#include "core/NetworkAggregator.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

#include "core/FlowKey.h"
#include "core/IpAddress.h"

namespace pulse {
namespace {

std::string nameFor(uint32_t pid, const std::unordered_map<uint32_t, std::string>& names) {
    const auto it = names.find(pid);
    return it != names.end() ? it->second : "pid " + std::to_string(pid);
}

bool isInactive(TcpState state) {
    return state == TcpState::Closed || state == TcpState::TimeWait || state == TcpState::DeleteTcb;
}

struct EndpointBuilder {
    bool is_private = false;
    std::set<uint16_t> ports;
    uint32_t connection_count = 0;
    std::set<std::string> processes;
    double down_bps = 0.0;
    double up_bps = 0.0;
};

struct ProcessBuilder {
    ProcessNetwork view;
    double down_bps = 0.0;
    double up_bps = 0.0;
};

// ::ffff:a.b.c.d 형태인가 (ETW 이벤트의 IPv4 주소는 이렇게 온다).
bool isMappedV4(const IpBytes& b) {
    for (size_t i = 0; i < 10; ++i) {
        if (b[i] != 0) {
            return false;
        }
    }
    return b[10] == 0xff && b[11] == 0xff;
}

// 프로세스의 UDP 소켓 하나. wildcard 는 0.0.0.0 / :: 에 바인드된 소켓이다 (모든 주소에 맞는다).
struct UdpSocket {
    IpBytes ip{};
    bool wildcard = true;
    uint16_t port = 0;
};

bool matchesSocket(const std::vector<UdpSocket>& sockets, const IpBytes& ip, uint16_t port) {
    for (const UdpSocket& socket : sockets) {
        if (socket.port == port && (socket.wildcard || socket.ip == ip)) {
            return true;
        }
    }
    return false;
}

// 창 동안의 바이트를 초당 속도로 바꾼다. 창 길이가 0 이하면 0.
double rateOf(uint64_t bytes, double window_seconds) {
    return window_seconds > 0.0 ? static_cast<double>(bytes) / window_seconds : 0.0;
}

// 연결 하나를 집계에 더한다. 프로세스·끝점 합산과 요약 카운트를 함께 올린다.
void addConnection(ConnectionView connection, const std::optional<IpAddress>& remote, const std::string& remote_ip,
                   uint16_t remote_port, std::map<uint32_t, ProcessBuilder>& by_pid,
                   std::map<std::string, EndpointBuilder>& endpoints, NetworkSummary& summary) {
    ProcessBuilder& process = by_pid[connection.pid];
    EndpointBuilder& endpoint = endpoints[remote_ip];
    endpoint.is_private = remote.has_value() && remote->isPrivate();
    endpoint.ports.insert(remote_port);
    endpoint.connection_count += 1;
    endpoint.processes.insert(connection.process);
    if (connection.down_bps.has_value()) {
        process.down_bps += *connection.down_bps;
        process.up_bps += connection.up_bps.value_or(0.0);
        endpoint.down_bps += *connection.down_bps;
        endpoint.up_bps += connection.up_bps.value_or(0.0);
        summary.down_bps += *connection.down_bps;
        summary.up_bps += connection.up_bps.value_or(0.0);
    }
    summary.connections += 1;
    if (connection.protocol == NetProtocol::Tcp && connection.state == TcpState::Established) {
        summary.established += 1;
    }
    process.view.connections.push_back(std::move(connection));
}

}  // namespace

NetworkView aggregateNetwork(const std::vector<RawConnection>& connections,
                             const std::unordered_map<uint32_t, std::string>& names,
                             const std::optional<RawNetworkTraffic>& traffic) {
    NetworkView view;
    view.summary.traffic_measured = traffic.has_value();
    const double window = traffic.has_value() ? traffic->window_seconds : 0.0;

    std::map<FlowKey, const RawFlowTraffic*> flows;
    if (traffic.has_value()) {
        for (const RawFlowTraffic& flow : traffic->flows) {
            flows[flow.key] = &flow;
        }
    }

    std::map<uint32_t, ProcessBuilder> by_pid;
    std::map<std::string, EndpointBuilder> endpoints;
    // UDP 플로우에서 어느 끝점이 로컬인지 가리려고 프로세스별 UDP 소켓(포트·바인드 주소)과
    // 이 PC 의 로컬 주소 집합을 모은다 (같은 포트끼리 통신하는 NTP·mDNS 도 가려낸다).
    std::map<uint32_t, std::vector<UdpSocket>> udp_sockets;
    std::set<IpBytes> local_ips;
    for (const RawConnection& raw : connections) {
        const std::optional<IpAddress> local = parseIp(raw.local_ip);
        if (local.has_value() && !local->isUnspecified()) {
            local_ips.insert(local->bytes);
        }
    }

    for (const RawConnection& raw : connections) {
        if (raw.protocol == NetProtocol::Udp) {
            by_pid[raw.pid].view.udp_sockets += 1;
            view.summary.udp_sockets += 1;
            const std::optional<IpAddress> bound = parseIp(raw.local_ip);
            UdpSocket socket;
            socket.port = raw.local_port;
            socket.wildcard = !bound.has_value() || bound->isUnspecified();
            if (bound.has_value()) {
                socket.ip = bound->bytes;
            }
            udp_sockets[raw.pid].push_back(socket);
            continue;
        }

        const std::optional<IpAddress> remote = parseIp(raw.remote_ip);
        // 판정 순서는 스펙 4절. 한 항목은 한 규칙으로만 센다.
        if (raw.state == TcpState::Listen || (remote.has_value() && remote->isUnspecified())) {
            view.summary.listening += 1;
            continue;
        }
        if (remote.has_value() && remote->isLoopback()) {
            view.summary.loopback += 1;
            continue;
        }
        if (isInactive(raw.state)) {
            view.summary.inactive += 1;
            continue;
        }

        ConnectionView connection;
        connection.protocol = NetProtocol::Tcp;
        connection.pid = raw.pid;
        connection.process = nameFor(raw.pid, names);
        connection.local_ip = raw.local_ip;
        connection.local_port = raw.local_port;
        connection.remote_ip = raw.remote_ip;
        connection.remote_port = raw.remote_port;
        connection.state = raw.state;

        if (traffic.has_value()) {
            connection.down_bps = 0.0;
            connection.up_bps = 0.0;
            const std::optional<IpAddress> local = parseIp(raw.local_ip);
            if (local.has_value() && remote.has_value()) {
                const FlowKey key = makeFlowKey(NetProtocol::Tcp, raw.pid, local->bytes, raw.local_port, remote->bytes,
                                                raw.remote_port);
                const auto found = flows.find(key);
                if (found != flows.end()) {
                    connection.down_bps = rateOf(found->second->bytes_received, window);
                    connection.up_bps = rateOf(found->second->bytes_sent, window);
                }
            }
        }
        addConnection(std::move(connection), remote, raw.remote_ip, raw.remote_port, by_pid, endpoints, view.summary);
    }

    // UDP 플로우: 연결 목록에는 원격이 없으므로 ETW 이벤트로만 끝점이 보인다.
    for (const auto& [key, flow] : flows) {
        if (key.protocol != NetProtocol::Udp) {
            continue;
        }
        const auto sockets = udp_sockets.find(key.pid);
        if (sockets == udp_sockets.end()) {
            continue;  // 이 프로세스의 UDP 소켓이 목록에 없다 (이미 닫힘)
        }
        // 끝점의 (주소, 포트)가 이 프로세스의 UDP 소켓에 맞는 쪽이 로컬이다. 포트가 같은 쪽이 둘이면
        // (NTP 123 <-> 123 등) 이 PC 의 로컬 주소인 쪽을 고른다. 그래도 가릴 수 없으면 추측하지 않고 건너뛴다.
        const bool a_local = matchesSocket(sockets->second, key.ip_a, key.port_a);
        const bool b_local = matchesSocket(sockets->second, key.ip_b, key.port_b);
        bool local_is_a = a_local;
        if (a_local && b_local) {
            const bool a_known = local_ips.count(key.ip_a) > 0;
            const bool b_known = local_ips.count(key.ip_b) > 0;
            if (a_known == b_known) {
                continue;
            }
            local_is_a = a_known;
        } else if (!a_local && !b_local) {
            continue;
        }
        const IpBytes& remote_bytes = local_is_a ? key.ip_b : key.ip_a;
        const IpBytes& local_bytes = local_is_a ? key.ip_a : key.ip_b;
        const uint16_t remote_port = local_is_a ? key.port_b : key.port_a;
        const uint16_t local_port = local_is_a ? key.port_a : key.port_b;
        // 이벤트의 IPv4 는 mapped 형태로 온다. mapped 는 IPv4 로 보고 나머지는 IPv6 로 쓴다.
        const IpAddress remote = makeIpAddress(remote_bytes, !isMappedV4(remote_bytes));
        const IpAddress local = makeIpAddress(local_bytes, !isMappedV4(local_bytes));
        if (remote.isUnspecified()) {
            continue;
        }
        if (remote.isLoopback()) {
            view.summary.loopback += 1;  // 외부 공간에는 나가는 연결만 둔다 (스펙 D103). TCP 와 같이 센다.
            continue;
        }
        ConnectionView connection;
        connection.protocol = NetProtocol::Udp;
        connection.pid = key.pid;
        connection.process = nameFor(key.pid, names);
        connection.local_ip = formatIp(local);
        connection.local_port = local_port;
        connection.remote_ip = formatIp(remote);
        connection.remote_port = remote_port;
        connection.state = TcpState::None;
        connection.down_bps = rateOf(flow->bytes_received, window);
        connection.up_bps = rateOf(flow->bytes_sent, window);
        const std::string remote_text = connection.remote_ip;
        addConnection(std::move(connection), std::optional<IpAddress>(remote), remote_text, remote_port, by_pid, endpoints,
                      view.summary);
    }

    for (auto& [pid, builder] : by_pid) {
        ProcessNetwork process = std::move(builder.view);
        process.pid = pid;
        process.process = nameFor(pid, names);
        if (traffic.has_value()) {
            process.down_bps = builder.down_bps;
            process.up_bps = builder.up_bps;
        }
        std::sort(process.connections.begin(), process.connections.end(),
                  [](const ConnectionView& a, const ConnectionView& b) {
                      return std::tie(a.remote_ip, a.remote_port, a.local_port, a.local_ip, a.protocol) <
                             std::tie(b.remote_ip, b.remote_port, b.local_port, b.local_ip, b.protocol);
                  });
        view.processes.push_back(std::move(process));
    }
    std::sort(view.processes.begin(), view.processes.end(),
              [](const ProcessNetwork& a, const ProcessNetwork& b) {
                  if (a.connections.size() != b.connections.size()) {
                      return a.connections.size() > b.connections.size();
                  }
                  return std::tie(a.process, a.pid) < std::tie(b.process, b.pid);
              });

    for (auto& [ip, builder] : endpoints) {
        RemoteEndpoint endpoint;
        endpoint.ip = ip;
        endpoint.is_private = builder.is_private;
        endpoint.ports.assign(builder.ports.begin(), builder.ports.end());
        endpoint.connection_count = builder.connection_count;
        endpoint.processes.assign(builder.processes.begin(), builder.processes.end());
        if (traffic.has_value()) {
            endpoint.down_bps = builder.down_bps;
            endpoint.up_bps = builder.up_bps;
        }
        view.endpoints.push_back(std::move(endpoint));
    }
    std::sort(view.endpoints.begin(), view.endpoints.end(),
              [](const RemoteEndpoint& a, const RemoteEndpoint& b) {
                  if (a.connection_count != b.connection_count) {
                      return a.connection_count > b.connection_count;
                  }
                  return a.ip < b.ip;
              });
    view.summary.endpoints = static_cast<uint32_t>(view.endpoints.size());
    return view;
}

}  // namespace pulse
