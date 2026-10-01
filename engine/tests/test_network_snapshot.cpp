#include <catch2/catch_test_macros.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/EngineLoop.h"
#include "core/FlowKey.h"
#include "core/IpAddress.h"
#include "core/NetworkSnapshot.h"
#include "fakes/FakeNetwork.h"
#include "fakes/FakeSystemReader.h"
#include "network/Serializer.h"

using namespace pulse;
namespace json = boost::json;

namespace {

ProcessGroup group(const std::string& key, uint32_t root, std::vector<uint32_t> children = {}) {
    ProcessGroup g;
    g.key = key;
    g.root_pid = root;
    for (const uint32_t pid : children) {
        ChildProcess child;
        child.pid = pid;
        g.children.push_back(child);
    }
    return g;
}

RawConnection tcp(uint32_t pid, uint16_t local_port, const std::string& remote, uint16_t remote_port,
                  TcpState state = TcpState::Established) {
    RawConnection c;
    c.pid = pid;
    c.protocol = NetProtocol::Tcp;
    c.local_ip = "192.168.0.10";
    c.local_port = local_port;
    c.remote_ip = remote;
    c.remote_port = remote_port;
    c.state = state;
    return c;
}

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

const std::unordered_map<uint32_t, std::string> kNames = {{100, "chrome.exe"}, {101, "chrome.exe"}, {200, "git.exe"}, {300, "svchost.exe"}};

NetworkView viewOf(const std::vector<RawConnection>& connections, const std::optional<RawNetworkTraffic>& traffic = std::nullopt) {
    return aggregateNetwork(connections, kNames, traffic);
}

}  // namespace

TEST_CASE("connections are attached to the group of their process, root or child", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100, {101}), group("git.exe:200", 200)};
    const NetworkView view = viewOf({tcp(100, 1, "1.1.1.1", 443), tcp(101, 2, "1.1.1.1", 443), tcp(200, 3, "2.2.2.2", 22)});

    const NetworkSnapshot snapshot = buildNetworkSnapshot(view, groups, false);

    REQUIRE(snapshot.connections.size() == 3);
    CHECK(snapshot.connections[0].group == "chrome.exe:100");
    CHECK(snapshot.connections[1].group == "chrome.exe:100");
    CHECK(snapshot.connections[1].pid == 101);
    CHECK(snapshot.connections[2].group == "git.exe:200");
    REQUIRE(snapshot.endpoints.size() == 2);
    CHECK(snapshot.endpoints[0].ip == "1.1.1.1");
    CHECK(snapshot.endpoints[0].connections == 2);
    CHECK(snapshot.endpoints[0].groups == std::vector<std::string>{"chrome.exe:100"});
}

TEST_CASE("connections of processes outside every group are left out but still counted in the summary", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    const NetworkView view = viewOf({tcp(100, 1, "1.1.1.1", 443), tcp(300, 2, "3.3.3.3", 443), tcp(300, 3, "4.4.4.4", 443)});

    const NetworkSnapshot snapshot = buildNetworkSnapshot(view, groups, false);

    CHECK(snapshot.summary.connections == 3);
    CHECK(snapshot.summary.endpoints == 3);
    REQUIRE(snapshot.endpoints.size() == 1);
    CHECK(snapshot.endpoints[0].ip == "1.1.1.1");
    CHECK(snapshot.connections.size() == 1);
}

