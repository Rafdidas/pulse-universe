#pragma once

#include <cstddef>
#include <string>

#include "core/NetworkAggregator.h"

namespace pulse {

// M10 스펙 5절. 한 프로세스에서 보여 주는 연결 수와 보여 주는 끝점 수의 상한. 넘으면 "... and N more" 로 줄인다.
constexpr size_t MAX_CONNECTIONS_PER_PROCESS = 8;
constexpr size_t MAX_ENDPOINTS_SHOWN = 24;

// TCP 상태 이름 (ESTABLISHED, SYN_SENT ...). UDP 는 "-".
const char* tcpStateName(TcpState state);

// 네트워크 요약 줄, 프로세스별 연결, 원격 끝점 요약을 콘솔 표로 만든다.
std::string formatNetworkTable(const NetworkView& view);

}  // namespace pulse
