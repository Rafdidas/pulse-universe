#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/IpAddress.h"
#include "core/NetworkAggregator.h"

using namespace pulse;

namespace {

RawConnection tcp(uint32_t pid, const std::string& local, uint16_t local_port, const std::string& remote,
                  uint16_t remote_port, TcpState state = TcpState::Established) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Tcp;
    c.local_ip = local;
    c.local_port = local_port;
    c.remote_ip = remote;
    c.remote_port = remote_port;
    c.state = state;
    return c;
}

RawConnection udp(uint32_t pid, const std::string& local, uint16_t local_port) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Udp;
    c.local_ip = local;
    c.local_port = local_port;
    return c;
}

const std::unordered_map<uint32_t, std::string> kNames = {{100, "chrome.exe"}, {200, "git.exe"}, {300, "svchost.exe"}};

}  // namespace

TEST_CASE("parseIp reads IPv4 and IPv6 text", "[ip]") {
    REQUIRE(parseIp("192.168.0.10").has_value());
    REQUIRE(parseIp("0.0.0.0").has_value());
    REQUIRE(parseIp("::").has_value());
    REQUIRE(parseIp("::1").has_value());
    REQUIRE(parseIp("2001:db8::1").has_value());
    REQUIRE(parseIp("fe80::1%12").has_value());
    REQUIRE(parseIp("::ffff:127.0.0.1").has_value());
    REQUIRE(parseIp("1:2:3:4:5:6:7:8").has_value());

    for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "a.b.c.d", "1::2::3", ":::", "12345::1",
                            "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "::g", "1.2.3.4:80", "1.2.3.4::", "1.2.3.4::1",
                            "1:2:3:4:5:1.2.3.4:6"}) {
        INFO(bad);
        REQUIRE_FALSE(parseIp(bad).has_value());
    }
}

TEST_CASE("IpAddress classifies unspecified, loopback and private addresses", "[ip]") {
    CHECK(parseIp("0.0.0.0")->isUnspecified());
    CHECK(parseIp("::")->isUnspecified());
    CHECK_FALSE(parseIp("1.2.3.4")->isUnspecified());

    CHECK(parseIp("127.0.0.1")->isLoopback());
    CHECK(parseIp("127.255.255.254")->isLoopback());
    CHECK(parseIp("::1")->isLoopback());
    CHECK(parseIp("::ffff:127.0.0.1")->isLoopback());
    CHECK_FALSE(parseIp("128.0.0.1")->isLoopback());
    CHECK_FALSE(parseIp("::2")->isLoopback());

    for (const char* lan : {"10.0.0.1", "172.16.0.1", "172.31.255.255", "192.168.1.1", "169.254.10.1", "fc00::1",
                            "fd12:3456::1", "fe80::1", "febf::1", "::ffff:192.168.1.1"}) {
        INFO(lan);
        CHECK(parseIp(lan)->isPrivate());
    }
    for (const char* wan : {"172.15.255.255", "172.32.0.1", "8.8.8.8", "192.169.0.1", "11.0.0.1", "2001:db8::1",
                            "fec0::1", "::ffff:8.8.8.8"}) {
        INFO(wan);
        CHECK_FALSE(parseIp(wan)->isPrivate());
    }
}

TEST_CASE("connections are mapped to their process names", "[network]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, "192.168.0.10", 50000, "142.250.76.110", 443), tcp(999, "192.168.0.10", 50001, "8.8.8.8", 53)},
        kNames);

    REQUIRE(view.processes.size() == 2);
    CHECK(view.processes[0].process == "chrome.exe");
    CHECK(view.processes[1].process == "pid 999");
    CHECK(view.processes[1].connections[0].process == "pid 999");
}

TEST_CASE("listening, loopback and inactive sockets are skipped and counted once", "[network]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, "0.0.0.0", 80, "0.0.0.0", 0, TcpState::Listen),          // listening
         tcp(100, "::", 80, "::", 0, TcpState::Listen),                    // listening (IPv6)
         tcp(100, "10.0.0.5", 1, "0.0.0.0", 0, TcpState::SynSent),         // unspecified remote, not LISTEN
         tcp(100, "127.0.0.1", 50000, "127.0.0.1", 9000),                  // loopback
         tcp(100, "::1", 50001, "::1", 9000),                              // loopback (IPv6)
         tcp(100, "::ffff:127.0.0.1", 50002, "::ffff:127.0.0.1", 9000),    // loopback (mapped)
         tcp(0, "192.168.0.10", 50003, "8.8.8.8", 443, TcpState::TimeWait),  // inactive
         tcp(100, "192.168.0.10", 50004, "8.8.8.8", 443, TcpState::Closed),  // inactive
         tcp(100, "192.168.0.10", 50005, "8.8.8.8", 443, TcpState::DeleteTcb),  // inactive
         tcp(100, "192.168.0.10", 50006, "8.8.4.4", 443)},                 // real
        kNames);

    CHECK(view.summary.listening == 3);
    CHECK(view.summary.loopback == 3);
    CHECK(view.summary.inactive == 3);
    CHECK(view.summary.connections == 1);
    CHECK(view.summary.established == 1);
    CHECK(view.summary.endpoints == 1);
    REQUIRE(view.endpoints.size() == 1);
    CHECK(view.endpoints[0].ip == "8.8.4.4");
}

