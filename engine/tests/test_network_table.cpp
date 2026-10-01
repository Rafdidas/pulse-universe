#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "cli/NetworkTableFormatter.h"

using namespace pulse;

namespace {

RawConnection tcp(uint32_t pid, uint16_t local_port, const std::string& remote, uint16_t remote_port) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Tcp;
    c.local_ip = "192.168.0.10";
    c.local_port = local_port;
    c.remote_ip = remote;
    c.remote_port = remote_port;
    c.state = TcpState::Established;
    return c;
}

size_t count(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("the table starts with the summary line", "[networktable]") {
    const NetworkView view = aggregateNetwork({tcp(100, 50000, "142.250.76.110", 443)}, {{100, "chrome.exe"}});

    const std::string text = formatNetworkTable(view);

    CHECK(text.rfind("Network  connections 1 (established 1) | endpoints 1 | udp sockets 0 | skipped: listening 0, "
                     "loopback 0, inactive 0\n", 0) == 0);
}

TEST_CASE("a process block lists its connections and the endpoint summary follows", "[networktable]") {
    const NetworkView view = aggregateNetwork(
        {tcp(100, 50000, "142.250.76.110", 443), tcp(100, 50001, "192.168.0.1", 53)}, {{100, "chrome.exe"}});

    const std::string text = formatNetworkTable(view);

    CHECK(text.find("chrome.exe (pid 100)  2 connections\n") != std::string::npos);
    CHECK(text.find("192.168.0.10:50000") != std::string::npos);
    CHECK(text.find("142.250.76.110:443") != std::string::npos);
    CHECK(text.find("ESTABLISHED") != std::string::npos);
    CHECK(text.find("\nEndpoints\n") != std::string::npos);
    CHECK(text.find("192.168.0.1 (lan)") != std::string::npos);
    CHECK(text.find("ports 443") != std::string::npos);
}

TEST_CASE("a single connection says connection, not connections", "[networktable]") {
    const NetworkView view = aggregateNetwork({tcp(100, 50000, "1.1.1.1", 443)}, {{100, "a.exe"}});

    CHECK(formatNetworkTable(view).find("a.exe (pid 100)  1 connection\n") != std::string::npos);
}

TEST_CASE("connections beyond the per-process limit collapse into one line", "[networktable]") {
    std::vector<RawConnection> input;
    for (uint16_t i = 0; i < MAX_CONNECTIONS_PER_PROCESS + 3; ++i) {
        input.push_back(tcp(100, static_cast<uint16_t>(50000 + i), "1.1.1." + std::to_string(i + 1), 443));
    }
    const std::string text = formatNetworkTable(aggregateNetwork(input, {{100, "a.exe"}}));

    CHECK(count(text, "  TCP  ") == MAX_CONNECTIONS_PER_PROCESS);
    CHECK(text.find("  ... and 3 more\n") != std::string::npos);
}

TEST_CASE("endpoints beyond the display limit collapse into one line", "[networktable]") {
    std::vector<RawConnection> input;
    for (size_t i = 0; i < MAX_ENDPOINTS_SHOWN + 5; ++i) {
        input.push_back(tcp(100, static_cast<uint16_t>(50000 + i), "10.1." + std::to_string(i / 200) + "." + std::to_string(i % 200 + 1), 443));
    }
    const std::string text = formatNetworkTable(aggregateNetwork(input, {{100, "a.exe"}}));

    CHECK(text.find("  ... and 5 more endpoints\n") != std::string::npos);
}

TEST_CASE("a process with only UDP sockets is not listed but UDP is counted", "[networktable]") {
    RawConnection udp;
    udp.pid = 200;
    udp.protocol = NetProtocol::Udp;
    udp.local_ip = "0.0.0.0";
    udp.local_port = 5353;
    const NetworkView view = aggregateNetwork({udp, tcp(100, 50000, "1.1.1.1", 443)}, {{100, "a.exe"}, {200, "mdns.exe"}});

    const std::string text = formatNetworkTable(view);

    CHECK(text.find("udp sockets 1") != std::string::npos);
    CHECK(text.find("mdns.exe") == std::string::npos);
}

TEST_CASE("an IPv6 endpoint is bracketed so the port stays readable", "[networktable]") {
    const NetworkView view = aggregateNetwork({tcp(100, 50000, "2001:4860:4860::8888", 443)}, {{100, "a.exe"}});

    CHECK(formatNetworkTable(view).find("[2001:4860:4860::8888]:443") != std::string::npos);
}

TEST_CASE("an endpoint with very many ports still ends its line and does not run into the next", "[networktable]") {
    std::vector<RawConnection> input;
    for (uint16_t port = 1000; port < 1100; ++port) {
        input.push_back(tcp(100, static_cast<uint16_t>(port + 10000), "9.9.9.9", port));
    }
    input.push_back(tcp(100, 60000, "8.8.8.8", 443));
    const std::string text = formatNetworkTable(aggregateNetwork(input, {{100, "a.exe"}}));

    const size_t endpoints = text.find("\nEndpoints\n");
    REQUIRE(endpoints != std::string::npos);
    const std::string tail = text.substr(endpoints);
    CHECK(count(tail, "\n") == 4);  // Endpoints 앞의 빈 줄, "Endpoints", 끝점 두 줄
    CHECK(tail.find("100 connections") != std::string::npos);
    CHECK(tail.find("8.8.8.8") != std::string::npos);
}

TEST_CASE("an empty view prints only the summary line", "[networktable]") {
    const std::string text = formatNetworkTable(aggregateNetwork({}, {}));

    CHECK(count(text, "\n") == 1);
    CHECK(text.find("Endpoints") == std::string::npos);
}

TEST_CASE("TCP states have readable names", "[networktable]") {
    CHECK(std::string(tcpStateName(TcpState::Established)) == "ESTABLISHED");
    CHECK(std::string(tcpStateName(TcpState::SynSent)) == "SYN_SENT");
    CHECK(std::string(tcpStateName(TcpState::CloseWait)) == "CLOSE_WAIT");
    CHECK(std::string(tcpStateName(TcpState::None)) == "-");
}
