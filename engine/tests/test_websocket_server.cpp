#include <catch2/catch_test_macros.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <windows.h>

#include "network/AssetPack.h"
#include "network/WebSocketServer.h"
#include "pak_builder.h"

using namespace pulse;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = net::ip::tcp;

namespace {

std::shared_ptr<const std::string> msg(std::string text) {
    return std::make_shared<const std::string>(std::move(text));
}

// 서버를 자기 스레드에서 돌리고 소멸 시 정리한다.
class ServerFixture {
public:
    explicit ServerFixture(ServerConfig cfg = ServerConfig{}) {
        cfg.port = 0;  // 임시 포트
        server_ = std::make_unique<WebSocketServer>(ioc_, cfg);
        port_ = server_->port();
        thread_ = std::thread([this] { ioc_.run(); });
    }

    ~ServerFixture() {
        server_->stop();
        thread_.join();
    }

    WebSocketServer& server() { return *server_; }
    unsigned short port() const { return port_; }

private:
    net::io_context ioc_;
    std::unique_ptr<WebSocketServer> server_;
    unsigned short port_ = 0;
    std::thread thread_;
};

// 접속해서 메시지를 읽는 동기 클라이언트.
class TestClient {
public:
    TestClient(unsigned short port, const std::string& origin = {}) : ws_(ioc_) {
        tcp::resolver resolver(ioc_);
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
        net::connect(ws_.next_layer(), endpoints);

        if (!origin.empty()) {
            ws_.set_option(websocket::stream_base::decorator(
                [origin](websocket::request_type& req) {
                    req.set(boost::beast::http::field::origin, origin);
                }));
        }
        ws_.handshake("127.0.0.1", "/");
    }

    std::string read() {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

    void close() { ws_.close(websocket::close_code::normal); }

    // linger 0 으로 닫으면 FIN 대신 RST 가 나가 서버의 쓰기가 실패한다.
    void abort() {
        beast::error_code ignored;
        ws_.next_layer().set_option(net::socket_base::linger(true, 0), ignored);
        ws_.next_layer().close(ignored);
    }

    // 서버가 보낸 close 프레임을 받을 때까지 읽는다.
    // 정상 종료면 websocket::error::closed, 소켓만 끊기면 다른 코드가 나온다.
    beast::error_code readUntilClosed() {
        beast::error_code ec;
        for (int i = 0; i < 10; ++i) {
            beast::flat_buffer buffer;
            ws_.read(buffer, ec);
            if (ec) {
                return ec;
            }
        }
        return ec;
    }

private:
    net::io_context ioc_;
    websocket::stream<tcp::socket> ws_;
};

}  // namespace

TEST_CASE("the server binds an ephemeral port on request", "[ws]") {
    ServerFixture fixture;

    REQUIRE(fixture.port() != 0);
}

TEST_CASE("a new client receives the hello message first", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());

    REQUIRE(client.read() == R"({"type":"hello"})");
    client.close();
}

TEST_CASE("a new client receives the latest snapshot right after hello", "[ws]") {
    // 접속 시점이 샘플링 주기와 무관하므로, 보관본이 없으면 화면이
    // 최대 한 주기 동안 비어 있다 — 스펙 6.3 절.
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));
    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":7})"));

    TestClient client(fixture.port());

    REQUIRE(client.read() == R"({"type":"hello"})");
    REQUIRE(client.read() == R"({"type":"snapshot","seq":7})");
    client.close();
}

TEST_CASE("a broadcast reaches a connected client", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());
    REQUIRE(client.read() == R"({"type":"hello"})");

    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":1})"));

    REQUIRE(client.read() == R"({"type":"snapshot","seq":1})");
    client.close();
}

TEST_CASE("a broadcast reaches every connected client", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient a(fixture.port());
    TestClient b(fixture.port());
    REQUIRE(a.read() == R"({"type":"hello"})");
    REQUIRE(b.read() == R"({"type":"hello"})");

    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":1})"));

    REQUIRE(a.read() == R"({"type":"snapshot","seq":1})");
    REQUIRE(b.read() == R"({"type":"snapshot","seq":1})");
    a.close();
    b.close();
}

