#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct FilterConfig {
    size_t max_groups = 40;
    double cpu_weight = 1.0;
    double mem_weight = 1.0;
    // 사용자가 직접 띄운 프로그램이 배경 서비스보다 우선 노출되게 한다.
    double user_account_bonus = 1.5;
    // 직전 목록에 있던 그룹의 점수에 곱한다. 경계 순위의 작은 그룹들이 점수의
    // 미세한 변동만으로 매초 목록을 드나드는 것을 막는다. 값은 실측으로 정했다
    // (M5 스펙 4.1: 72초 동안 목록 진입 44회 → 1.25 에서 30회 → 2.0 에서 9회).
    double incumbent_bonus = 2.0;
};

// 그룹 점수를 계산해 상위 N개만 남긴다. incumbents 는 직전 선택 결과의
// 그룹 key 집합이다 — 그 안의 그룹은 incumbent_bonus 를 받는다.
class ProcessFilter {
public:
    explicit ProcessFilter(FilterConfig cfg = {});

    std::vector<ProcessGroup> select(
        std::vector<ProcessGroup> groups,
        double total_mem_mb,
        const std::unordered_set<std::string>& incumbents = {}) const;

private:
    FilterConfig cfg_;
};

}  // namespace pulse