TEST_CASE("an endpoint is rebuilt from the included connections only", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    RawFlowTraffic a;
    a.key = makeFlowKey(NetProtocol::Tcp, 100, parseIp("192.168.0.10")->bytes, 1, v4(9, 9, 9, 9), 443);
    a.bytes_sent = 100;
    a.bytes_received = 1000;
    RawFlowTraffic b;
    b.key = makeFlowKey(NetProtocol::Tcp, 300, parseIp("192.168.0.10")->bytes, 2, v4(9, 9, 9, 9), 80);
    b.bytes_sent = 5000;
    b.bytes_received = 50000;
    traffic.flows = {a, b};
    const NetworkView view = viewOf({tcp(100, 1, "9.9.9.9", 443), tcp(300, 2, "9.9.9.9", 80)}, traffic);

    const NetworkSnapshot snapshot = buildNetworkSnapshot(view, groups, true);

    REQUIRE(snapshot.endpoints.size() == 1);
    const NetworkEndpointOut& e = snapshot.endpoints[0];
    CHECK(e.connections == 1);
    CHECK(e.ports == std::vector<uint16_t>{443});
    CHECK(e.down_bps == 1000.0);  // 숨은 프로세스(300)의 50000 은 들어가지 않는다
    CHECK(e.up_bps == 100.0);
    CHECK(snapshot.summary.down_bps == 51000.0);  // 요약은 모든 프로세스 기준이다
    CHECK(snapshot.summary.up_bps == 5100.0);
    CHECK(snapshot.traffic == "measured");
}

TEST_CASE("without a traffic window every rate is null even when a collector exists", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    const NetworkView view = viewOf({tcp(100, 1, "1.1.1.1", 443)});

    const NetworkSnapshot snapshot = buildNetworkSnapshot(view, groups, true);

    CHECK(snapshot.traffic == "measured");
    CHECK_FALSE(snapshot.summary.down_bps.has_value());
    CHECK_FALSE(snapshot.endpoints[0].down_bps.has_value());
    CHECK_FALSE(snapshot.connections[0].down_bps.has_value());

    CHECK(buildNetworkSnapshot(view, groups, false).traffic == "unavailable");
}

TEST_CASE("endpoints are limited by connection count and the rest of the connections follow", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    std::vector<RawConnection> connections;
    // 1.1.1.1 은 연결 3 개, 나머지는 1 개씩.
    connections.push_back(tcp(100, 1, "1.1.1.1", 443));
    connections.push_back(tcp(100, 2, "1.1.1.1", 80));
    connections.push_back(tcp(100, 3, "1.1.1.1", 8080));
    for (int i = 0; i < 5; ++i) {
        connections.push_back(tcp(100, static_cast<uint16_t>(10 + i), "5.5.5." + std::to_string(i + 1), 443));
    }
    NetworkLimits limits;
    limits.max_endpoints = 3;

    const NetworkSnapshot snapshot = buildNetworkSnapshot(viewOf(connections), groups, false, limits);

    REQUIRE(snapshot.endpoints.size() == 3);
    CHECK(snapshot.endpoints[0].ip == "1.1.1.1");
    CHECK(snapshot.endpoints[1].ip == "5.5.5.1");  // 같은 연결 수는 IP 오름차순
    CHECK(snapshot.endpoints[2].ip == "5.5.5.2");
    CHECK(snapshot.summary.endpoints == 6);  // 잘리기 전의 총수
    CHECK(snapshot.connections.size() == 5);
    for (const NetworkConnectionOut& c : snapshot.connections) {
        CHECK((c.remote == "1.1.1.1" || c.remote == "5.5.5.1" || c.remote == "5.5.5.2"));
    }
}

TEST_CASE("the connection limit keeps the busiest connections in a deterministic order", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    std::vector<RawConnection> connections;
    for (uint16_t i = 0; i < 6; ++i) {
        connections.push_back(tcp(100, static_cast<uint16_t>(1000 + i), "7.7.7.7", static_cast<uint16_t>(2000 + i)));
        RawFlowTraffic flow;
        flow.key = makeFlowKey(NetProtocol::Tcp, 100, parseIp("192.168.0.10")->bytes, static_cast<uint16_t>(1000 + i), v4(7, 7, 7, 7),
                               static_cast<uint16_t>(2000 + i));
        flow.bytes_received = (i == 1 || i == 4) ? 9000 : 10;  // 1 과 4 가 바쁘다
        traffic.flows.push_back(flow);
    }
    NetworkLimits limits;
    limits.max_connections = 2;

    const NetworkSnapshot snapshot = buildNetworkSnapshot(viewOf(connections, traffic), groups, true, limits);

    REQUIRE(snapshot.connections.size() == 2);
    CHECK(snapshot.connections[0].remote_port == 2001);  // 결정적 순서(remote_port 오름차순)로 다시 정렬
    CHECK(snapshot.connections[1].remote_port == 2004);
}

