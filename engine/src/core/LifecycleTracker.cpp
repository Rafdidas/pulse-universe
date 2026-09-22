#include "core/LifecycleTracker.h"

namespace pulse {

LifecycleDelta LifecycleTracker::update(const std::vector<RawProcess>& processes) {
    LifecycleDelta delta;

    std::unordered_map<uint32_t, uint64_t> current;
    current.reserve(processes.size());
    for (const RawProcess& p : processes) {
        current[p.pid] = p.start_time_ms;
    }

    if (!primed_) {
        // 첫 호출은 기준선을 세울 뿐 아무것도 보고하지 않는다.
        start_time_by_pid_ = std::move(current);
        primed_ = true;
        return delta;
    }

    for (const RawProcess& p : processes) {
        const auto it = start_time_by_pid_.find(p.pid);
        if (it == start_time_by_pid_.end()) {
            delta.spawned.push_back(SpawnedProcess{p.pid, p.ppid, p.name});
        } else if (it->second != p.start_time_ms) {
            // 같은 pid 인데 시작 시각이 다르다. 재사용된 pid 다.
            delta.terminated.push_back(p.pid);
            delta.spawned.push_back(SpawnedProcess{p.pid, p.ppid, p.name});
        }
    }

    for (const auto& [pid, start_time] : start_time_by_pid_) {
        (void)start_time;
        if (current.find(pid) == current.end()) {
            delta.terminated.push_back(pid);
        }
    }

    start_time_by_pid_ = std::move(current);
    return delta;
}

}  // namespace pulse
