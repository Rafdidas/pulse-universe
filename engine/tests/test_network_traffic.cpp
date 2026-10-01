#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

#include "cli/NetworkTableFormatter.h"
#include "core/FlowKey.h"
#include "core/IpAddress.h"
#include "core/NetworkAggregator.h"
#include "core/NetworkEvents.h"

using namespace pulse;

namespace {

IpBytes v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    IpBytes bytes{};
    bytes[10] = 0xff;
    bytes[11] = 0xff;
    bytes[12] = a;
    bytes[13] = b;
    bytes[14] = c;
    bytes[15] = d;
    return bytes;
}

IpBytes ip(const char* text) {
    return parseIp(text)->bytes;
}

void putU32(std::vector<unsigned char>& out, uint32_t value) {
    unsigned char raw[4];
    std::memcpy(raw, &value, 4);
    out.insert(out.end(), raw, raw + 4);
}

void putPort(std::vector<unsigned char>& out, uint16_t port) {  // 네트워크 바이트 순서
    out.push_back(static_cast<unsigned char>(port >> 8));
    out.push_back(static_cast<unsigned char>(port & 0xff));
}

// IPv4 이벤트 페이로드: PID, size, daddr, saddr, dport, sport (+ 뒤쪽 필드 8 바이트)
std::vector<unsigned char> payload4(uint32_t pid, uint32_t size, const IpBytes& daddr, const IpBytes& saddr,
                                    uint16_t dport, uint16_t sport) {
    std::vector<unsigned char> out;
    putU32(out, pid);
    putU32(out, size);
    out.insert(out.end(), daddr.begin() + 12, daddr.end());
    out.insert(out.end(), saddr.begin() + 12, saddr.end());
    putPort(out, dport);
    putPort(out, sport);
    putU32(out, 0);
    putU32(out, 0);
    return out;
}

std::vector<unsigned char> payload6(uint32_t pid, uint32_t size, const IpBytes& daddr, const IpBytes& saddr,
                                    uint16_t dport, uint16_t sport) {
    std::vector<unsigned char> out;
    putU32(out, pid);
    putU32(out, size);
    out.insert(out.end(), daddr.begin(), daddr.end());
    out.insert(out.end(), saddr.begin(), saddr.end());
    putPort(out, dport);
    putPort(out, sport);
    putU32(out, 0);
    return out;
}

RawConnection tcp(uint32_t pid, const std::string& local, uint16_t local_port, const std::string& remote,
                  uint16_t remote_port) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Tcp;
    c.local_ip = local;
    c.local_port = local_port;
    c.remote_ip = remote;
    c.remote_port = remote_port;
    c.state = TcpState::Established;
    return c;
}

RawConnection udpSocket(uint32_t pid, uint16_t port) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Udp;
    c.local_ip = "0.0.0.0";
    c.local_port = port;
    return c;
}

RawFlowTraffic flow(NetProtocol protocol, uint32_t pid, const IpBytes& a, uint16_t pa, const IpBytes& b, uint16_t pb,
                    uint64_t sent, uint64_t received) {
    RawFlowTraffic f;
    f.key = makeFlowKey(protocol, pid, a, pa, b, pb);
    f.bytes_sent = sent;
    f.bytes_received = received;
    return f;
}

const std::unordered_map<uint32_t, std::string> kNames = {{100, "chrome.exe"}, {200, "git.exe"}};

}  // namespace

// ---------------------------------------------------------------- makeFlowKey