TEST_CASE("UDP connections are reported with proto udp and state none", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    RawConnection socket;
    socket.pid = 100;
    socket.protocol = NetProtocol::Udp;
    socket.local_ip = "192.168.0.10";
    socket.local_port = 5353;
    RawNetworkTraffic traffic;
    traffic.window_seconds = 1.0;
    RawFlowTraffic flow;
    flow.key = makeFlowKey(NetProtocol::Udp, 100, parseIp("192.168.0.10")->bytes, 5353, v4(8, 8, 4, 4), 53);
    flow.bytes_sent = 40;
    flow.bytes_received = 80;
    traffic.flows = {flow};

    const NetworkSnapshot snapshot = buildNetworkSnapshot(viewOf({socket}, traffic), groups, true);

    REQUIRE(snapshot.connections.size() == 1);
    CHECK(snapshot.connections[0].proto == "udp");
    CHECK(snapshot.connections[0].state == "none");
    CHECK(snapshot.connections[0].remote == "8.8.4.4");
    CHECK(snapshot.connections[0].down_bps == 80.0);
}

TEST_CASE("an empty network view gives an empty block", "[netsnapshot]") {
    const NetworkSnapshot snapshot = buildNetworkSnapshot(viewOf({}), {}, false);

    CHECK(snapshot.traffic == "unavailable");
    CHECK(snapshot.endpoints.empty());
    CHECK(snapshot.connections.empty());
    CHECK(snapshot.summary.connections == 0);
}

TEST_CASE("the same connections in any order give the same snapshot", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100, {101}), group("git.exe:200", 200)};
    std::vector<RawConnection> input = {tcp(100, 1, "1.1.1.1", 443), tcp(101, 2, "2.2.2.2", 443), tcp(200, 3, "1.1.1.1", 22),
                                        tcp(100, 4, "3.3.3.3", 80),  tcp(300, 5, "4.4.4.4", 80)};
    const NetworkSnapshot reference = buildNetworkSnapshot(viewOf(input), groups, false);

    std::mt19937 rng(7);
    for (int round = 0; round < 20; ++round) {
        std::shuffle(input.begin(), input.end(), rng);
        const NetworkSnapshot shuffled = buildNetworkSnapshot(viewOf(input), groups, false);
        REQUIRE(shuffled.connections.size() == reference.connections.size());
        for (size_t i = 0; i < reference.connections.size(); ++i) {
            CHECK(shuffled.connections[i].local_port == reference.connections[i].local_port);
        }
        REQUIRE(shuffled.endpoints.size() == reference.endpoints.size());
        for (size_t i = 0; i < reference.endpoints.size(); ++i) {
            CHECK(shuffled.endpoints[i].ip == reference.endpoints[i].ip);
        }
    }
}

// ------------------------------------------------------------------ serializer

