#include <catch2/catch_test_macros.hpp>

// winsock2.h 는 windows.h 보다 먼저 와야 한다.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>

#include "platform/windows/WindowsConnectionScanner.h"

using namespace pulse;

namespace {

// WSAStartup 와 짝을 이루는 WSACleanup. REQUIRE 가 실패해도 정리되도록 가장 먼저 만든다.
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

// 이 프로세스 안에서 127.0.0.1 의 TCP 리스너와 그에 붙은 클라이언트를 연다. 소멸 시 모두 닫는다.
class LoopbackPair {
public:
    LoopbackPair() {
        REQUIRE(winsock_.started());

        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listener_ != INVALID_SOCKET);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        REQUIRE(::listen(listener_, 1) == 0);
        int length = sizeof(address);
        REQUIRE(::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        listener_port_ = ::ntohs(address.sin_port);

        client_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(client_ != INVALID_SOCKET);
        REQUIRE(::connect(client_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        accepted_ = ::accept(listener_, nullptr, nullptr);
        REQUIRE(accepted_ != INVALID_SOCKET);

        int client_length = sizeof(address);
        REQUIRE(::getsockname(client_, reinterpret_cast<sockaddr*>(&address), &client_length) == 0);
        client_port_ = ::ntohs(address.sin_port);
    }

    ~LoopbackPair() {
        // 생성자가 중간에 실패하면 일부 소켓만 열려 있다.
        for (const SOCKET socket : {accepted_, client_, listener_}) {
            if (socket != INVALID_SOCKET) {
                ::closesocket(socket);
            }
        }
    }
    LoopbackPair(const LoopbackPair&) = delete;
    LoopbackPair& operator=(const LoopbackPair&) = delete;

    uint16_t listenerPort() const { return listener_port_; }
    uint16_t clientPort() const { return client_port_; }

private:
    WinsockSession winsock_;
    SOCKET listener_ = INVALID_SOCKET;
    SOCKET client_ = INVALID_SOCKET;
    SOCKET accepted_ = INVALID_SOCKET;
    uint16_t listener_port_ = 0;
    uint16_t client_port_ = 0;
};

const RawConnection* find(const ConnectionScan& scan, NetProtocol protocol, uint16_t local_port,
                          TcpState state, uint16_t remote_port) {
    const auto it = std::find_if(scan.connections.begin(), scan.connections.end(), [&](const RawConnection& c) {
        return c.protocol == protocol && c.local_port == local_port && c.state == state &&
               c.remote_port == remote_port && c.pid == ::GetCurrentProcessId();
    });
    return it == scan.connections.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("the scanner sees this process's loopback TCP connections", "[network][integration]") {
    LoopbackPair pair;
    WindowsConnectionScanner scanner;

    const ConnectionScan scan = scanner.scan();

    CHECK(scan.error.empty());
    const RawConnection* listening = find(scan, NetProtocol::Tcp, pair.listenerPort(), TcpState::Listen, 0);
    REQUIRE(listening != nullptr);
    CHECK(listening->local_ip == "127.0.0.1");

    const RawConnection* from_client =
        find(scan, NetProtocol::Tcp, pair.clientPort(), TcpState::Established, pair.listenerPort());
    REQUIRE(from_client != nullptr);
    CHECK(from_client->local_ip == "127.0.0.1");
    CHECK(from_client->remote_ip == "127.0.0.1");

    const RawConnection* from_server =
        find(scan, NetProtocol::Tcp, pair.listenerPort(), TcpState::Established, pair.clientPort());
    REQUIRE(from_server != nullptr);
}

TEST_CASE("the scanner sees a UDP socket with no remote address", "[network][integration]") {
    WinsockSession winsock;
    REQUIRE(winsock.started());
    SOCKET udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(udp != INVALID_SOCKET);
    struct Closer {
        SOCKET socket;
        ~Closer() { ::closesocket(socket); }
    } closer{udp};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(udp, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int length = sizeof(address);
    REQUIRE(::getsockname(udp, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const uint16_t port = ::ntohs(address.sin_port);

    WindowsConnectionScanner scanner;
    const ConnectionScan scan = scanner.scan();

    CHECK(scan.error.empty());
    const RawConnection* found = find(scan, NetProtocol::Udp, port, TcpState::None, 0);
    REQUIRE(found != nullptr);
    CHECK(found->local_ip == "127.0.0.1");
    CHECK(found->remote_ip.empty());
}