TEST_CASE("an allowed origin is accepted", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port(), "http://localhost:5173");

    REQUIRE(client.read() == R"({"type":"hello"})");
    client.close();
}

TEST_CASE("a disallowed origin is rejected", "[ws]") {
    ServerFixture fixture;

    REQUIRE_THROWS([&] { TestClient client(fixture.port(), "http://evil.example"); }());
}

TEST_CASE("the server survives a client disconnecting", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    {
        TestClient first(fixture.port());
        REQUIRE(first.read() == R"({"type":"hello"})");
        first.close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TestClient second(fixture.port());
    REQUIRE(second.read() == R"({"type":"hello"})");
    second.close();
}

TEST_CASE("a client that stops reading does not grow an unbounded queue", "[ws]") {
    // 세션당 대기 스냅샷은 1개. 밀린 것은 새 것으로 덮어쓴다 — 스펙 7 절.
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());
    REQUIRE(client.read() == R"({"type":"hello"})");

    for (int i = 1; i <= 50; ++i) {
        fixture.server().broadcast(msg(R"({"type":"snapshot","seq":)" + std::to_string(i) + "}"));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 큐가 자라지 않았다면 밀린 50개가 전부 오지는 않는다.
    // 적어도 하나는 오고, 마지막 값이 결국 도달한다.
    bool saw_last = false;
    for (int reads = 0; reads < 5 && !saw_last; ++reads) {
        if (client.read() == R"({"type":"snapshot","seq":50})") {
            saw_last = true;
        }
    }
    REQUIRE(saw_last);
    client.close();
}

TEST_CASE("a second server cannot bind a port already in use", "[ws]") {
    // SO_REUSEADDR 이 설정되어 있으면 두 번째 바인딩이 조용히 성공해
    // 포트를 가로챈다. 실패해야 한다.
    net::io_context first_ioc;
    ServerConfig first_cfg;
    first_cfg.port = 0;
    WebSocketServer first(first_ioc, first_cfg);

    ServerConfig second_cfg;
    second_cfg.port = first.port();

    net::io_context second_ioc;
    REQUIRE_THROWS([&] { WebSocketServer second(second_ioc, second_cfg); }());

    first.stop();
    first_ioc.run();
}

TEST_CASE("run returns only after posted shutdown work has executed", "[ws]") {
    // runServe 는 이 성질에 기댄다. stop() 은 세션 정리를 post 할 뿐이므로,
    // run() 이 큐에 남은 핸들러를 건너뛰고 돌아오면 정상 종료가 사라진다.
    // stop() 자신이 post 한 핸들러(세션의 close 프레임 전송)가 실제로
    // 실행되었는지를 클라이언트 쪽에서 관찰해야 이 성질을 증명한다 —
    // 제3의 스레드가 stop() 이 반환된 뒤에 post 한 플래그는 Asio 가 순서를
    // 보장하지 않는 다른 성질이다.
    net::io_context ioc;
    ServerConfig cfg;
    cfg.port = 0;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    server.setHello(msg(R"({"type":"hello"})"));

    std::thread io([&] { ioc.run(); });

    TestClient client(port);
    REQUIRE(client.read() == R"({"type":"hello"})");

    server.stop();

    const beast::error_code ec = client.readUntilClosed();
    io.join();

    REQUIRE(ec == websocket::error::closed);
}

TEST_CASE("the server closes sessions with a websocket close frame", "[ws]") {
    // 서버가 소켓을 그냥 닫으면 클라이언트는 1006(비정상)을 본다.
    // 정상 종료 프레임을 보내야 M3 프론트엔드가 "서버가 닫았다" 를
    // "연결이 끊겼다" 와 구분할 수 있다.
    net::io_context ioc;
    ServerConfig cfg;
    cfg.port = 0;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    server.setHello(msg(R"({"type":"hello"})"));

    std::thread io([&] { ioc.run(); });

    TestClient client(port);
    REQUIRE(client.read() == R"({"type":"hello"})");

    server.stop();

    const beast::error_code ec = client.readUntilClosed();
    io.join();

    REQUIRE(ec == websocket::error::closed);
}

TEST_CASE("the server drains after a client aborts mid-write", "[ws]") {
    net::io_context ioc;
    ServerConfig cfg;
    cfg.port = 0;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    server.setHello(msg(R"({"type":"hello"})"));

    std::thread io([&] { ioc.run(); });

    {
        TestClient client(port);
        REQUIRE(client.read() == R"({"type":"hello"})");

        // 큰 메시지를 여러 번 밀어 넣어 쓰기가 진행 중일 확률을 높인다.
        const std::string big(64 * 1024, 'x');
        for (int i = 0; i < 8; ++i) {
            server.broadcast(msg(big));
        }
        client.abort();
    }

    server.stop();
    io.join();  // 드레인되지 않으면 여기서 멈춘다

    SUCCEED("io_context drained after the client aborted the connection mid-write");
}

namespace {

// 임시 디렉터리에 작은 web-root 를 만들고 소멸 시 지운다.
class TempWebRoot {
public:
    TempWebRoot() {
        root_ = std::filesystem::temp_directory_path() /
                ("pulse-web-" + std::to_string(::GetCurrentProcessId()) + "-" +
                 std::to_string(counter_++));
        std::filesystem::create_directories(root_ / "assets");
        write(root_ / "index.html", "<html>index</html>");
        write(root_ / "assets" / "app.js", "console.log(1);");
    }

    ~TempWebRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::string path() const { return root_.string(); }

private:
    static void write(const std::filesystem::path& file, const std::string& body) {
        std::ofstream out(file, std::ios::binary);
        out << body;
    }

    static inline int counter_ = 0;
    std::filesystem::path root_;
};

// 동기 HTTP 요청. 응답 전체를 돌려준다. method 는 기본이 GET.
http::response<http::string_body> httpRequest(unsigned short port, const std::string& target,
                                              http::verb method = http::verb::get) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));

    http::request<http::string_body> request(method, target, 11);
    request.set(http::field::host, "127.0.0.1");
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    return response;
}

