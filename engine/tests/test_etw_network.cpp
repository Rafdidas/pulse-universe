#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// winsock2.h 는 windows.h 보다 먼저 와야 한다.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <map>
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

struct Wanted {
    FlowKey key;
    uint64_t min_sent = 0;
    uint64_t min_received = 0;
};

FlowKey loopbackKey(NetProtocol protocol, uint16_t port1, uint16_t port2, const IpBytes& address) {
    return makeFlowKey(protocol, ::GetCurrentProcessId(), address, port1, address, port2);
}

// 여러 번 drain 해 합친다. 이벤트는 강제 플러시 뒤에도 수백 ms 늦게 도착하므로(진단 시험: 100 개 데이터그램이
// 보낸 뒤 약 0.3~0.5 초에 모였다), 기대한 바이트가 모일 때까지 최대 5 초 기다린다.
RawNetworkTraffic gather(EtwNetworkCollector& collector, const std::vector<Wanted>& wanted) {
    std::map<FlowKey, RawFlowTraffic> merged;
    RawNetworkTraffic total;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (const std::optional<RawNetworkTraffic> traffic = collector.drain()) {
            total.window_seconds += traffic->window_seconds;
            for (const RawFlowTraffic& flow : traffic->flows) {
                RawFlowTraffic& sum = merged[flow.key];
                sum.key = flow.key;
                sum.bytes_sent += flow.bytes_sent;
                sum.bytes_received += flow.bytes_received;
            }
        }
        bool done = true;
        for (const Wanted& w : wanted) {
            const auto it = merged.find(w.key);
            if (it == merged.end() || it->second.bytes_sent < w.min_sent || it->second.bytes_received < w.min_received) {
                done = false;
            }
        }
        if (done || std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
    for (auto& entry : merged) {
        total.flows.push_back(entry.second);
    }
    return total;
}

}  // namespace

TEST_CASE("the ETW network collector counts the bytes of a loopback TCP transfer when elevated", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    // 공급자를 켠 직후 잠시는 이벤트가 오지 않는다. 기다린 뒤 창을 연다.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    collector->drain();  // 창의 시작

    WinsockSession winsock;
    REQUIRE(winsock.started());
    constexpr size_t kTotal = 5 * 1024 * 1024;
    // 한 번에 보내는 크기: 큰 조각과 작은 조각(이벤트 수가 많은 경우) 둘 다 시험한다.
    const size_t chunk_size = GENERATE(size_t{1024}, size_t{64 * 1024});
    INFO("chunk size " << chunk_size);

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
    std::vector<char> chunk(chunk_size, 'x');
    size_t sent = 0;
    while (sent < kTotal) {
        const int n = ::send(client, chunk.data(), static_cast<int>(chunk.size()), 0);
        REQUIRE(n > 0);
        sent += static_cast<size_t>(n);
    }
    receiver.join();
    const RawNetworkTraffic traffic = gather(
        *collector, {{loopbackKey(NetProtocol::Tcp, listener_port, client_port, loopbackBytes()), kTotal, kTotal}});
    ::closesocket(accepted);
    ::closesocket(client);
    ::closesocket(listener);

    const RawFlowTraffic flow = findFlow(traffic, NetProtocol::Tcp, listener_port, client_port);
    CAPTURE(traffic.flows.size(), flow.bytes_sent, flow.bytes_received);
    CHECK(flow.bytes_sent >= kTotal);
    CHECK(flow.bytes_received >= kTotal);
}

