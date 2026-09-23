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

TEST_CASE("the hello message carries the reader's host info", "[serve]") {
    FakeSystemReader reader(someSamples(), 6);
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;

    std::atomic<unsigned short> port{0};
    ServeResult result;

    std::thread server_thread([&] {
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

TEST_CASE("a snapshot follows the hello message", "[serve]") {
    FakeSystemReader reader(someSamples(), 6);
    ServeConfig cfg;
    cfg.iterations = 3;
    cfg.interval_ms = 50;
    cfg.server.port = 0;

    std::atomic<unsigned short> port{0};

    std::thread server_thread([&] {
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