http::response<http::string_body> httpGet(unsigned short port, const std::string& target) {
    return httpRequest(port, target, http::verb::get);
}

}  // namespace

TEST_CASE("a static file is served when a web root is configured", "[ws]") {
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/assets/app.js");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "console.log(1);");
    REQUIRE(response[http::field::content_type] == "text/javascript");
}

TEST_CASE("an unknown path falls back to index.html", "[ws]") {
    // SPA 라우팅. 브라우저가 /universe 로 새로고침해도 앱이 뜬다.
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/universe");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "<html>index</html>");
}

TEST_CASE("a path escaping the web root is refused", "[ws]") {
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/../../windows/win.ini");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::forbidden);
}

TEST_CASE("a colon-style path falls back to the app instead of 403", "[ws]") {
    // /chrome.exe:1234 는 Windows 에서 대체 데이터 스트림 구문으로 파싱되어
    // weakly_canonical 이 해석할 수 없다. 그룹 키가 이런 모양이므로 탈출
    // 시도로 취급해 403 을 주면 안 되고, 없는 파일처럼 SPA 폴백을 태워야 한다.
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/chrome.exe:1234");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "<html>index</html>");
}

TEST_CASE("an unresolvable path falls back to the app instead of 403", "[ws]") {
    // resolveWebPath 가 Unresolvable 을 내는 경우(서로를 가리키는 순환
    // 정션)를 엔드투엔드로 태운다. 이 기기에서는 /chrome.exe:1234 가
    // Ok 로 풀려버려 그 테스트가 실제로는 이 분기를 태우지 못한다.
    TempWebRoot web;
    const std::filesystem::path root(web.path());
    std::error_code ignored;

    std::filesystem::create_directories(root / "b", ignored);
    const int created_a =
        std::system(("cmd /c mklink /J \"" + (root / "a").string() + "\" \"" +
                     (root / "b").string() + "\" >nul 2>&1")
                        .c_str());
    REQUIRE(created_a == 0);

    std::filesystem::remove(root / "b", ignored);
    const int created_b =
        std::system(("cmd /c mklink /J \"" + (root / "b").string() + "\" \"" +
                     (root / "a").string() + "\" >nul 2>&1")
                        .c_str());
    REQUIRE(created_b == 0);

    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/a/x");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "<html>index</html>");
}