TEST_CASE("the snapshot carries the network block with integer rates and nulls", "[serialize][netsnapshot]") {
    SystemSnapshot snapshot;
    snapshot.network.traffic = "measured";
    snapshot.network.summary.connections = 5;
    snapshot.network.summary.established = 4;
    snapshot.network.summary.endpoints = 2;
    snapshot.network.summary.udp_sockets = 7;
    snapshot.network.summary.down_bps = 1234.6;
    NetworkEndpointOut endpoint;
    endpoint.ip = "142.250.76.110";
    endpoint.is_private = false;
    endpoint.ports = {80, 443};
    endpoint.connections = 3;
    endpoint.groups = {"chrome.exe:100"};
    endpoint.down_bps = 12.4;
    endpoint.up_bps = 0.0;
    snapshot.network.endpoints.push_back(endpoint);
    NetworkConnectionOut connection;
    connection.group = "chrome.exe:100";
    connection.pid = 100;
    connection.proto = "tcp";
    connection.local_port = 50000;
    connection.remote = "142.250.76.110";
    connection.remote_port = 443;
    connection.state = "established";
    snapshot.network.connections.push_back(connection);

    const json::value parsed = json::parse(serializeSnapshot(snapshot));
    const json::object& network = parsed.at("network").as_object();

    CHECK(network.at("traffic").as_string() == "measured");
    const json::object& summary = network.at("summary").as_object();
    CHECK(summary.at("connections").to_number<int>() == 5);
    CHECK(summary.at("established").to_number<int>() == 4);
    CHECK(summary.at("endpoints").to_number<int>() == 2);
    CHECK(summary.at("udp_sockets").to_number<int>() == 7);
    CHECK(summary.at("down_bps").is_int64());
    CHECK(summary.at("down_bps").as_int64() == 1235);
    CHECK(summary.at("up_bps").is_null());

    const json::object& e = network.at("endpoints").as_array().at(0).as_object();
    CHECK(e.at("ip").as_string() == "142.250.76.110");
    CHECK(e.at("private").as_bool() == false);
    CHECK(e.at("ports").as_array().size() == 2);
    CHECK(e.at("ports").as_array().at(1).to_number<int>() == 443);
    CHECK(e.at("connections").to_number<int>() == 3);
    CHECK(e.at("groups").as_array().at(0).as_string() == "chrome.exe:100");
    CHECK(e.at("down_bps").as_int64() == 12);
    CHECK(e.at("up_bps").as_int64() == 0);  // 측정된 0 은 null 이 아니다

    const json::object& c = network.at("connections").as_array().at(0).as_object();
    CHECK(c.at("group").as_string() == "chrome.exe:100");
    CHECK(c.at("pid").to_number<int>() == 100);
    CHECK(c.at("proto").as_string() == "tcp");
    CHECK(c.at("local_port").to_number<int>() == 50000);
    CHECK(c.at("remote").as_string() == "142.250.76.110");
    CHECK(c.at("remote_port").to_number<int>() == 443);
    CHECK(c.at("state").as_string() == "established");
    CHECK(c.at("down_bps").is_null());
    CHECK(c.at("up_bps").is_null());
}

TEST_CASE("a default snapshot still has an unavailable network block", "[serialize][netsnapshot]") {
    const json::value parsed = json::parse(serializeSnapshot(SystemSnapshot{}));
    const json::object& network = parsed.at("network").as_object();

    CHECK(network.at("traffic").as_string() == "unavailable");
    CHECK(network.at("endpoints").as_array().empty());
    CHECK(network.at("connections").as_array().empty());
    CHECK(network.at("summary").as_object().at("down_bps").is_null());
}

TEST_CASE("hello carries the network traffic capability", "[serialize][netsnapshot]") {
    HelloInfo info;
    info.network_traffic = "measured";

    const json::value parsed = json::parse(serializeHello(info));

    CHECK(parsed.at("capabilities").as_object().at("network_traffic").as_string() == "measured");
    CHECK(parsed.at("capabilities").as_object().at("thread_mapping").as_string() == "estimated");
    CHECK(json::parse(serializeHello(HelloInfo{})).at("capabilities").as_object().at("network_traffic").as_string() == "unavailable");
}

// ------------------------------------------------------------------ engine loop

namespace {

RawSample sampleWithProcesses(uint64_t timestamp_ms) {
    RawSample s;
    s.timestamp_ms = timestamp_ms;
    s.cores = {RawCore{0, 25.0}};
    s.memory = RawMemory{1024ull * 1024 * 1024, 4096ull * 1024 * 1024};
    for (const uint32_t pid : {100u, 200u}) {
        RawProcess p;
        p.pid = pid;
        p.ppid = 4;
        p.name = pid == 100 ? "app.exe" : "tool.exe";
        p.cpu_cumulative_ms = timestamp_ms / 10;
        p.mem_bytes = 64ull * 1024 * 1024;
        p.thread_count = 4;
        p.start_time_ms = 1000;
        p.account = Account::User;
        s.processes.push_back(p);
    }
    return s;
}

EngineLoopConfig quickConfig(unsigned iterations) {
    EngineLoopConfig cfg;
    cfg.interval_ms = 1;
    cfg.iterations = iterations;
    return cfg;
}

}  // namespace

