#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/RawNetwork.h"

namespace pulse {

// M10 스펙 4절. 연결 목록을 프로세스별 연결과 원격 끝점으로 묶는다. 순수 로직이다.

struct ConnectionView {
    uint32_t pid = 0;
    std::string process;  // 이름. 모르면 "pid <n>"
    std::string local_ip;
    uint16_t local_port = 0;
    std::string remote_ip;
    uint16_t remote_port = 0;
    TcpState state = TcpState::Established;
};

struct ProcessNetwork {
    uint32_t pid = 0;
    std::string process;
    std::vector<ConnectionView> connections;  // 정렬: remote_ip, remote_port, local_port
    uint32_t udp_sockets = 0;                 // 이 프로세스의 UDP 로컬 소켓 수
};

struct RemoteEndpoint {
    std::string ip;
    bool is_private = false;              // 사설망·링크 로컬
    std::vector<uint16_t> ports;          // 오름차순, 중복 없음
    uint32_t connection_count = 0;
    std::vector<std::string> processes;   // 이름, 오름차순, 중복 없음
};

struct NetworkSummary {
    uint32_t connections = 0;  // 집계에 들어간 TCP 연결 수
    uint32_t established = 0;  // 그중 ESTABLISHED
    uint32_t endpoints = 0;
    uint32_t listening = 0;    // 제외: LISTEN·미지정 원격
    uint32_t loopback = 0;     // 제외: 루프백
    uint32_t inactive = 0;     // 제외: CLOSED·TIME_WAIT·DELETE_TCB
    uint32_t udp_sockets = 0;
};

struct NetworkView {
    std::vector<ProcessNetwork> processes;  // 연결이 있거나 UDP 소켓이 있는 프로세스. 정렬: 연결 수 내림차순, 이름, pid
    std::vector<RemoteEndpoint> endpoints;  // 정렬: connection_count 내림차순, ip
    NetworkSummary summary;
};

// names: pid -> 프로세스 이름. 없는 pid 는 "pid <n>" 으로 표시한다.
NetworkView aggregateNetwork(const std::vector<RawConnection>& connections,
                             const std::unordered_map<uint32_t, std::string>& names);

}  // namespace pulse
