#include <catch2/catch_test_macros.hpp>

// winsock2.h 는 windows.h 보다 먼저 와야 한다.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "core/FlowKey.h"
#include "core/IpAddress.h"
#include "platform/windows/EtwNetworkCollector.h"

using namespace pulse;

namespace {

// M11 스펙 6절. 관리자 권한에서만 돈다. 권한이 없으면 SKIP 이다. 루프백으로 알려진 크기의 데이터를 주고받고,
// 수집기가 그 PID 의 플로우에서 같은 크기를 보았는지 확인한다 — 이벤트 배치(스펙 5절)를 증명한다.

class WinsockSession {
public:
    WinsockSession() {
        WSADATA data;
        started_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession() {
        if (started_) {
            ::WSACleanup();
        }
    }
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;
    bool started() const { return started_; }

private:
    bool started_ = false;
};

IpBytes loopbackBytes() {
    return parseIp("127.0.0.1")->bytes;
}

// 이 프로세스의 플로우 중 (프로토콜, 포트 쌍)이 같은 것의 합. 같은 키에 송신(클라이언트)과 수신(서버)이 함께 쌓인다.
RawFlowTraffic findFlow(const RawNetworkTraffic& traffic, NetProtocol protocol, uint16_t port1, uint16_t port2) {
    const FlowKey wanted = makeFlowKey(protocol, ::GetCurrentProcessId(), loopbackBytes(), port1, loopbackBytes(), port2);
    for (const RawFlowTraffic& flow : traffic.flows) {
        if (flow.key == wanted) {
            return flow;
        }
    }
    return RawFlowTraffic{};
}

}  // namespace

TEST_CASE("the ETW network collector counts the bytes of a loopback TCP transfer when elevated", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    collector->drain();  // 창의 시작

    WinsockSession winsock;
    REQUIRE(winsock.started());
    constexpr size_t kTotal = 5 * 1024 * 1024;

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    int length = sizeof(address);
    REQUIRE(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const uint16_t listener_port = ::ntohs(address.sin_port);

    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(client != INVALID_SOCKET);
    REQUIRE(::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    SOCKET accepted = ::accept(listener, nullptr, nullptr);
    REQUIRE(accepted != INVALID_SOCKET);
    sockaddr_in local{};
    int local_length = sizeof(local);
    REQUIRE(::getsockname(client, reinterpret_cast<sockaddr*>(&local), &local_length) == 0);
    const uint16_t client_port = ::ntohs(local.sin_port);

    std::thread receiver([&] {
        std::vector<char> buffer(64 * 1024);
        size_t received = 0;
        while (received < kTotal) {
            const int n = ::recv(accepted, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (n <= 0) {
                break;
            }
            received += static_cast<size_t>(n);
        }
    });
    std::vector<char> chunk(64 * 1024, 'x');
    size_t sent = 0;
    while (sent < kTotal) {
        const int n = ::send(client, chunk.data(), static_cast<int>(chunk.size()), 0);
        REQUIRE(n > 0);
        sent += static_cast<size_t>(n);
    }
    receiver.join();
    // 이벤트는 버퍼 플러시(1 초)마다 전달된다. 넉넉히 기다린다.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    const std::optional<RawNetworkTraffic> traffic = collector->drain();
    ::closesocket(accepted);
    ::closesocket(client);
    ::closesocket(listener);

    REQUIRE(traffic.has_value());
    REQUIRE(traffic->window_seconds > 1.0);
    const RawFlowTraffic flow = findFlow(*traffic, NetProtocol::Tcp, listener_port, client_port);
    CAPTURE(traffic->flows.size(), flow.bytes_sent, flow.bytes_received);
    CHECK(flow.bytes_sent >= kTotal);
    CHECK(flow.bytes_received >= kTotal);
}

TEST_CASE("the ETW network collector counts loopback UDP datagrams when elevated", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    collector->drain();

    WinsockSession winsock;
    REQUIRE(winsock.started());
    SOCKET receiver = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(receiver != INVALID_SOCKET);
    REQUIRE(sender != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(::bind(sender, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int length = sizeof(address);
    sockaddr_in receiver_address{};
    REQUIRE(::getsockname(receiver, reinterpret_cast<sockaddr*>(&receiver_address), &length) == 0);
    sockaddr_in sender_address{};
    int sender_length = sizeof(sender_address);
    REQUIRE(::getsockname(sender, reinterpret_cast<sockaddr*>(&sender_address), &sender_length) == 0);
    const uint16_t receiver_port = ::ntohs(receiver_address.sin_port);
    const uint16_t sender_port = ::ntohs(sender_address.sin_port);

    constexpr int kDatagrams = 100;
    constexpr int kSize = 1024;
    std::vector<char> data(kSize, 'u');
    std::vector<char> buffer(kSize);
    for (int i = 0; i < kDatagrams; ++i) {
        REQUIRE(::sendto(sender, data.data(), kSize, 0, reinterpret_cast<sockaddr*>(&receiver_address),
                         sizeof(receiver_address)) == kSize);
        REQUIRE(::recv(receiver, buffer.data(), kSize, 0) == kSize);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    const std::optional<RawNetworkTraffic> traffic = collector->drain();
    ::closesocket(sender);
    ::closesocket(receiver);

    REQUIRE(traffic.has_value());
    const RawFlowTraffic flow = findFlow(*traffic, NetProtocol::Udp, sender_port, receiver_port);
    CAPTURE(traffic->flows.size(), flow.bytes_sent, flow.bytes_received);
    CHECK(flow.bytes_sent >= static_cast<uint64_t>(kDatagrams) * kSize);
    CHECK(flow.bytes_received >= static_cast<uint64_t>(kDatagrams) * kSize);
}

TEST_CASE("the ETW network session is gone after the collector is destroyed", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    collector.reset();

    std::unique_ptr<EtwNetworkCollector> again = EtwNetworkCollector::start(error);
    REQUIRE(again != nullptr);
}