TEST_CASE("a flow key does not depend on the order of its endpoints", "[flow]") {
    const FlowKey a = makeFlowKey(NetProtocol::Tcp, 7, v4(10, 0, 0, 1), 5000, v4(8, 8, 8, 8), 443);
    const FlowKey b = makeFlowKey(NetProtocol::Tcp, 7, v4(8, 8, 8, 8), 443, v4(10, 0, 0, 1), 5000);

    CHECK(a == b);
    CHECK_FALSE(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("flow keys differ by protocol, pid, address and port", "[flow]") {
    const FlowKey base = makeFlowKey(NetProtocol::Tcp, 7, v4(10, 0, 0, 1), 5000, v4(8, 8, 8, 8), 443);

    CHECK_FALSE(base == makeFlowKey(NetProtocol::Udp, 7, v4(10, 0, 0, 1), 5000, v4(8, 8, 8, 8), 443));
    CHECK_FALSE(base == makeFlowKey(NetProtocol::Tcp, 8, v4(10, 0, 0, 1), 5000, v4(8, 8, 8, 8), 443));
    CHECK_FALSE(base == makeFlowKey(NetProtocol::Tcp, 7, v4(10, 0, 0, 2), 5000, v4(8, 8, 8, 8), 443));
    CHECK_FALSE(base == makeFlowKey(NetProtocol::Tcp, 7, v4(10, 0, 0, 1), 5001, v4(8, 8, 8, 8), 443));
    CHECK_FALSE(base == makeFlowKey(NetProtocol::Tcp, 7, v4(10, 0, 0, 1), 5000, v4(8, 8, 8, 8), 444));
}

TEST_CASE("endpoints with the same address are ordered by port", "[flow]") {
    const FlowKey a = makeFlowKey(NetProtocol::Udp, 1, v4(1, 1, 1, 1), 9000, v4(1, 1, 1, 1), 53);

    CHECK(a.port_a == 53);
    CHECK(a.port_b == 9000);
    CHECK(a == makeFlowKey(NetProtocol::Udp, 1, v4(1, 1, 1, 1), 53, v4(1, 1, 1, 1), 9000));
}

// -------------------------------------------------------------- parseNetworkEvent

TEST_CASE("TCP IPv4 send and receive events are parsed", "[netevent]") {
    const auto data = payload4(1234, 4096, v4(142, 250, 76, 110), v4(192, 168, 0, 10), 443, 50000);

    const auto send = parseNetworkEvent(10, data.data(), data.size());
    REQUIRE(send.has_value());
    CHECK(send->protocol == NetProtocol::Tcp);
    CHECK_FALSE(send->v6);
    CHECK(send->sent);
    CHECK(send->pid == 1234);
    CHECK(send->size == 4096);
    CHECK(send->daddr == v4(142, 250, 76, 110));
    CHECK(send->saddr == v4(192, 168, 0, 10));
    CHECK(send->dport == 443);
    CHECK(send->sport == 50000);

    const auto receive = parseNetworkEvent(11, data.data(), data.size());
    REQUIRE(receive.has_value());
    CHECK_FALSE(receive->sent);
}

TEST_CASE("UDP IPv4 events are parsed", "[netevent]") {
    const auto data = payload4(77, 512, v4(8, 8, 4, 4), v4(192, 168, 0, 10), 53, 61000);

    const auto send = parseNetworkEvent(42, data.data(), data.size());
    const auto receive = parseNetworkEvent(43, data.data(), data.size());
    REQUIRE(send.has_value());
    REQUIRE(receive.has_value());
    CHECK(send->protocol == NetProtocol::Udp);
    CHECK(send->sent);
    CHECK_FALSE(receive->sent);
    CHECK(send->dport == 53);
    CHECK(send->sport == 61000);
}

TEST_CASE("IPv6 events keep the 16 address bytes", "[netevent]") {
    const IpBytes remote = ip("2001:4860:4860::8888");
    const IpBytes local = ip("2001:db8::10");
    const auto data = payload6(9, 100, remote, local, 443, 40000);

    for (const uint16_t id : std::initializer_list<uint16_t>{26, 27, 58, 59}) {
        const auto event = parseNetworkEvent(id, data.data(), data.size());
        REQUIRE(event.has_value());
        CHECK(event->v6);
        CHECK(event->daddr == remote);
        CHECK(event->saddr == local);
        CHECK(event->dport == 443);
        CHECK(event->sport == 40000);
    }
    CHECK(parseNetworkEvent(26, data.data(), data.size())->protocol == NetProtocol::Tcp);
    CHECK(parseNetworkEvent(58, data.data(), data.size())->protocol == NetProtocol::Udp);
    CHECK(parseNetworkEvent(26, data.data(), data.size())->sent);
    CHECK_FALSE(parseNetworkEvent(27, data.data(), data.size())->sent);
}

TEST_CASE("short payloads, unknown ids and null data are ignored", "[netevent]") {
    const auto v4data = payload4(1, 1, v4(1, 1, 1, 1), v4(2, 2, 2, 2), 1, 2);
    const auto v6data = payload6(1, 1, ip("::1"), ip("::2"), 1, 2);

    // 꼭 필요한 길이는 IPv4 20 바이트, IPv6 44 바이트다.
    for (const uint16_t id : std::initializer_list<uint16_t>{10, 11, 42, 43}) {
        CHECK(parseNetworkEvent(id, v4data.data(), 20).has_value());
        CHECK_FALSE(parseNetworkEvent(id, v4data.data(), 19).has_value());
        CHECK_FALSE(parseNetworkEvent(id, v4data.data(), 0).has_value());
    }
    for (const uint16_t id : std::initializer_list<uint16_t>{26, 27, 58, 59}) {
        CHECK(parseNetworkEvent(id, v6data.data(), 44).has_value());
        CHECK_FALSE(parseNetworkEvent(id, v6data.data(), 43).has_value());
    }
    for (const uint16_t id : std::initializer_list<uint16_t>{0, 1, 9, 12, 13, 25, 28, 41, 44, 57, 60, 65535}) {
        CHECK_FALSE(parseNetworkEvent(id, v4data.data(), v4data.size()).has_value());
    }
    CHECK_FALSE(parseNetworkEvent(10, nullptr, 100).has_value());
}

// -------------------------------------------------------------------- formatIp

TEST_CASE("formatIp writes IPv4 dotted and IPv6 per RFC 5952", "[ip]") {
    CHECK(formatIp(makeIpAddress(v4(192, 168, 0, 10), false)) == "192.168.0.10");
    CHECK(formatIp(*parseIp("::1")) == "::1");
    CHECK(formatIp(*parseIp("::")) == "::");
    CHECK(formatIp(*parseIp("fe80::1")) == "fe80::1");
    CHECK(formatIp(*parseIp("2001:0db8:0000:0000:0000:0000:0000:0001")) == "2001:db8::1");
    CHECK(formatIp(*parseIp("1:2:3:4:5:6:7:8")) == "1:2:3:4:5:6:7:8");
    CHECK(formatIp(*parseIp("1:0:3:4:5:6:7:8")) == "1:0:3:4:5:6:7:8");  // 길이 1 구간은 줄이지 않는다
    CHECK(formatIp(*parseIp("2001:db8:0:0:1:0:0:1")) == "2001:db8::1:0:0:1");  // 같으면 앞쪽
    CHECK(formatIp(*parseIp("2001:DB8::ABCD")) == "2001:db8::abcd");
    CHECK(formatIp(*parseIp("::ffff:1.2.3.4")) == "::ffff:1.2.3.4");
    CHECK(formatIp(*parseIp("1::")) == "1::");
}

TEST_CASE("formatIp output parses back to the same address", "[ip]") {
    for (const char* text : {"10.1.2.3", "::1", "fe80::2", "2001:db8::1:0:0:1", "1:2:3:4:5:6:7:8", "::ffff:8.8.8.8"}) {
        INFO(text);
        const IpAddress parsed = *parseIp(text);
        const IpAddress again = *parseIp(formatIp(parsed));
        CHECK(again.bytes == parsed.bytes);
    }
}

// ------------------------------------------------------------ aggregator traffic

TEST_CASE("TCP connections get their rates from the matching flow", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 2.0;
    // 키는 방향 없이 정렬되므로 어느 쪽 주소를 먼저 써도 같다.
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(142, 250, 76, 110), 443, v4(192, 168, 0, 10), 50000, 2000, 8000));

    const NetworkView view = aggregateNetwork(
        {tcp(100, "192.168.0.10", 50000, "142.250.76.110", 443), tcp(100, "192.168.0.10", 50001, "1.1.1.1", 443)}, kNames,
        traffic);

    REQUIRE(view.processes.size() == 1);
    const ProcessNetwork& chrome = view.processes[0];
    const ConnectionView& busy = chrome.connections[1];  // 정렬: 1.1.1.1 이 먼저, 142.250... 이 둘째
    REQUIRE(busy.remote_ip == "142.250.76.110");
    CHECK(busy.down_bps == 4000.0);
    CHECK(busy.up_bps == 1000.0);
    const ConnectionView& idle = chrome.connections[0];
    CHECK(idle.down_bps == 0.0);
    CHECK(idle.up_bps == 0.0);

    CHECK(chrome.down_bps == 4000.0);
    CHECK(chrome.up_bps == 1000.0);
    CHECK(view.summary.traffic_measured);
    CHECK(view.summary.down_bps == 4000.0);
    CHECK(view.summary.up_bps == 1000.0);
    const auto google = std::find_if(view.endpoints.begin(), view.endpoints.end(),
                                     [](const RemoteEndpoint& e) { return e.ip == "142.250.76.110"; });
    REQUIRE(google != view.endpoints.end());
    CHECK(google->down_bps == 4000.0);
}

