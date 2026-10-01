#include <catch2/catch_test_macros.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "app/ServeApp.h"
#include "network/ServerConfig.h"
#include "network/WebSocketServer.h"
#include "platform/RawTypes.h"

#include "fakes/FakeNetwork.h"
#include "fakes/FakeSystemReader.h"

using namespace pulse;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace json = boost::json;
using tcp = net::ip::tcp;

namespace {

// 접속해서 메시지를 읽는 동기 클라이언트. test_websocket_server.cpp 의
// 것과 같은 구조지만, 이 파일과 그쪽은 독립적인 테스트라 헤더 하나를
// 공유할 가치가 없어 그대로 복사한다.
class TestClient {
public:
    explicit TestClient(unsigned short port) : ws_(ioc_) {
        tcp::resolver resolver(ioc_);
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
        net::connect(ws_.next_layer(), endpoints);
        ws_.handshake("127.0.0.1", "/");
    }

    std::string read() {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

    void close() {
        beast::error_code ignored;
        ws_.close(websocket::close_code::normal, ignored);
    }

private:
    net::io_context ioc_;
    websocket::stream<tcp::socket> ws_;
};

std::vector<RawSample> someSamples() {
    RawSample sample;
    sample.timestamp_ms = 1000;
    return {sample, sample, sample};
}

}  // namespace

TEST_CASE("runServe reports a bind failure", "[serve]") {
    net::io_context ioc;
    ServerConfig occupied_cfg;
    occupied_cfg.port = 0;
    WebSocketServer occupied(ioc, occupied_cfg);
    const unsigned short port = occupied.port();

    FakeSystemReader reader(someSamples(), 4);
    ServeConfig cfg;
    cfg.server.port = port;

    const ServeResult result = runServe(reader, cfg);

    REQUIRE(result.exit_code == 2);
    REQUIRE(result.message.find(std::to_string(port)) != std::string::npos);

    occupied.stop();
    ioc.run();
}

TEST_CASE("runServe completes cleanly after its iterations", "[serve]") {
    FakeSystemReader reader(someSamples(), 4);
    ServeConfig cfg;
    cfg.iterations = 2;
    cfg.interval_ms = 1;
    cfg.server.port = 0;

    const ServeResult result = runServe(reader, cfg);

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.message.empty());
}

TEST_CASE("runServe reports a sampling failure", "[serve]") {
    ThrowingSystemReader reader(1);
    ServeConfig cfg;
    cfg.iterations = 5;
    cfg.server.port = 0;

    const ServeResult result = runServe(reader, cfg);

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.message.find("sampling failed") != std::string::npos);
}

// 서버 스레드는 std::jthread 로 둔다. 클라이언트 접속이나 단언이 join 전에
// 예외를 던지면, join 되지 않은 std::thread 가 소멸하면서 std::terminate()
// → abort() 로 바이너리 전체가 죽는다 — 실패를 보고하는 대신 Debug CRT
// 대화상자만 남는다. jthread 는 소멸 시 join 하므로 예외가 정상적으로
// Catch2 까지 올라간다. runServe 는 모두 유한한 iterations 로 부르므로
// join 은 항상 끝난다.
TEST_CASE("the hello message carries the reader's host info", "[serve]") {
    FakeSystemReader reader(someSamples(), 6);
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;

    std::atomic<unsigned short> port{0};
    ServeResult result;

    std::jthread server_thread([&] {
        result = runServe(reader, cfg,
                          [&](unsigned short p) { port.store(p); });
    });

    while (port.load() == 0) {
        std::this_thread::yield();
    }

    TestClient client(port.load());
    const std::string hello_text = client.read();
    const json::value hello = json::parse(hello_text);

    REQUIRE(hello.at("type").as_string() == "hello");
    REQUIRE(hello.at("core_count").as_int64() == 6);
    REQUIRE(hello.at("host").at("os").as_string() == "FakeOS");

    client.close();
    server_thread.join();
}