TEST_CASE("without network sources the snapshot has an unavailable empty network block", "[loop][netsnapshot]") {
    FakeSystemReader reader({sampleWithProcesses(1000)}, 4);
    SystemSnapshot last;

    EngineLoop loop(reader, quickConfig(1), [&](const SystemSnapshot& s) { last = s; });
    loop.run();

    CHECK(last.network.traffic == "unavailable");
    CHECK(last.network.connections.empty());
    CHECK(last.network.endpoints.empty());
}

TEST_CASE("the loop fills the network block from the scanner and the traffic windows", "[loop][netsnapshot]") {
    FakeSystemReader reader({sampleWithProcesses(1000), sampleWithProcesses(2000)}, 4);
    FakeConnectionScanner scanner({tcp(100, 50000, "142.250.76.110", 443), tcp(999, 50001, "8.8.8.8", 53)});
    RawNetworkTraffic window;
    window.window_seconds = 2.0;
    RawFlowTraffic flow;
    flow.key = makeFlowKey(NetProtocol::Tcp, 100, parseIp("192.168.0.10")->bytes, 50000, v4(142, 250, 76, 110), 443);
    flow.bytes_sent = 200;
    flow.bytes_received = 4000;
    window.flows = {flow};
    FakeTrafficSource traffic({window});
    std::vector<SystemSnapshot> snapshots;

    EngineLoop loop(reader, quickConfig(2), [&](const SystemSnapshot& s) { snapshots.push_back(s); },
                    NetworkSources{&scanner, &traffic});
    loop.run();

    REQUIRE(snapshots.size() == 2);
    REQUIRE(loop.error().empty());
    CHECK(scanner.scanCount() == 2);
    CHECK(traffic.drainCount() == 2);

    // 첫 스냅샷: 수집기는 있지만 창이 없어 속도가 null 이다.
    CHECK(snapshots[0].network.traffic == "measured");
    REQUIRE(snapshots[0].network.connections.size() == 1);
    CHECK(snapshots[0].network.connections[0].group == "app.exe:100");
    CHECK_FALSE(snapshots[0].network.connections[0].down_bps.has_value());
    // 숨은(그룹에 없는) 프로세스 999 의 연결은 요약에만 센다.
    CHECK(snapshots[0].network.summary.connections == 2);

    // 둘째 스냅샷: 창이 있어 속도가 계산된다 (바이트 / 2 초).
    REQUIRE(snapshots[1].network.connections.size() == 1);
    CHECK(snapshots[1].network.connections[0].down_bps == 2000.0);
    CHECK(snapshots[1].network.connections[0].up_bps == 100.0);
}

TEST_CASE("a scanner error does not stop the loop", "[loop][netsnapshot]") {
    FakeSystemReader reader({sampleWithProcesses(1000)}, 4);
    FakeConnectionScanner scanner({tcp(100, 1, "1.1.1.1", 443)}, "GetExtendedTcpTable(AF_INET6) failed with error 5");
    int calls = 0;

    EngineLoop loop(reader, quickConfig(2), [&](const SystemSnapshot&) { ++calls; }, NetworkSources{&scanner, nullptr});
    loop.run();

    CHECK(calls == 2);
    CHECK(loop.error().empty());
}

// ---------------------------------------------------------- review follow-ups