TEST_CASE("the ETW network collector counts loopback UDP datagrams when elevated", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
    const uint64_t expected = static_cast<uint64_t>(kDatagrams) * kSize;
    const RawNetworkTraffic traffic = gather(
        *collector, {{loopbackKey(NetProtocol::Udp, sender_port, receiver_port, loopbackBytes()), expected, expected}});
    ::closesocket(sender);
    ::closesocket(receiver);

    const RawFlowTraffic flow = findFlow(traffic, NetProtocol::Udp, sender_port, receiver_port);
    CAPTURE(traffic.flows.size(), flow.bytes_sent, flow.bytes_received);
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

TEST_CASE("the ETW network collector counts IPv6 loopback TCP and UDP when elevated", "[etw][network]") {
    std::string error;
    std::unique_ptr<EtwNetworkCollector> collector = EtwNetworkCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW network collector unavailable: " + error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    collector->drain();

    WinsockSession winsock;
    REQUIRE(winsock.started());
    const IpBytes loopback6 = parseIp("::1")->bytes;
    const auto portOf = [](SOCKET socket) {
        sockaddr_in6 address{};
        int length = sizeof(address);
        REQUIRE(::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        return static_cast<uint16_t>(::ntohs(address.sin6_port));
    };
    sockaddr_in6 any{};
    any.sin6_family = AF_INET6;
    any.sin6_addr = in6addr_loopback;

    // TCP: 1 MB 를 16 KB 조각으로.
    constexpr size_t kTcpTotal = 1024 * 1024;
    SOCKET listener = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(listener != INVALID_SOCKET);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr*>(&any), sizeof(any)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    const uint16_t listener_port = portOf(listener);
    sockaddr_in6 target = any;
    target.sin6_port = ::htons(listener_port);
    SOCKET client = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(client != INVALID_SOCKET);
    REQUIRE(::connect(client, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0);
    SOCKET accepted = ::accept(listener, nullptr, nullptr);
    REQUIRE(accepted != INVALID_SOCKET);
    const uint16_t client_port = portOf(client);
    std::thread receiver([&] {
        std::vector<char> buffer(16 * 1024);
        size_t received = 0;
        while (received < kTcpTotal) {
            const int n = ::recv(accepted, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (n <= 0) {
                break;
            }
            received += static_cast<size_t>(n);
        }
    });
    std::vector<char> chunk(16 * 1024, 't');
    for (size_t sent = 0; sent < kTcpTotal;) {
        const int n = ::send(client, chunk.data(), static_cast<int>(chunk.size()), 0);
        REQUIRE(n > 0);
        sent += static_cast<size_t>(n);
    }
    receiver.join();

    // UDP: 50 개의 1 KB 데이터그램.
    SOCKET udp_receiver = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET udp_sender = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(udp_receiver != INVALID_SOCKET);
    REQUIRE(udp_sender != INVALID_SOCKET);
    REQUIRE(::bind(udp_receiver, reinterpret_cast<sockaddr*>(&any), sizeof(any)) == 0);
    REQUIRE(::bind(udp_sender, reinterpret_cast<sockaddr*>(&any), sizeof(any)) == 0);
    const uint16_t udp_receiver_port = portOf(udp_receiver);
    const uint16_t udp_sender_port = portOf(udp_sender);
    sockaddr_in6 udp_target = any;
    udp_target.sin6_port = ::htons(udp_receiver_port);
    std::vector<char> datagram(1024, 'u');
    std::vector<char> buffer(1024);
    for (int i = 0; i < 50; ++i) {
        REQUIRE(::sendto(udp_sender, datagram.data(), 1024, 0, reinterpret_cast<sockaddr*>(&udp_target),
                         sizeof(udp_target)) == 1024);
        REQUIRE(::recv(udp_receiver, buffer.data(), 1024, 0) == 1024);
    }
    const RawNetworkTraffic traffic =
        gather(*collector, {{loopbackKey(NetProtocol::Tcp, listener_port, client_port, loopback6), kTcpTotal, kTcpTotal},
                            {loopbackKey(NetProtocol::Udp, udp_sender_port, udp_receiver_port, loopback6), 50u * 1024u,
                             50u * 1024u}});
    for (const SOCKET socket : {accepted, client, listener, udp_receiver, udp_sender}) {
        ::closesocket(socket);
    }

    const auto find = [&](NetProtocol protocol, uint16_t p1, uint16_t p2) {
        const FlowKey wanted = loopbackKey(protocol, p1, p2, loopback6);
        for (const RawFlowTraffic& flow : traffic.flows) {
            if (flow.key == wanted) {
                return flow;
            }
        }
        return RawFlowTraffic{};
    };
    const RawFlowTraffic tcp_flow = find(NetProtocol::Tcp, listener_port, client_port);
    CAPTURE(traffic.flows.size(), tcp_flow.bytes_sent, tcp_flow.bytes_received);
    CHECK(tcp_flow.bytes_sent >= kTcpTotal);
    CHECK(tcp_flow.bytes_received >= kTcpTotal);
    const RawFlowTraffic udp_flow = find(NetProtocol::Udp, udp_sender_port, udp_receiver_port);
    CAPTURE(udp_flow.bytes_sent, udp_flow.bytes_received);
    CHECK(udp_flow.bytes_sent >= 50u * 1024u);
    CHECK(udp_flow.bytes_received >= 50u * 1024u);
}
