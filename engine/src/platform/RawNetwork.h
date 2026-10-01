#pragma once

#include <cstdint>
#include <string>
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

}  // namespace pulse
