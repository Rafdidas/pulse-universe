#pragma once

#include <cstddef>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct FlowConfig {
    // 이보다 약한 흐름은 화면을 어지럽히기만 하므로 버린다.
    double min_weight = 0.05;
    size_t max_flows_per_group = 4;
};

// 그룹의 CPU 점유율을 코어별 부하에 비례 배분해 흐름 세기를 만든다.
// 실측이 아니므로 결과는 전부 source == "estimated" 다.
class FlowEstimator {
public:
    explicit FlowEstimator(FlowConfig cfg = {});

    std::vector<Flow> estimate(const std::vector<ProcessGroup>& groups,
                               const std::vector<CoreLoad>& cores) const;

private:
    FlowConfig cfg_;
};

}  // namespace pulse
