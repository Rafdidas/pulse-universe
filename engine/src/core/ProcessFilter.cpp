#include "core/ProcessFilter.h"

#include <algorithm>

namespace pulse {

ProcessFilter::ProcessFilter(FilterConfig cfg) : cfg_(cfg) {}

std::vector<ProcessGroup> ProcessFilter::select(std::vector<ProcessGroup> groups,
                                                double total_mem_mb) const {
    const auto score = [&](const ProcessGroup& g) {
        const double cpu = g.cpu_pct.value_or(0.0);
        const double mem_norm =
            total_mem_mb > 0.0 ? (g.mem_mb / total_mem_mb) * 100.0 : 0.0;
        const double base = cpu * cfg_.cpu_weight + mem_norm * cfg_.mem_weight;
        const double bonus = g.account == Account::User ? cfg_.user_account_bonus : 1.0;
        return base * bonus;
    };

    // stable_sort 라야 동점일 때 입력 순서가 보존된다.
    std::stable_sort(groups.begin(), groups.end(),
                     [&](const ProcessGroup& a, const ProcessGroup& b) {
                         return score(a) > score(b);
                     });

    if (groups.size() > cfg_.max_groups) {
        groups.resize(cfg_.max_groups);
    }
    return groups;
}

}  // namespace pulse
