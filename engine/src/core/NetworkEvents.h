#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "platform/RawNetwork.h"

namespace pulse {

// M11 스펙 5절. Microsoft-Windows-Kernel-Network 의 데이터 전송 이벤트 하나를 푼 것. OS 헤더를 쓰지 않는
// 순수 파서라 일반 권한 테스트로 확인한다.
struct NetworkEvent {
    NetProtocol protocol = NetProtocol::Tcp;
    bool v6 = false;
    bool sent = false;  // 송신이면 참, 수신이면 거짓 (이 프로세스 기준)
    uint32_t pid = 0;
    uint32_t size = 0;  // 바이트
    IpBytes daddr{};
    uint16_t dport = 0;
    IpBytes saddr{};
    uint16_t sport = 0;
};

// 이벤트 ID: TCP 송신/수신 IPv4 10/11, IPv6 26/27; UDP 송신/수신 IPv4 42/43, IPv6 58/59.
// 알 수 없는 ID 이거나 페이로드가 짧으면 nullopt. 포트와 IPv4 주소는 네트워크 바이트 순서로 읽는다.
std::optional<NetworkEvent> parseNetworkEvent(uint16_t event_id, const unsigned char* data, size_t length);

}  // namespace pulse
