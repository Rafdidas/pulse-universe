#pragma once

#include <vector>

#include "core/FlowEstimator.h"
#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

// ETW 스펙 6절. 실측 실행 시간을 흐름으로 바꾼다. weight 는 그룹 구성원이 그 코어에서
// 돈 시간 ÷ 창 길이 — "이 그룹이 코어를 몇 % 썼나" 이다 (1 에서 자른다).
// 걸러내기는 FlowEstimator 와 같다: min_weight 미만은 버리고 그룹당 상위
// max_flows_per_group 개만 남긴다. 결과는 전부 source == "measured" 다.
std::vector<Flow> measuredFlows(const std::vector<ProcessGroup>& groups,
                                const RawThreadMapping& mapping, const FlowConfig& cfg);

}  // namespace pulse
