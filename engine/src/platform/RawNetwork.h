#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace pulse {

// M10 스펙 2절. OS 가 알려 주는 네트워크 연결 한 줄. 필터링과 묶음은 core/NetworkAggregator 가 한다.
enum class NetProtocol { Tcp, Udp };

enum class TcpState {
    None,  // UDP
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
    DeleteTcb,
};

struct RawConnection {
    uint32_t pid = 0;
    NetProtocol protocol = NetProtocol::Tcp;
    std::string local_ip;  // 사람이 읽는 문자열: "192.168.0.10", "fe80::1"
    uint16_t local_port = 0;
    std::string remote_ip;     // UDP 는 빈 문자열
    uint16_t remote_port = 0;  // UDP 는 0
    TcpState state = TcpState::None;
};

struct ConnectionScan {
    std::vector<RawConnection> connections;
    // 일부 표를 읽지 못했을 때의 이유. 읽은 만큼은 connections 에 있다. 비어 있으면 모두 읽었다.
    std::string error;
};

// OS 접근의 경계. core/ 는 이 뒤의 구현을 모른다.
class IConnectionScanner {
public:
    virtual ~IConnectionScanner() = default;
    virtual ConnectionScan scan() = 0;
};


// ---- M11 스펙 2절: 연결(플로우)별 트래픽 ----

// 16 바이트 주소. IPv4 는 ::ffff:a.b.c.d 형태로 둔다 (core/IpAddress 와 같다).
using IpBytes = std::array<uint8_t, 16>;

// 방향 없는 플로우 키. 두 끝점은 (ip, port) 사전순으로 정렬해 둔다 — core/FlowKey 의 makeFlowKey 가 만든다.
struct FlowKey {
    NetProtocol protocol = NetProtocol::Tcp;
    uint32_t pid = 0;
    IpBytes ip_a{};
    uint16_t port_a = 0;
    IpBytes ip_b{};
    uint16_t port_b = 0;
};

inline bool operator==(const FlowKey& l, const FlowKey& r) {
    return std::tie(l.protocol, l.pid, l.ip_a, l.port_a, l.ip_b, l.port_b) ==
           std::tie(r.protocol, r.pid, r.ip_a, r.port_a, r.ip_b, r.port_b);
}

inline bool operator<(const FlowKey& l, const FlowKey& r) {
    return std::tie(l.protocol, l.pid, l.ip_a, l.port_a, l.ip_b, l.port_b) <
           std::tie(r.protocol, r.pid, r.ip_a, r.port_a, r.ip_b, r.port_b);
}

struct RawFlowTraffic {
    FlowKey key;
    uint64_t bytes_sent = 0;      // 이 프로세스가 보낸 바이트 (창 동안)
    uint64_t bytes_received = 0;  // 이 프로세스가 받은 바이트 (창 동안)
};

struct RawNetworkTraffic {
    double window_seconds = 0.0;  // drain 호출 사이의 단조 시계 간격
    std::vector<RawFlowTraffic> flows;
};

// 트래픽 수집원. 실제 구현은 관리자 권한이 필요한 ETW 수집기다.
class INetworkTrafficSource {
public:
    virtual ~INetworkTrafficSource() = default;
    // 마지막 drain 이후의 창을 닫아 돌려준다. 아직 창이 없으면 nullopt.
    virtual std::optional<RawNetworkTraffic> drain() = 0;
};

}  // namespace pulse
