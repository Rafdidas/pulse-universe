#include "cli/NetworkTableFormatter.h"

#include <algorithm>
#include <cstdio>

namespace pulse {
namespace {

// 이름표에 너무 길게 이어지지 않도록 끝점 한 줄에 보여 주는 프로세스 이름의 수.
constexpr size_t kMaxProcessNames = 4;

std::string endpointText(const std::string& ip, uint16_t port) {
    // IPv6 는 포트와 구분되도록 대괄호로 감싼다.
    if (ip.find(':') != std::string::npos) {
        return "[" + ip + "]:" + std::to_string(port);
    }
    return ip + ":" + std::to_string(port);
}

std::string join(const std::vector<std::string>& items, size_t limit) {
    std::string text;
    for (size_t i = 0; i < items.size() && i < limit; ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += items[i];
    }
    if (items.size() > limit) {
        text += " +" + std::to_string(items.size() - limit);
    }
    return text;
}

std::string portList(const std::vector<uint16_t>& ports) {
    std::string text = "ports ";
    for (size_t i = 0; i < ports.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += std::to_string(ports[i]);
    }
    return text;
}

}  // namespace

const char* tcpStateName(TcpState state) {
    switch (state) {
        case TcpState::None: return "-";
        case TcpState::Closed: return "CLOSED";
        case TcpState::Listen: return "LISTEN";
        case TcpState::SynSent: return "SYN_SENT";
        case TcpState::SynReceived: return "SYN_RCVD";
        case TcpState::Established: return "ESTABLISHED";
        case TcpState::FinWait1: return "FIN_WAIT1";
        case TcpState::FinWait2: return "FIN_WAIT2";
        case TcpState::CloseWait: return "CLOSE_WAIT";
        case TcpState::Closing: return "CLOSING";
        case TcpState::LastAck: return "LAST_ACK";
        case TcpState::TimeWait: return "TIME_WAIT";
        case TcpState::DeleteTcb: return "DELETE_TCB";
    }
    return "-";
}

std::string formatNetworkTable(const NetworkView& view) {
    const NetworkSummary& s = view.summary;
    std::string out;
    char line[512];

    std::snprintf(line, sizeof(line),
                  "Network  connections %u (established %u) | endpoints %u | udp sockets %u | skipped: "
                  "listening %u, loopback %u, inactive %u\n",
                  s.connections, s.established, s.endpoints, s.udp_sockets, s.listening, s.loopback,
                  s.inactive);
    out += line;

    for (const ProcessNetwork& process : view.processes) {
        if (process.connections.empty()) {
            continue;  // UDP 소켓만 있는 프로세스는 요약의 udp sockets 에만 센다.
        }
        std::snprintf(line, sizeof(line), "\n%s (pid %u)  %zu connection%s", process.process.c_str(),
                      process.pid, process.connections.size(), process.connections.size() == 1 ? "" : "s");
        out += line;
        if (process.udp_sockets > 0) {
            out += ", " + std::to_string(process.udp_sockets) + " udp socket" +
                   (process.udp_sockets == 1 ? "" : "s");
        }
        out += "\n";

        const size_t shown = std::min(process.connections.size(), MAX_CONNECTIONS_PER_PROCESS);
        for (size_t i = 0; i < shown; ++i) {
            const ConnectionView& c = process.connections[i];
            const std::string left = endpointText(c.local_ip, c.local_port);
            const std::string right = endpointText(c.remote_ip, c.remote_port);
            std::snprintf(line, sizeof(line), "  TCP  %-24s -> %-24s %s\n", left.c_str(), right.c_str(),
                          tcpStateName(c.state));
            out += line;
        }
        if (process.connections.size() > shown) {
            out += "  ... and " + std::to_string(process.connections.size() - shown) + " more\n";
        }
    }

    if (!view.endpoints.empty()) {
        out += "\nEndpoints\n";
        const size_t shown = std::min(view.endpoints.size(), MAX_ENDPOINTS_SHOWN);
        for (size_t i = 0; i < shown; ++i) {
            const RemoteEndpoint& e = view.endpoints[i];
            const std::string name = e.ip + (e.is_private ? " (lan)" : "");
            std::snprintf(line, sizeof(line), "  %-26s %-18s %3u connection%s  %s\n", name.c_str(),
                          portList(e.ports).c_str(), e.connection_count, e.connection_count == 1 ? " " : "s",
                          join(e.processes, kMaxProcessNames).c_str());
            out += line;
        }
        if (view.endpoints.size() > shown) {
            out += "  ... and " + std::to_string(view.endpoints.size() - shown) + " more endpoints\n";
        }
    }
    return out;
}

}  // namespace pulse