TEST_CASE("one endpoint shared by two groups merges its ports and groups", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100), group("git.exe:200", 200)};
    const NetworkView view = viewOf({tcp(100, 1, "9.9.9.9", 443), tcp(200, 2, "9.9.9.9", 80), tcp(200, 3, "9.9.9.9", 443)});

    const NetworkSnapshot snapshot = buildNetworkSnapshot(view, groups, false);

    REQUIRE(snapshot.endpoints.size() == 1);
    CHECK(snapshot.endpoints[0].connections == 3);
    CHECK(snapshot.endpoints[0].ports == std::vector<uint16_t>{80, 443});
    CHECK(snapshot.endpoints[0].groups == std::vector<std::string>{"chrome.exe:100", "git.exe:200"});
}

TEST_CASE("exactly at the limits nothing is cut, one past the limit cuts one", "[netsnapshot]") {
    const std::vector<ProcessGroup> groups = {group("chrome.exe:100", 100)};
    NetworkLimits limits;
    limits.max_endpoints = 4;
    limits.max_connections = 4;
    std::vector<RawConnection> four;
    for (int i = 0; i < 4; ++i) {
        four.push_back(tcp(100, static_cast<uint16_t>(10 + i), "6.6.6." + std::to_string(i + 1), 443));
    }

    const NetworkSnapshot exact = buildNetworkSnapshot(viewOf(four), groups, false, limits);
    CHECK(exact.endpoints.size() == 4);
    CHECK(exact.connections.size() == 4);

    four.push_back(tcp(100, 99, "6.6.6.5", 443));
    const NetworkSnapshot over = buildNetworkSnapshot(viewOf(four), groups, false, limits);
    CHECK(over.endpoints.size() == 4);
    CHECK(over.connections.size() == 4);
    CHECK(over.summary.endpoints == 5);
}

TEST_CASE("with no rates the connection cap keeps a connection for every endpoint first", "[netsnapshot]") {
    // 그룹 key 가 정렬에서 뒤인 그룹의 연결이 몽땅 잘려 끝점만 남는 일이 없어야 한다.
    const std::vector<ProcessGroup> groups = {group("a.exe:100", 100), group("z.exe:200", 200)};
    std::vector<RawConnection> connections;
    // a.exe 가 같은 끝점 5 개에 연결 3 개씩, z.exe 가 끝점 5 개에 하나씩 (끝점 이름이 겹치지 않는다).
    for (int e = 0; e < 5; ++e) {
        for (int k = 0; k < 3; ++k) {
            connections.push_back(tcp(100, static_cast<uint16_t>(1000 + e * 10 + k), "1.1.1." + std::to_string(e + 1),
                                      static_cast<uint16_t>(2000 + k)));
        }
        connections.push_back(tcp(200, static_cast<uint16_t>(3000 + e), "2.2.2." + std::to_string(e + 1), 443));
    }
    NetworkLimits limits;
    limits.max_connections = 10;  // 끝점이 10 개이므로 끝점마다 하나씩이 정확히 들어간다

    const NetworkSnapshot snapshot = buildNetworkSnapshot(viewOf(connections), groups, false, limits);

    REQUIRE(snapshot.connections.size() == 10);
    std::set<std::string> remotes;
    for (const NetworkConnectionOut& c : snapshot.connections) {
        remotes.insert(c.remote);
    }
    CHECK(remotes.size() == 10);
    for (const NetworkEndpointOut& endpoint : snapshot.endpoints) {
        CHECK(remotes.count(endpoint.ip) == 1);
    }
}

TEST_CASE("a scanner that throws stops the loop with the error, it does not crash", "[loop][netsnapshot]") {
    struct ThrowingScanner final : IConnectionScanner {
        ConnectionScan scan() override { throw std::runtime_error("scanner exploded"); }
    } scanner;
    FakeSystemReader reader({sampleWithProcesses(1000)}, 4);
    int calls = 0;

    EngineLoop loop(reader, quickConfig(3), [&](const SystemSnapshot&) { ++calls; }, NetworkSources{&scanner, nullptr});
    loop.run();

    CHECK(calls == 0);
    CHECK(loop.error() == "scanner exploded");
}
