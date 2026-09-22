#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace pulse {

// 누적 CPU 시간 표본 두 개의 차이로 순간 사용률을 구한다.
// Windows 는 순간 사용률을 제공하지 않으므로 직전 표본을 보관해야 한다.
class CpuDelta {
public:
    explicit CpuDelta(unsigned core_count);

    // 이 pid 의 첫 표본이거나 표본이 유효하지 않으면 nullopt 를 돌려준다.
    // 유효하면 0~100 범위의 사용률을 돌려준다.
    std::optional<double> update(uint32_t pid, uint64_t cpu_cumulative_ms, uint64_t timestamp_ms);

    void forget(uint32_t pid);

private:
    struct Sample {
        uint64_t cpu_ms = 0;
        uint64_t wall_ms = 0;
    };

    unsigned core_count_;
    std::unordered_map<uint32_t, Sample> previous_;
};

}  // namespace pulse
