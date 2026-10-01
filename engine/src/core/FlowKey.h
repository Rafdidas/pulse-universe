#pragma once

#include <cstdint>

#include "platform/RawNetwork.h"

namespace pulse {

// M11 스펙 2절. 두 끝점을 (주소, 포트) 사전순으로 정렬한 방향 없는 키를 만든다. 어느 쪽이 로컬이고
// 어느 쪽이 원격인지 따지지 않으므로, 송신·수신 이벤트의 주소 순서가 달라도 같은 키가 나온다.
FlowKey makeFlowKey(NetProtocol protocol, uint32_t pid, const IpBytes& ip1, uint16_t port1, const IpBytes& ip2,
                    uint16_t port2);

}  // namespace pulse