TEST_CASE("without a traffic window every rate is unknown", "[network][traffic]") {
    const NetworkView view = aggregateNetwork({tcp(100, "192.168.0.10", 50000, "1.1.1.1", 443)}, kNames);

    CHECK_FALSE(view.summary.traffic_measured);
    CHECK_FALSE(view.processes[0].connections[0].down_bps.has_value());
    CHECK_FALSE(view.processes[0].down_bps.has_value());
    CHECK_FALSE(view.endpoints[0].down_bps.has_value());
}

TEST_CASE("rates add up per process and per endpoint without double counting", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(192, 168, 0, 10), 1, v4(9, 9, 9, 9), 443, 100, 1000));
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(192, 168, 0, 10), 2, v4(9, 9, 9, 9), 443, 50, 500));
    traffic.flows.push_back(flow(NetProtocol::Tcp, 200, v4(192, 168, 0, 10), 3, v4(9, 9, 9, 9), 80, 10, 100));

    const NetworkView view = aggregateNetwork({tcp(100, "192.168.0.10", 1, "9.9.9.9", 443),
                                               tcp(100, "192.168.0.10", 2, "9.9.9.9", 443),
                                               tcp(200, "192.168.0.10", 3, "9.9.9.9", 80)},
                                              kNames, traffic);

    REQUIRE(view.endpoints.size() == 1);
    CHECK(view.endpoints[0].down_bps == 1600.0);
    CHECK(view.endpoints[0].up_bps == 160.0);
    CHECK(view.summary.down_bps == 1600.0);
    CHECK(view.summary.up_bps == 160.0);
    const auto chrome = std::find_if(view.processes.begin(), view.processes.end(),
                                     [](const ProcessNetwork& p) { return p.pid == 100; });
    REQUIRE(chrome != view.processes.end());
    CHECK(chrome->down_bps == 1500.0);
}

