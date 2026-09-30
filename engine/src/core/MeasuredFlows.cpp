#include "core/MeasuredFlows.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace pulse {

std::vector<Flow> measuredFlows(const std::vector<ProcessGroup>& groups,
                                const RawThreadMapping& mapping, const FlowConfig& cfg) {
    std::vector<Flow> flows;
    if (mapping.window_seconds <= 0.0) {
        return flows;
    }

    // pid -> 그룹 순번. 화면에 남은 그룹의 구성원만 담긴다.
    std::unordered_map<uint32_t, std::size_t> group_by_pid;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        group_by_pid.emplace(groups[i].root_pid, i);
        for (const ChildProcess& child : groups[i].children) {
            group_by_pid.emplace(child.pid, i);
        }
    }

    // 그룹마다 코어별 실행 시간을 더한다. 코어 순서를 고정하려고 map 을 쓴다.
    std::vector<std::map<uint32_t, double>> seconds_by_core(groups.size());
    for (const RawRunTime& run : mapping.run_times) {
        const auto it = group_by_pid.find(run.pid);
        if (it != group_by_pid.end()) {
            seconds_by_core[it->second][run.core] += run.seconds;
        }
    }

    std::vector<Flow> per_group;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        per_group.clear();
        for (const auto& [core, seconds] : seconds_by_core[i]) {
            const double weight = std::min(1.0, seconds / mapping.window_seconds);
            if (weight < cfg.min_weight) {
                continue;
            }
            Flow f;
            f.group = groups[i].key;
            f.core = core;
            f.weight = weight;
            f.source = "measured";
            per_group.push_back(std::move(f));
        }

        std::stable_sort(per_group.begin(), per_group.end(),
                         [](const Flow& a, const Flow& b) { return a.weight > b.weight; });
        if (per_group.size() > cfg.max_flows_per_group) {
            per_group.resize(cfg.max_flows_per_group);
        }
        flows.insert(flows.end(), per_group.begin(), per_group.end());
    }

    return flows;
}

}  // namespace pulse