TEST_CASE("a loopback connection in an inactive state counts as loopback", "[network]") {
    // 판정 순서: 루프백이 비활성보다 먼저다. 한 항목은 한 규칙으로만 센다.
    const NetworkView view =
        aggregateNetwork({tcp(100, "127.0.0.1", 1, "127.0.0.1", 2, TcpState::TimeWait)}, kNames);

    CHECK(view.summary.loopback == 1);
    CHECK(view.summary.inactive == 0);
}

TEST_CASE("only established connections count as established, other active states still connect", "[network]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, "10.0.0.1", 1, "1.1.1.1", 443), tcp(100, "10.0.0.1", 2, "1.1.1.1", 443, TcpState::SynSent),
         tcp(100, "10.0.0.1", 3, "1.1.1.1", 443, TcpState::CloseWait)},
        kNames);

    CHECK(view.summary.connections == 3);
    CHECK(view.summary.established == 1);
}

TEST_CASE("remote endpoints are grouped by IP with ports and processes", "[network]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, "192.168.0.10", 50000, "142.250.76.110", 443), tcp(100, "192.168.0.10", 50001, "142.250.76.110", 443),
         tcp(200, "192.168.0.10", 50002, "142.250.76.110", 80), tcp(300, "192.168.0.10", 50003, "192.168.0.1", 53),
         tcp(100, "192.168.0.10", 50004, "192.168.0.1", 443)},
        kNames);

    REQUIRE(view.endpoints.size() == 2);
    const RemoteEndpoint& google = view.endpoints[0];
    CHECK(google.ip == "142.250.76.110");
    CHECK_FALSE(google.is_private);
    CHECK(google.connection_count == 3);
    CHECK(google.ports == std::vector<uint16_t>{80, 443});
    CHECK(google.processes == std::vector<std::string>{"chrome.exe", "git.exe"});

    const RemoteEndpoint& router = view.endpoints[1];
    CHECK(router.ip == "192.168.0.1");
    CHECK(router.is_private);
    CHECK(router.connection_count == 2);
    CHECK(router.ports == std::vector<uint16_t>{53, 443});
    CHECK(router.processes == std::vector<std::string>{"chrome.exe", "svchost.exe"});
}

TEST_CASE("IPv6 remotes group and classify like IPv4", "[network]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, "2001:db8::10", 50000, "2001:4860:4860::8888", 443), tcp(100, "fe80::1", 50001, "fe80::2", 443)}, kNames);

    REQUIRE(view.endpoints.size() == 2);
    const auto lan = std::find_if(view.endpoints.begin(), view.endpoints.end(),
                                  [](const RemoteEndpoint& e) { return e.ip == "fe80::2"; });
    REQUIRE(lan != view.endpoints.end());
    CHECK(lan->is_private);
}

TEST_CASE("UDP sockets are counted per process and never become endpoints", "[network]") {
    const NetworkView view = aggregateNetwork(
        {udp(100, "0.0.0.0", 5353), udp(100, "::", 5353), udp(200, "192.168.0.10", 6000),
         tcp(100, "192.168.0.10", 50000, "1.1.1.1", 443)},
        kNames);

    CHECK(view.summary.udp_sockets == 3);
    CHECK(view.endpoints.size() == 1);
    const auto chrome = std::find_if(view.processes.begin(), view.processes.end(),
                                     [](const ProcessNetwork& p) { return p.pid == 100; });
    REQUIRE(chrome != view.processes.end());
    CHECK(chrome->udp_sockets == 2);
    const auto git = std::find_if(view.processes.begin(), view.processes.end(),
                                  [](const ProcessNetwork& p) { return p.pid == 200; });
    REQUIRE(git != view.processes.end());
    CHECK(git->connections.empty());
    CHECK(git->udp_sockets == 1);
}

