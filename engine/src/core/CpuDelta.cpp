#include "core/CpuDelta.h"

#include <algorithm>

namespace pulse {

CpuDelta::CpuDelta(unsigned core_count)
    : core_count_(core_count == 0 ? 1u : core_count) {}

std::optional<double> CpuDelta::update(uint32_t pid,
                                       uint64_t cpu_cumulative_ms,
                                       uint64_t timestamp_ms) {
    const auto it = previous_.find(pid);
    if (it == previous_.end()) {
        previous_.emplace(pid, Sample{cpu_cumulative_ms, timestamp_ms});
        return std::nullopt;
    }

    const Sample before = it->second;
    it->second = Sample{cpu_cumulative_ms, timestamp_ms};

    // 시계가 전진하지 않았다.
    if (timestamp_ms <= before.wall_ms) {
        return std::nullopt;
    }
    // 누적 카운터가 뒤로 갔다. pid 재사용이거나 표본 손상이다.
    if (cpu_cumulative_ms < before.cpu_ms) {
        return std::nullopt;
    }

    const double wall_ms = static_cast<double>(timestamp_ms - before.wall_ms);
    const double cpu_ms = static_cast<double>(cpu_cumulative_ms - before.cpu_ms);
    const double pct = cpu_ms / (wall_ms * static_cast<double>(core_count_)) * 100.0;

    return std::clamp(pct, 0.0, 100.0);
}

void CpuDelta::forget(uint32_t pid) {
    previous_.erase(pid);
}

}  // namespace pulse
