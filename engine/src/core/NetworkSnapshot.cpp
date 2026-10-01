#include "core/NetworkSnapshot.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

namespace pulse {
namespace {

const char* stateName(TcpState state) {
    switch (state) {
        case TcpState::None: return "none";
        case TcpState::Closed: return "closed";
        case TcpState::Listen: return "listen";
        case TcpState::SynSent: return "syn_sent";
        case TcpState::SynReceived: return "syn_received";
        case TcpState::Established: return "established";
        case TcpState::FinWait1: return "fin_wait1";
        case TcpState::FinWait2: return "fin_wait2";
        case TcpState::CloseWait: return "close_wait";
        case TcpState::Closing: return "closing";
        case TcpState::LastAck: return "last_ack";
        case TcpState::TimeWait: return "time_wait";
        case TcpState::DeleteTcb: return "delete_tcb";
    }
    return "none";
}

double rateSum(const NetworkConnectionOut& c) {
    return c.down_bps.value_or(0.0) + c.up_bps.value_or(0.0);
}

struct EndpointBuilder {
    bool is_private = false;
    std::set<uint16_t> ports;
    uint32_t connections = 0;
    std::set<std::string> groups;
    bool measured = false;
    double down = 0.0;
    double up = 0.0;
};

}  // namespace

NetworkSnapshot buildNetworkSnapshot(const NetworkView& view, const std::vector<ProcessGroup>& groups,
                                     bool traffic_capable, const NetworkLimits& limits) {
    NetworkSnapshot out;
    out.traffic = traffic_capable ? "measured" : "unavailable";

    const NetworkSummary& s = view.summary;
    out.summary.connections = s.connections;
    out.summary.established = s.established;
    out.summary.endpoints = s.endpoints;
    out.summary.udp_sockets = s.udp_sockets;
    if (s.traffic_measured) {
        out.summary.down_bps = s.down_bps;
        out.summary.up_bps = s.up_bps;
    }

    std::unordered_map<uint32_t, const std::string*> group_by_pid;
    for (const ProcessGroup& group : groups) {
        group_by_pid.emplace(group.root_pid, &group.key);
        for (const ChildProcess& child : group.children) {
            group_by_pid.emplace(child.pid, &group.key);
        }
    }

    // 그룹에 속한 연결만 모은다. 끝점 합계도 이 연결들로 다시 계산한다.
    std::vector<NetworkConnectionOut> connections;
    std::map<std::string, EndpointBuilder> endpoints;
    for (const ProcessNetwork& process : view.processes) {
        const auto group = group_by_pid.find(process.pid);
        if (group == group_by_pid.end()) {
            continue;
        }
        for (const ConnectionView& c : process.connections) {
            NetworkConnectionOut connection;
            connection.group = *group->second;
            connection.pid = c.pid;
            connection.proto = c.protocol == NetProtocol::Udp ? "udp" : "tcp";
            connection.local_port = c.local_port;
            connection.remote = c.remote_ip;
            connection.remote_port = c.remote_port;
            connection.state = stateName(c.state);
            connection.down_bps = c.down_bps;
            connection.up_bps = c.up_bps;

            EndpointBuilder& endpoint = endpoints[c.remote_ip];
            endpoint.ports.insert(c.remote_port);
            endpoint.connections += 1;
            endpoint.groups.insert(connection.group);
            if (c.down_bps.has_value() && c.up_bps.has_value()) {
                endpoint.measured = true;
                endpoint.down += *c.down_bps;
                endpoint.up += *c.up_bps;
            }
            connections.push_back(std::move(connection));
        }
    }
    // 사설망 여부는 집계기가 이미 판정했다 (파싱을 다시 하지 않는다).
    for (const RemoteEndpoint& e : view.endpoints) {
        const auto it = endpoints.find(e.ip);
        if (it != endpoints.end()) {
            it->second.is_private = e.is_private;
        }
    }

    std::vector<NetworkEndpointOut> kept;
    kept.reserve(endpoints.size());
    for (auto& [ip, builder] : endpoints) {
        NetworkEndpointOut endpoint;
        endpoint.ip = ip;
        endpoint.is_private = builder.is_private;
        endpoint.ports.assign(builder.ports.begin(), builder.ports.end());
        endpoint.connections = builder.connections;
        endpoint.groups.assign(builder.groups.begin(), builder.groups.end());
        if (builder.measured) {
            endpoint.down_bps = builder.down;
            endpoint.up_bps = builder.up;
        }
        kept.push_back(std::move(endpoint));
    }
    std::sort(kept.begin(), kept.end(), [](const NetworkEndpointOut& a, const NetworkEndpointOut& b) {
        if (a.connections != b.connections) {
            return a.connections > b.connections;
        }
        return a.ip < b.ip;
    });
    if (kept.size() > limits.max_endpoints) {
        kept.resize(limits.max_endpoints);
    }
    std::set<std::string> kept_ips;
    for (const NetworkEndpointOut& endpoint : kept) {
        kept_ips.insert(endpoint.ip);
    }
    std::erase_if(connections, [&](const NetworkConnectionOut& c) { return kept_ips.count(c.remote) == 0; });

    const auto deterministic = [](const NetworkConnectionOut& a, const NetworkConnectionOut& b) {
        return std::tie(a.group, a.remote, a.remote_port, a.local_port, a.proto, a.pid) <
               std::tie(b.group, b.remote, b.remote_port, b.local_port, b.proto, b.pid);
    };
    if (connections.size() > limits.max_connections) {
        // 속도가 큰 순으로 남긴다. 같으면 결정적 순서.
        std::sort(connections.begin(), connections.end(),
                  [&](const NetworkConnectionOut& a, const NetworkConnectionOut& b) {
                      const double ra = rateSum(a);
                      const double rb = rateSum(b);
                      if (ra != rb) {
                          return ra > rb;
                      }
                      return deterministic(a, b);
                  });
        connections.resize(limits.max_connections);
    }
    std::sort(connections.begin(), connections.end(), deterministic);

    out.endpoints = std::move(kept);
    out.connections = std::move(connections);
    return out;
}

}  // namespace pulse