TEST_CASE("an unparseable remote IP is treated as external, not dropped", "[network]") {
    const NetworkView view = aggregateNetwork({tcp(100, "10.0.0.1", 1, "not-an-ip", 443)}, kNames);

    CHECK(view.summary.connections == 1);
    REQUIRE(view.endpoints.size() == 1);
    CHECK_FALSE(view.endpoints[0].is_private);
}

TEST_CASE("ordering is by connection count then name, and independent of input order", "[network]") {
    std::vector<RawConnection> input = {
        tcp(200, "10.0.0.1", 1, "9.9.9.9", 443),   tcp(100, "10.0.0.1", 2, "9.9.9.9", 443),
        tcp(100, "10.0.0.1", 3, "8.8.8.8", 443),   tcp(300, "10.0.0.1", 4, "7.7.7.7", 443),
        tcp(300, "10.0.0.1", 5, "7.7.7.7", 80),    tcp(300, "10.0.0.1", 6, "6.6.6.6", 443),
        udp(100, "0.0.0.0", 1),                    tcp(100, "127.0.0.1", 7, "127.0.0.1", 8)};

    const NetworkView first = aggregateNetwork(input, kNames);
    REQUIRE(first.processes.size() == 3);
    CHECK(first.processes[0].process == "svchost.exe");  // 3 connections
    CHECK(first.processes[1].process == "chrome.exe");   // 2
    CHECK(first.processes[2].process == "git.exe");      // 1
    CHECK(first.endpoints[0].ip == "7.7.7.7");           // 2 connections, ties go to the lower ip
    CHECK(first.endpoints[1].ip == "9.9.9.9");           // 2 connections
    CHECK(first.endpoints[2].ip == "6.6.6.6");           // 1 connection
    CHECK(first.endpoints[3].ip == "8.8.8.8");           // 1 connection
}

TEST_CASE("the same connections in any order give the same view", "[network]") {
    std::vector<RawConnection> input = {
        tcp(200, "10.0.0.1", 1, "9.9.9.9", 443), tcp(100, "10.0.0.1", 2, "9.9.9.9", 443),
        tcp(100, "10.0.0.1", 3, "8.8.8.8", 443), tcp(300, "10.0.0.1", 4, "7.7.7.7", 443),
        udp(100, "0.0.0.0", 1),                  tcp(300, "10.0.0.1", 5, "7.7.7.7", 80)};
    const NetworkView reference = aggregateNetwork(input, kNames);

    std::mt19937 rng(12345);
    for (int round = 0; round < 20; ++round) {
        std::shuffle(input.begin(), input.end(), rng);
        const NetworkView shuffled = aggregateNetwork(input, kNames);
        REQUIRE(shuffled.processes.size() == reference.processes.size());
        for (size_t i = 0; i < reference.processes.size(); ++i) {
            CHECK(shuffled.processes[i].pid == reference.processes[i].pid);
            REQUIRE(shuffled.processes[i].connections.size() == reference.processes[i].connections.size());
            for (size_t j = 0; j < reference.processes[i].connections.size(); ++j) {
                CHECK(shuffled.processes[i].connections[j].local_port == reference.processes[i].connections[j].local_port);
            }
        }
        REQUIRE(shuffled.endpoints.size() == reference.endpoints.size());
        for (size_t i = 0; i < reference.endpoints.size(); ++i) {
            CHECK(shuffled.endpoints[i].ip == reference.endpoints[i].ip);
        }
    }
}

TEST_CASE("connections with the same remote and local port but different local IPs keep a stable order", "[network]") {
    RawConnection a = tcp(100, "10.0.0.1", 5000, "1.1.1.1", 443);
    RawConnection b = tcp(100, "10.0.0.2", 5000, "1.1.1.1", 443);
    const NetworkView forward = aggregateNetwork({a, b}, kNames);
    const NetworkView backward = aggregateNetwork({b, a}, kNames);

    REQUIRE(forward.processes[0].connections.size() == 2);
    CHECK(forward.processes[0].connections[0].local_ip == "10.0.0.1");
    CHECK(backward.processes[0].connections[0].local_ip == "10.0.0.1");
}

TEST_CASE("an empty scan gives an empty view", "[network]") {
    const NetworkView view = aggregateNetwork({}, kNames);

    CHECK(view.processes.empty());
    CHECK(view.endpoints.empty());
    CHECK(view.summary.connections == 0);
    CHECK(view.summary.endpoints == 0);
}
