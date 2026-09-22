#include "core/LifecycleTracker.h"

#include <unordered_set>

namespace pulse {

LifecycleDelta LifecycleTracker::update(const std::vector<RawProcess>& processes) {
    LifecycleDelta delta;

    std::unordered_map<uint32_t, uint64_t> current;
    current.reserve(processes.size());
    for (const RawProcess& p : processes) {
        // emplace keeps the first occurrence, so a pid repeated within one
        // sample resolves the same way here as in the detection loop below.
        current.emplace(p.pid, p.start_time_ms);
    }

    if (!primed_) {
        // 첫 호출은 기준선을 세울 뿐 아무것도 보고하지 않는다.
        start_time_by_pid_ = std::move(current);
        primed_ = true;
        return delta;
    }

    // 한 표본에 같은 pid 가 두 번 들어와도 한 번만 보고한다.
    // 그러지 않으면 화면에서 같은 프로그램의 생성 애니메이션이 두 번 재생된다.
    std::unordered_set<uint32_t> reported;
    reported.reserve(processes.size());

    for (const RawProcess& p : processes) {
        if (!reported.insert(p.pid).second) {
            continue;
        }
        const auto it = start_time_by_pid_.find(p.pid);
        if (it == start_time_by_pid_.end()) {
            delta.spawned.push_back(SpawnedProcess{p.pid, p.ppid, p.name});
        } else if (it->second != p.start_time_ms) {
            // 같은 pid 인데 시작 시각이 다르다. 재사용된 pid 다.
            delta.terminated.push_back(p.pid);
            delta.spawned.push_back(SpawnedProcess{p.pid, p.ppid, p.name});
        }
    }

    // 이 루프는 unordered_map 을 순회하므로 여기서 추가되는 pid 들의 순서는
    // 정해져 있지 않다. terminated 는 순서가 아니라 집합으로 취급한다.
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
