#pragma once

#include <cstddef>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct FilterConfig {
    size_t max_groups = 40;
    double cpu_weight = 1.0;
    double mem_weight = 1.0;
    // 사용자가 직접 띄운 프로그램이 배경 서비스보다 우선 노출되게 한다.
    double user_account_bonus = 1.5;
};

// 그룹 점수를 계산해 상위 N개만 남긴다.
class ProcessFilter {
public:
    explicit ProcessFilter(FilterConfig cfg = {});

    std::vector<ProcessGroup> select(std::vector<ProcessGroup> groups,
                                     double total_mem_mb) const;

private:
    FilterConfig cfg_;
};

}  // namespace pulse
