#pragma once

#include <string>
#include <unordered_set>

#include "core/CpuDelta.h"
#include "core/FlowEstimator.h"
#include "core/GroupBuilder.h"
#include "core/LifecycleTracker.h"
#include "core/ProcessFilter.h"
#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

struct AggregatorConfig {
    FilterConfig filter;
    FlowConfig flow;
};

// 원시 표본 하나를 받아 SystemSnapshot 하나를 만든다.
// 이 클래스만이 core/ 의 나머지 부품들을 안다.
class DataAggregator {
public:
    explicit DataAggregator(unsigned core_count, AggregatorConfig cfg = {});

    SystemSnapshot aggregate(const RawSample& sample);

private:
    CpuDelta cpu_delta_;
    GroupBuilder group_builder_;
    ProcessFilter filter_;
    LifecycleTracker lifecycle_;
    // 실측 흐름(measuredFlows)도 추정과 같은 걸러내기를 쓴다.
    FlowConfig flow_config_;
    FlowEstimator flow_estimator_;
    // 직전 스냅샷에 실린 그룹 key. 다음 선택에서 유지 보너스를 받는다.
    std::unordered_set<std::string> shown_keys_;
    uint64_t seq_ = 0;
};

}  // namespace pulse