namespace {

// 새 클라이언트를 붙여 hello 메시지 하나를 받아온다.
json::value receiveHello(FakeSystemReader& reader, const ServeConfig& cfg) {
    std::atomic<unsigned short> port{0};

    std::jthread server_thread([&] {
        runServe(reader, cfg, [&](unsigned short p) { port.store(p); });
    });

    while (port.load() == 0) {
        std::this_thread::yield();
    }

    TestClient client(port.load());
    const std::string hello_text = client.read();
    const json::value hello = json::parse(hello_text);

    client.close();
    server_thread.join();
    return hello;
}

}  // namespace

TEST_CASE("the hello message carries a 16-character lowercase hex session", "[serve]") {
    FakeSystemReader reader(someSamples(), 6);
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;

    const json::value hello = receiveHello(reader, cfg);
    const std::string session(hello.at("session").as_string());

    REQUIRE(session.size() == 16);
    REQUIRE(session.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("two separate runServe runs produce different sessions", "[serve]") {
    FakeSystemReader reader_a(someSamples(), 6);
    ServeConfig cfg_a;
    cfg_a.iterations = 3;
    cfg_a.interval_ms = 50;
    cfg_a.server.port = 0;
    const json::value hello_a = receiveHello(reader_a, cfg_a);

    FakeSystemReader reader_b(someSamples(), 6);
    ServeConfig cfg_b;
    cfg_b.iterations = 3;
    cfg_b.interval_ms = 50;
    cfg_b.server.port = 0;
    const json::value hello_b = receiveHello(reader_b, cfg_b);

    REQUIRE(hello_a.at("session").as_string() != hello_b.at("session").as_string());
}

TEST_CASE("a snapshot follows the hello message", "[serve]") {
    FakeSystemReader reader(someSamples(), 6);
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;

    std::atomic<unsigned short> port{0};

    std::jthread server_thread([&] {
        runServe(reader, cfg, [&](unsigned short p) { port.store(p); });
    });

    while (port.load() == 0) {
        std::this_thread::yield();
    }

    TestClient client(port.load());
    client.read();  // hello
    const std::string snapshot_text = client.read();
    const json::value snapshot = json::parse(snapshot_text);

    REQUIRE(snapshot.at("type").as_string() == "snapshot");

    client.close();
    server_thread.join();
}

TEST_CASE("a client arriving after runServe has finished is refused cleanly", "[serve]") {
    // runServe 는 iterations 를 채우면 서버를 닫고 돌아온다. 그 뒤에 온
    // 클라이언트가 거절되는 것은 경쟁 조건이 아니라 기대 동작이다.
    FakeSystemReader reader(someSamples(), 4);
    ServeConfig cfg;
    cfg.iterations = 1;
    cfg.interval_ms = 1;
    cfg.server.port = 0;

    std::atomic<unsigned short> port{0};
    ServeResult result;
    {
        std::jthread server_thread([&] {
            result = runServe(reader, cfg, [&](unsigned short p) { port.store(p); });
        });
    }  // runServe 가 스스로 끝날 때까지 기다린다

    REQUIRE(result.exit_code == 0);
    REQUIRE(port.load() != 0);
    REQUIRE_THROWS(TestClient(port.load()));
}

TEST_CASE("the hello and the first snapshot describe the network block", "[serve][netsnapshot]") {
    FakeSystemReader reader(someSamples(), 4);
    FakeConnectionScanner scanner({});
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;
    cfg.network.scanner = &scanner;  // 트래픽 수집기는 없다

    std::atomic<unsigned short> port{0};
    std::jthread server_thread([&] { runServe(reader, cfg, [&](unsigned short p) { port.store(p); }); });
    while (port.load() == 0) {
        std::this_thread::yield();
    }
    TestClient client(port.load());
    const json::value hello = json::parse(client.read());
    const json::value snapshot = json::parse(client.read());
    client.close();
    server_thread.join();

    REQUIRE(hello.at("capabilities").at("network_traffic").as_string() == "unavailable");
    REQUIRE(snapshot.at("type").as_string() == "snapshot");
    REQUIRE(snapshot.at("network").at("traffic").as_string() == "unavailable");
    REQUIRE(snapshot.at("network").at("endpoints").is_array());
    REQUIRE(snapshot.at("network").at("summary").at("down_bps").is_null());
    REQUIRE(scanner.scanCount() >= 1);
}