TEST_CASE("a flow that is not in the connection list is ignored", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(192, 168, 0, 10), 9, v4(5, 5, 5, 5), 443, 999, 999));

    const NetworkView view = aggregateNetwork({tcp(100, "192.168.0.10", 1, "1.1.1.1", 443)}, kNames, traffic);

    CHECK(view.summary.down_bps == 0.0);
    CHECK(view.endpoints.size() == 1);
}

TEST_CASE("a zero-length window gives zero rates, not infinity", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 0.0;
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(192, 168, 0, 10), 1, v4(1, 1, 1, 1), 443, 100, 100));

    const NetworkView view = aggregateNetwork({tcp(100, "192.168.0.10", 1, "1.1.1.1", 443)}, kNames, traffic);

    CHECK(view.processes[0].connections[0].down_bps == 0.0);
}

TEST_CASE("a UDP flow of a known socket becomes a connection with its remote endpoint", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Udp, 100, v4(192, 168, 0, 10), 5353, v4(8, 8, 4, 4), 53, 300, 700));

    const NetworkView view = aggregateNetwork({udpSocket(100, 5353)}, kNames, traffic);

    CHECK(view.summary.udp_sockets == 1);
    CHECK(view.summary.connections == 1);
    CHECK(view.summary.established == 0);
    REQUIRE(view.processes.size() == 1);
    REQUIRE(view.processes[0].connections.size() == 1);
    const ConnectionView& c = view.processes[0].connections[0];
    CHECK(c.protocol == NetProtocol::Udp);
    CHECK(c.state == TcpState::None);
    CHECK(c.local_port == 5353);
    CHECK(c.remote_ip == "8.8.4.4");
    CHECK(c.remote_port == 53);
    CHECK(c.down_bps == 700.0);
    CHECK(c.up_bps == 300.0);
    REQUIRE(view.endpoints.size() == 1);
    CHECK(view.endpoints[0].ip == "8.8.4.4");
}

