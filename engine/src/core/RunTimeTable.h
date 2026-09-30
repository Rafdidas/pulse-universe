#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

#include "platform/RawTypes.h"

namespace pulse {

// ETW 스펙 5절. 문맥 전환 이벤트로 (프로세스, 코어)별 실행 시간을 쌓는다.
// 이벤트를 푸는 것은 플랫폼 계층의 수집기이고, 이 클래스는 Win32 를 모른다.
// 스레드 안전하지 않다 — 수집기가 뮤텍스로 감싼다.
// 시각은 정수 틱이다. 틱/초는 생성자에서 받는다.
class RunTimeTable {
public:
    explicit RunTimeTable(double ticks_per_second);

    // Thread Start / DCStart(런다운). 스레드가 어느 프로세스 것인지 기억한다.
    void threadStarted(uint32_t pid, uint32_t tid);

    // Thread End. 표에서 지운다. 코어에 올라가 있던 조각은 조각이 열릴 때
    // 기억해 둔 pid 로 정산된다.
    void threadEnded(uint32_t tid);

    // 코어 core 가 ts 에 new_tid 로 넘어갔다. 이전 조각을 ts 까지 정산하고
    // 새 조각을 연다.
    void contextSwitch(uint32_t core, uint32_t new_tid, int64_t ts);

    // 창을 닫고 그동안 쌓은 실행 시간을 돌려준다. 기준 시각은 지금까지 본
    // 이벤트 시각의 최댓값이다. 코어를 독점해 전환이 없는 스레드도 세도록,
    // 진행 중인 조각을 기준 시각까지 정산하고 다음 창으로 이월한다.
    RawThreadMapping drain();

private:
    struct Slice {
        uint32_t tid = 0;
        // 조각이 열릴 때 알던 pid. 0 이면 몰랐다 (Idle 도 0 이다).
        uint32_t pid_at_open = 0;
        int64_t since = 0;
    };

    uint32_t pidOf(const Slice& slice) const;
    void credit(uint32_t core, const Slice& slice, int64_t until);

    double ticks_per_second_;
    std::unordered_map<uint32_t, uint32_t> pid_by_tid_;
    std::unordered_map<uint32_t, Slice> running_;          // core -> 진행 중인 조각
    std::map<std::pair<uint32_t, uint32_t>, int64_t> ticks_;  // (pid, core) -> 틱
    bool seen_ = false;
    int64_t latest_ = 0;        // 지금까지 본 이벤트 시각의 최댓값
    int64_t window_start_ = 0;  // 이번 창의 시작
};

}  // namespace pulse
