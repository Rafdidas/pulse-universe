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

// 바이트/초를 "340 B/s", "1.2 KB/s", "12.4 MB/s", "1.0 GB/s" 로 (1024 단위, 소수 한 자리, B/s 는 정수).
std::string formatRate(double bytes_per_second);

// 네트워크 요약 줄, 프로세스별 연결, 원격 끝점 요약을 콘솔 표로 만든다. 트래픽을 측정했으면
// (view.summary.traffic_measured) 속도 칸이 붙고, 측정하지 못했으면 traffic_note 가 있을 때 요약 아래에
// "traffic: not measured (<note>)" 한 줄이 붙는다.
std::string formatNetworkTable(const NetworkView& view, const std::string& traffic_note = {});

}  // namespace pulse
