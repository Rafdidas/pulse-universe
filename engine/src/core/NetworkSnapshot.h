#pragma once

#include <cstddef>
#include <vector>

#include "core/NetworkAggregator.h"
#include "core/Snapshot.h"

namespace pulse {

// M12 스펙 3절 (D122). 스냅샷 한 장에 실리는 네트워크 정보의 상한.
struct NetworkLimits {
    std::size_t max_endpoints = 64;
    std::size_t max_connections = 256;
};

// M10·M11 의 집계 결과를 계약의 network 블록으로 바꾼다. 순수 함수다.
//  - 연결은 스냅샷의 그룹(루트 + 자식 PID)으로 그룹 key 에 매핑한다. 그룹에 속하지 않는 PID 의 연결은
//    connections·endpoints 에서 뺀다 (summary 는 모든 프로세스 기준이다).
//  - 끝점은 포함된 연결만으로 다시 집계하고, 연결 수 내림차순(같으면 IP 오름차순) 상위 max_endpoints 개만 둔다.
//  - 연결은 남은 끝점에 속한 것 중 속도(down+up)가 큰 순으로 max_connections 개를 남기고, 결정적 순서로 다시 정렬한다.
//  - traffic_capable 은 트래픽 수집기가 있는지다. 있어도 이번 창이 없으면(첫 스냅샷) 속도는 null 이다.
NetworkSnapshot buildNetworkSnapshot(const NetworkView& view, const std::vector<ProcessGroup>& groups,
                                     bool traffic_capable, const NetworkLimits& limits = {});

}  // namespace pulse
