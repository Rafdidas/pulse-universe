#include "core/FlowEstimator.h"

#include <algorithm>

namespace pulse {

FlowEstimator::FlowEstimator(FlowConfig cfg) : cfg_(cfg) {}

std::vector<Flow> FlowEstimator::estimate(const std::vector<ProcessGroup>& groups,
                                          const std::vector<CoreLoad>& cores) const {
    std::vector<Flow> flows;

    double total_core_pct = 0.0;
    for (const CoreLoad& c : cores) {
        total_core_pct += c.pct;
    }
    if (total_core_pct <= 0.0) {
        return flows;
    }

    double max_group_cpu = 0.0;
    for (const ProcessGroup& g : groups) {
        max_group_cpu = std::max(max_group_cpu, g.cpu_pct.value_or(0.0));
    }
    if (max_group_cpu <= 0.0) {
        return flows;
    }

    std::vector<Flow> per_group;
    for (const ProcessGroup& g : groups) {
        if (!g.cpu_pct.has_value() || *g.cpu_pct <= 0.0) {
            continue;
        }
        const double activity = *g.cpu_pct / max_group_cpu;

        per_group.clear();
        for (const CoreLoad& c : cores) {
            const double weight = activity * (c.pct / total_core_pct);
            if (weight < cfg_.min_weight) {
                continue;
            }
            Flow f;
            f.group = g.key;
            f.core = c.id;
            f.weight = weight;
            per_group.push_back(std::move(f));
        }

        std::stable_sort(per_group.begin(), per_group.end(),
                         [](const Flow& a, const Flow& b) { return a.weight > b.weight; });
        if (per_group.size() > cfg_.max_flows_per_group) {
            per_group.resize(cfg_.max_flows_per_group);
        }
        flows.insert(flows.end(), per_group.begin(), per_group.end());
    }

    return flows;
}

}  // namespace pulse
