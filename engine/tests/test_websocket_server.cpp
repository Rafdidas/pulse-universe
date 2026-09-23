#include <catch2/catch_test_macros.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "network/WebSocketServer.h"

using namespace pulse;
namespace net = boost::asio;
namespace beast = boost::beast;
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
    net::io_context ioc;
    ServerConfig cfg;
    cfg.port = 0;
    WebSocketServer server(ioc, cfg);

    std::atomic<bool> ran_after_stop{false};
    std::thread io([&] { ioc.run(); });

    std::thread stopper([&] {
        server.stop();
        net::post(ioc, [&] { ran_after_stop.store(true); });
    });

    stopper.join();
    io.join();

    REQUIRE(ran_after_stop.load());
}
