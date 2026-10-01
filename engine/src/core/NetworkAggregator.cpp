#include "core/NetworkAggregator.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

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
};

}  // namespace

NetworkView aggregateNetwork(const std::vector<RawConnection>& connections,
                             const std::unordered_map<uint32_t, std::string>& names) {
    NetworkView view;
    std::map<uint32_t, ProcessNetwork> by_pid;
    std::map<std::string, EndpointBuilder> endpoints;

    for (const RawConnection& raw : connections) {
        if (raw.protocol == NetProtocol::Udp) {
            ProcessNetwork& process = by_pid[raw.pid];
            process.udp_sockets += 1;
            view.summary.udp_sockets += 1;
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
        connection.pid = raw.pid;
        connection.process = nameFor(raw.pid, names);
        connection.local_ip = raw.local_ip;
        connection.local_port = raw.local_port;
        connection.remote_ip = raw.remote_ip;
        connection.remote_port = raw.remote_port;
        connection.state = raw.state;

        ProcessNetwork& process = by_pid[raw.pid];
        process.connections.push_back(connection);

        EndpointBuilder& endpoint = endpoints[raw.remote_ip];
        endpoint.is_private = remote.has_value() && remote->isPrivate();
        endpoint.ports.insert(raw.remote_port);
        endpoint.connection_count += 1;
        endpoint.processes.insert(connection.process);

        view.summary.connections += 1;
        if (raw.state == TcpState::Established) {
            view.summary.established += 1;
        }
    }

    for (auto& [pid, process] : by_pid) {
        process.pid = pid;
        process.process = nameFor(pid, names);
        std::sort(process.connections.begin(), process.connections.end(),
                  [](const ConnectionView& a, const ConnectionView& b) {
                      return std::tie(a.remote_ip, a.remote_port, a.local_port, a.local_ip) <
                             std::tie(b.remote_ip, b.remote_port, b.local_port, b.local_ip);
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