TEST_CASE("a non-GET request to a web root is rejected with 405", "[ws]") {
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpRequest(port, "/", http::verb::post);

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::method_not_allowed);
    REQUIRE(response[http::field::allow] == "GET");
}

TEST_CASE("a served index.html carries no-cache and nosniff headers", "[ws]") {
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response[http::field::cache_control] == "no-cache");
    REQUIRE(response[http::field::x_content_type_options] == "nosniff");
}

TEST_CASE("a non-index static file has no cache-control header", "[ws]") {
    TempWebRoot web;
    ServerConfig cfg;
    cfg.port = 0;
    cfg.web_root = web.path();

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/assets/app.js");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response[http::field::cache_control].empty());
    REQUIRE(response[http::field::x_content_type_options] == "nosniff");
}

TEST_CASE("without a web root a plain request gets 426 rather than silence", "[ws]") {
    // 이게 없으면 브라우저로 주소를 열어본 개발자가 빈 화면만 본다.
    ServerConfig cfg;
    cfg.port = 0;

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpGet(port, "/");

    server.stop();
    io.join();

    REQUIRE(response.result() == http::status::upgrade_required);
}

namespace {

// pak 모드 서버를 띄우고 요청 하나를 보낸 뒤 응답을 돌려준다.
http::response<http::string_body> packRequest(const std::string& pak_bytes,
                                              const std::string& target,
                                              http::verb method = http::verb::get) {
    const auto pack = AssetPack::parse(pak_bytes);
    REQUIRE(pack.has_value());

    ServerConfig cfg;
    cfg.port = 0;
    cfg.assets = &*pack;

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpRequest(port, target, method);

    server.stop();
    io.join();
    return response;
}

const std::string kPak = pulse_test::makePak({{"/assets/app.js", "console.log(1);"},
                                              {"/index.html", "<html>index</html>"}});

}  // namespace

TEST_CASE("an embedded asset is served with its content type", "[ws][pack]") {
    const auto response = packRequest(kPak, "/assets/app.js");

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "console.log(1);");
    REQUIRE(response[http::field::content_type] == "text/javascript");
    REQUIRE(response[http::field::cache_control].empty());
}

TEST_CASE("the embedded index carries no-cache and nosniff, and unknown paths fall back to it",
          "[ws][pack]") {
    const auto root = packRequest(kPak, "/");
    REQUIRE(root.result() == http::status::ok);
    REQUIRE(root.body() == "<html>index</html>");
    REQUIRE(root[http::field::cache_control] == "no-cache");
    REQUIRE(root[http::field::x_content_type_options] == "nosniff");

    const auto fallback = packRequest(kPak, "/universe");
    REQUIRE(fallback.result() == http::status::ok);
    REQUIRE(fallback.body() == "<html>index</html>");
}

TEST_CASE("an embedded path escaping the root is refused", "[ws][pack]") {
    REQUIRE(packRequest(kPak, "/../secret").result() == http::status::forbidden);
}

TEST_CASE("a non-GET request to embedded assets is rejected with 405", "[ws][pack]") {
    const auto response = packRequest(kPak, "/", http::verb::post);

    REQUIRE(response.result() == http::status::method_not_allowed);
    REQUIRE(response[http::field::allow] == "GET");
}

TEST_CASE("an embedded path with a backslash is refused", "[ws][pack]") {
    REQUIRE(packRequest(kPak, "/assets\\app.js").result() == http::status::forbidden);
}

TEST_CASE("an embedded asset is found with its query string stripped", "[ws][pack]") {
    const auto response = packRequest(kPak, "/assets/app.js?v=1");

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "console.log(1);");
    REQUIRE(response[http::field::content_type] == "text/javascript");
}

TEST_CASE("an embedded path with a colon falls back to the index instead of 403", "[ws][pack]") {
    const auto response = packRequest(kPak, "/assets/app.js:stream");

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "<html>index</html>");
}
