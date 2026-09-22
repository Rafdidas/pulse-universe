#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/RawTypes.h"

namespace pulse {

struct SpawnedProcess {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string name;
};

struct LifecycleDelta {
    std::vector<SpawnedProcess> spawned;
    std::vector<uint32_t> terminated;
};

// 필터 전 전체 프로세스 집합을 받아 실제 생성/종료만 보고한다.
// 필터된 목록에서 빠진 것을 종료로 오인하지 않기 위한 장치다.
class LifecycleTracker {
public:
    LifecycleDelta update(const std::vector<RawProcess>& processes);

private:
    bool primed_ = false;
    std::unordered_map<uint32_t, uint64_t> start_time_by_pid_;
};

}  // namespace pulse
