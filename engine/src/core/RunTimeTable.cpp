#include "core/RunTimeTable.h"

#include <algorithm>

namespace pulse {

RunTimeTable::RunTimeTable(double ticks_per_second) : ticks_per_second_(ticks_per_second) {}

void RunTimeTable::threadStarted(uint32_t pid, uint32_t tid) {
    pid_by_tid_[tid] = pid;
}

void RunTimeTable::threadEnded(uint32_t tid) {
    pid_by_tid_.erase(tid);
}

uint32_t RunTimeTable::pidOf(const Slice& slice) const {
    const auto it = pid_by_tid_.find(slice.tid);
    return it != pid_by_tid_.end() ? it->second : slice.pid_at_open;
}

void RunTimeTable::credit(uint32_t core, const Slice& slice, int64_t until) {
    const int64_t length = until - slice.since;
    // 늦게 도착한 이벤트(버퍼 병합 순서 차이)는 이미 닫은 창보다 이를 수 있다.
    // 음수 길이는 버린다.
    if (length <= 0) {
        return;
    }
    const uint32_t pid = pidOf(slice);
    // pid 0 은 Idle 이거나 아직 모르는 스레드다. 어느 그룹에도 속하지 않는다.
    if (pid == 0) {
        return;
    }
    ticks_[{pid, core}] += length;
}

void RunTimeTable::contextSwitch(uint32_t core, uint32_t new_tid, int64_t ts) {
    if (!seen_) {
        seen_ = true;
        window_start_ = ts;
    }
    latest_ = std::max(latest_, ts);

    const auto found = pid_by_tid_.find(new_tid);
    Slice next;
    next.tid = new_tid;
    next.pid_at_open = found != pid_by_tid_.end() ? found->second : 0;
    // 이미 닫은 창 안으로 조각이 거슬러 올라가지 않게 한다.
    next.since = std::max(ts, window_start_);

    const auto it = running_.find(core);
    if (it != running_.end()) {
        credit(core, it->second, ts);
        it->second = next;
    } else {
        running_.emplace(core, next);
    }
}

RawThreadMapping RunTimeTable::drain() {
    RawThreadMapping mapping;
    if (!seen_) {
        return mapping;
    }

    const int64_t now = latest_;
    for (auto& [core, slice] : running_) {
        credit(core, slice, now);
        slice.since = std::max(slice.since, now);
    }

    mapping.window_seconds = static_cast<double>(now - window_start_) / ticks_per_second_;
    window_start_ = now;

    mapping.run_times.reserve(ticks_.size());
    for (const auto& [key, ticks] : ticks_) {
        RawRunTime run;
        run.pid = key.first;
        run.core = key.second;
        run.seconds = static_cast<double>(ticks) / ticks_per_second_;
        mapping.run_times.push_back(run);
    }
    ticks_.clear();
    return mapping;
}

}  // namespace pulse
