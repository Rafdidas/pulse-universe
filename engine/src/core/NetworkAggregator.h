#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/RawNetwork.h"

namespace pulse {

// M10 스펙 4절. 연결 목록을 프로세스별 연결과 원격 끝점으로 묶는다. 순수 로직이다.

struct ConnectionView {
    NetProtocol protocol = NetProtocol::Tcp;  // UDP 는 ETW 트래픽에서 원격이 확인된 플로우다 (M11 스펙 4절)
    uint32_t pid = 0;
    std::string process;  // 이름. 모르면 "pid <n>"
    std::string local_ip;
    uint16_t local_port = 0;
    std::string remote_ip;
    uint16_t remote_port = 0;
    TcpState state = TcpState::Established;  // UDP 는 None
    // 이 연결의 마지막 창 평균 속도 (바이트/초). 트래픽을 측정하지 못했으면 nullopt, 측정했는데
    // 플로우가 없으면 0.
    std::optional<double> down_bps;
    std::optional<double> up_bps;
};

struct ProcessNetwork {
    uint32_t pid = 0;
    std::string process;
    std::vector<ConnectionView> connections;  // 정렬: remote_ip, remote_port, local_port
    uint32_t udp_sockets = 0;                 // 이 프로세스의 UDP 로컬 소켓 수
    std::optional<double> down_bps;           // 연결별 속도의 합. 측정하지 못했으면 nullopt
    std::optional<double> up_bps;
};

struct RemoteEndpoint {
    std::string ip;
    bool is_private = false;              // 사설망·링크 로컬
    std::vector<uint16_t> ports;          // 오름차순, 중복 없음
    uint32_t connection_count = 0;
    std::vector<std::string> processes;   // 이름, 오름차순, 중복 없음
    std::optional<double> down_bps;       // 이 끝점으로 가는 연결·플로우 속도의 합
    std::optional<double> up_bps;
};

struct NetworkSummary {
    uint32_t connections = 0;  // 집계에 들어간 TCP 연결 수 + UDP 플로우 수
    uint32_t established = 0;  // 그중 ESTABLISHED
    uint32_t endpoints = 0;
    uint32_t listening = 0;    // 제외: LISTEN·미지정 원격
    uint32_t loopback = 0;     // 제외: 루프백
    uint32_t inactive = 0;     // 제외: CLOSED·TIME_WAIT·DELETE_TCB
    uint32_t udp_sockets = 0;
    bool traffic_measured = false;  // 트래픽 창이 있었는가
    double down_bps = 0.0;          // traffic_measured 일 때 전체 합
    double up_bps = 0.0;
};

struct NetworkView {
    std::vector<ProcessNetwork> processes;  // 연결이 있거나 UDP 소켓이 있는 프로세스. 정렬: 연결 수 내림차순, 이름, pid
    std::vector<RemoteEndpoint> endpoints;  // 정렬: connection_count 내림차순, ip
    NetworkSummary summary;
};

// names: pid -> 프로세스 이름. 없는 pid 는 "pid <n>" 으로 표시한다.
// traffic: ETW 로 측정한 마지막 창 (M11). 없으면 속도 칸이 모두 nullopt 이고 M10 과 같은 결과다.
NetworkView aggregateNetwork(const std::vector<RawConnection>& connections,
                             const std::unordered_map<uint32_t, std::string>& names,
                             const std::optional<RawNetworkTraffic>& traffic = std::nullopt);

}  // namespace pulse