TEST_CASE("UDP flows with loopback remotes or unknown sockets are ignored", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Udp, 100, v4(127, 0, 0, 1), 6000, v4(127, 0, 0, 1), 7000, 10, 10));
    traffic.flows.push_back(flow(NetProtocol::Udp, 100, v4(192, 168, 0, 10), 5353, v4(8, 8, 4, 4), 53, 10, 10));  // 소켓 없음
    traffic.flows.push_back(flow(NetProtocol::Udp, 300, v4(192, 168, 0, 10), 5353, v4(8, 8, 4, 4), 53, 10, 10));  // pid 없음

    const NetworkView view = aggregateNetwork({udpSocket(100, 6000)}, kNames, traffic);

    CHECK(view.summary.connections == 0);
    CHECK(view.endpoints.empty());
}

TEST_CASE("IPv6 UDP flows are written in the IPv6 text form", "[network][traffic]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Udp, 100, ip("2001:db8::10"), 5353, ip("2001:4860:4860::8888"), 53, 1, 1));

    const NetworkView view = aggregateNetwork({udpSocket(100, 5353)}, kNames, traffic);

    REQUIRE(view.endpoints.size() == 1);
    CHECK(view.endpoints[0].ip == "2001:4860:4860::8888");
}

// ---------------------------------------------------------------- table output

TEST_CASE("rates are written in B/s, KB/s, MB/s and GB/s", "[networktable]") {
    CHECK(formatRate(0.0) == "0 B/s");
    CHECK(formatRate(-5.0) == "0 B/s");
    CHECK(formatRate(1023.0) == "1023 B/s");
    CHECK(formatRate(1024.0) == "1.0 KB/s");
    CHECK(formatRate(1536.0) == "1.5 KB/s");
    CHECK(formatRate(1024.0 * 1024.0) == "1.0 MB/s");
    CHECK(formatRate(12.4 * 1024.0 * 1024.0) == "12.4 MB/s");
    CHECK(formatRate(1024.0 * 1024.0 * 1024.0) == "1.0 GB/s");
}

TEST_CASE("a measured table shows rates and a not-measured table says why", "[networktable]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Tcp, 100, v4(192, 168, 0, 10), 50000, v4(1, 1, 1, 1), 443, 2048, 4096));
    const std::vector<RawConnection> connections = {tcp(100, "192.168.0.10", 50000, "1.1.1.1", 443)};

    const std::string measured = formatNetworkTable(aggregateNetwork(connections, kNames, traffic));
    CHECK(measured.find("| down 4.0 KB/s, up 2.0 KB/s") != std::string::npos);
    CHECK(measured.find("down 4.0 KB/s  up 2.0 KB/s") != std::string::npos);
    CHECK(measured.find("traffic: not measured") == std::string::npos);

    const std::string unmeasured =
        formatNetworkTable(aggregateNetwork(connections, kNames), "ETW network events need administrator rights");
    CHECK(unmeasured.find("traffic: not measured (ETW network events need administrator rights)\n") != std::string::npos);
    CHECK(unmeasured.find("down ") == std::string::npos);
}

TEST_CASE("UDP flow lines say UDP", "[networktable]") {
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    traffic.flows.push_back(flow(NetProtocol::Udp, 100, v4(192, 168, 0, 10), 5353, v4(8, 8, 4, 4), 53, 10, 20));

    const std::string text = formatNetworkTable(aggregateNetwork({udpSocket(100, 5353)}, kNames, traffic));

    CHECK(text.find("  UDP  192.168.0.10:5353") != std::string::npos);
    CHECK(text.find("8.8.4.4:53") != std::string::npos);
}
