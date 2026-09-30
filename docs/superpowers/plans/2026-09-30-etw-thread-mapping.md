# ETW 실측 스레드-코어 매핑 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 관리자 권한으로 실행되면 ETW 문맥 전환 이벤트로 그룹별·코어별 실행 시간을 재어 `flows[]` 를 `source: "measured"` 로 채우고, 권한이 없으면 지금의 추정을 그대로 쓴다.

**Architecture:** 순수 코어 계층에 `RunTimeTable`(문맥 전환 → (pid, 코어)별 실행 시간 누적)과 `measuredFlows`(실행 시간 → 흐름)를 둔다. 플랫폼 계층의 `EtwSchedulerCollector` 가 전용 시스템 로거 세션 `PulseUniverse-Sched` 를 열고 별도 스레드의 `ProcessTrace` 로 이벤트를 받아 표에 쌓는다. `WindowsSystemReader::read()` 가 표본마다 창을 비워 `RawSample::thread_mapping` 에 싣고, `DataAggregator` 가 그것이 있으면 실측, 없으면 추정으로 흐름을 만든다. `--mapping auto|estimated|measured` 가 수집기를 켤지 정한다.

**Tech Stack:** C++20 / MSVC `/W4 /permissive- /utf-8` / CMake + vcpkg / Catch2 3.16 / Win32 ETW (evntrace.h, evntcons.h)

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-30-etw-thread-mapping-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`.
- 작업 브랜치는 `feature/etw-thread-mapping` 다. `web/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 관리자 권한 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다. "파일 끝에 붙인다" 는 기존 마지막 줄 뒤에 빈 줄 하나를 두고 블록을 그대로 붙인다는 뜻이다.
- `engine/src/core/**` 는 `<windows.h>` 나 Win32 헤더를 include 하지 않는다 (ETW 스펙 D58).
- 빌드는 경고 0 이어야 한다 (`/W4`). 경고가 나면 BLOCKED 로 보고한다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 빌드·테스트가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 구현자는 관리자 권한으로 아무것도 실행하지 않는다 (UAC 를 띄우지 않는다). 관리자 권한 확인은 컨트롤러가 한다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

명령은 Git Bash 에서 `C:\dev\pulse-uni\engine` 을 작업 디렉터리로 실행한다.

```
export PATH="/c/Program Files/CMake/bin:$PATH" VCPKG_ROOT="C:/vcpkg"
cmake --build --preset default          # Debug 빌드 (engine/build)
./build/tests/Debug/pulse-tests.exe     # 전체 테스트
```

새 소스 파일을 CMakeLists 에 더한 뒤 첫 빌드는 CMake 가 다시 구성한다 (`--preset default` 가 알아서 한다).

기준선(작업 전 `main` + 스펙 커밋): 엔진 테스트 175 test cases 통과, 빌드 경고 0.

## File Structure

```
engine/src/platform/RawTypes.h                          (전체 교체) RawRunTime, RawThreadMapping, RawSample::thread_mapping, HostInfo::thread_mapping
engine/src/core/RunTimeTable.{h,cpp}                    실행 시간 누적 (순수)
engine/src/core/MeasuredFlows.{h,cpp}                   실행 시간 → 흐름 (순수)
engine/src/core/DataAggregator.{h,cpp}                  (전체 교체) 실측/추정 분기
engine/src/core/Snapshot.h                              (전체 교체) Flow::source 주석
engine/src/cli/Options.{h,cpp}                          (전체 교체) --mapping
engine/src/platform/windows/EtwSchedulerCollector.{h,cpp}  ETW 세션과 소비 스레드
engine/src/platform/windows/WindowsSystemReader.{h,cpp} (전체 교체) 수집기 소유, drain
engine/src/main.cpp                                     (전체 교체) 옵션 전달, 시작 한 줄
engine/src/app/ServeApp.cpp                             (전체 교체) hello 능력
engine/src/network/Serializer.h                         (전체 교체) HelloInfo::thread_mapping 주석
engine/CMakeLists.txt, engine/tests/CMakeLists.txt      소스·테스트 추가, advapi32
engine/tests/test_run_time_table.cpp, test_measured_flows.cpp, test_etw_collector.cpp
engine/tests/test_aggregator.cpp, test_options.cpp      (끝에 붙임)
docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md  4.3·6.2절 정정
```

---

## Task 1: 데이터 모델과 실행 시간 누적

**Files:**
- Modify (전체 교체): `engine/src/platform/RawTypes.h`
- Create: `engine/src/core/RunTimeTable.h`, `engine/src/core/RunTimeTable.cpp`
- Modify: `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`
- Test: `engine/tests/test_run_time_table.cpp`

**Interfaces:**
- Produces:
  - `struct RawRunTime { uint32_t pid; uint32_t core; double seconds; }`
  - `struct RawThreadMapping { double window_seconds; std::vector<RawRunTime> run_times; }`
  - `RawSample::thread_mapping` (`std::optional<RawThreadMapping>`), `HostInfo::thread_mapping` (`std::string`, 기본 `"estimated"`)
  - `class RunTimeTable { explicit RunTimeTable(double ticks_per_second); void threadStarted(uint32_t pid, uint32_t tid); void threadEnded(uint32_t tid); void contextSwitch(uint32_t core, uint32_t new_tid, int64_t ts); RawThreadMapping drain(); }`

- [ ] **Step 1: `engine/src/platform/RawTypes.h` 전체 교체**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct RawProcess {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string name;
    uint64_t cpu_cumulative_ms = 0;
    uint64_t mem_bytes = 0;
    uint32_t thread_count = 0;
    uint64_t start_time_ms = 0;
    Account account = Account::System;
    std::string image_path;
};

struct RawCore {
    uint32_t id = 0;
    double pct = 0.0;
};

struct RawMemory {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

struct HostInfo {
    std::string os;
    bool elevated = false;
    // hello 의 capabilities.thread_mapping. 엔진이 시작할 때의 실측 수집기 상태다
    // (ETW 스펙 8절). 수집기가 없으면 "estimated".
    std::string thread_mapping = "estimated";
};

// 한 창 동안 한 프로세스가 한 코어에서 돈 시간.
struct RawRunTime {
    uint32_t pid = 0;
    uint32_t core = 0;
    double seconds = 0.0;
};

// 실측 스레드-코어 매핑 한 창 (ETW 스펙 4절). 창 길이는 이벤트 시각 기준이다.
struct RawThreadMapping {
    double window_seconds = 0.0;
    std::vector<RawRunTime> run_times;
};

struct RawSample {
    std::vector<RawProcess> processes;
    std::vector<RawCore> cores;
    RawMemory memory;
    uint64_t timestamp_ms = 0;
    // 실측 수집기가 살아 있으면 채운다. 없으면 흐름을 추정한다 (계약서 6.2절).
    std::optional<RawThreadMapping> thread_mapping;
};

}  // namespace pulse
```

- [ ] **Step 2: 실패하는 테스트 — `engine/tests/test_run_time_table.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#include "core/RunTimeTable.h"

using namespace pulse;
using Catch::Matchers::WithinAbs;

namespace {

// 1 틱 = 1 ms 로 두면 초 단위를 읽기 쉽다.
constexpr double kTicksPerSecond = 1000.0;

double secondsFor(const RawThreadMapping& mapping, uint32_t pid, uint32_t core) {
    const auto it = std::find_if(mapping.run_times.begin(), mapping.run_times.end(),
                                 [&](const RawRunTime& r) { return r.pid == pid && r.core == core; });
    return it == mapping.run_times.end() ? 0.0 : it->seconds;
}

}  // namespace

TEST_CASE("an empty table drains an empty zero-length window", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);

    const RawThreadMapping mapping = table.drain();

    REQUIRE(mapping.window_seconds == 0.0);
    REQUIRE(mapping.run_times.empty());
}

TEST_CASE("a slice is credited to the thread's process on the next switch", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);    // 코어 0: 스레드 1 (pid 100)
    table.contextSwitch(0, 2, 300);  // 300 ms 뒤 스레드 2 (pid 200)
    table.contextSwitch(0, 1, 500);  // 200 ms 뒤 다시 스레드 1

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(mapping.window_seconds, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 100, 0), WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 200, 0), WithinAbs(0.2, 1e-9));
}

TEST_CASE("cores are accounted separately", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(100, 2);
    table.threadStarted(300, 3);

    table.contextSwitch(0, 1, 0);
    table.contextSwitch(1, 2, 0);
    table.contextSwitch(0, 3, 400);
    table.contextSwitch(1, 3, 1000);

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(secondsFor(mapping, 100, 0), WithinAbs(0.4, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 100, 1), WithinAbs(1.0, 1e-9));
    // 코어 0 의 스레드 3 은 400 ms 부터 창 끝(1000 ms)까지 돌고 있었다.
    REQUIRE_THAT(secondsFor(mapping, 300, 0), WithinAbs(0.6, 1e-9));
}

TEST_CASE("a thread that never switches out is credited up to the drain and carried over",
          "[runtime]") {
    // 한 코어를 독점한 바쁜 루프에는 문맥 전환이 없다. 그래도 창마다 세어야 한다.
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);    // 코어 0 을 스레드 1 이 차지한다
    table.contextSwitch(1, 2, 1000); // 다른 코어의 이벤트가 시각을 1 초까지 밀어 준다

    const RawThreadMapping first = table.drain();
    REQUIRE_THAT(first.window_seconds, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(first, 100, 0), WithinAbs(1.0, 1e-9));

    table.contextSwitch(1, 2, 2000);
    const RawThreadMapping second = table.drain();
    REQUIRE_THAT(second.window_seconds, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(second, 100, 0), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(second, 200, 1), WithinAbs(1.0, 1e-9));
}

TEST_CASE("idle and unknown threads are not credited", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(0, 0);  // Idle 은 pid 0, tid 0 이다

    table.contextSwitch(0, 0, 0);
    table.contextSwitch(0, 77, 100);  // 모르는 스레드
    table.contextSwitch(0, 0, 300);

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(mapping.window_seconds, WithinAbs(0.3, 1e-9));
    REQUIRE(mapping.run_times.empty());
}

TEST_CASE("a thread learned after it was switched in is still credited", "[runtime]") {
    // 세션을 켠 직후에는 런다운(DCStart)보다 전환 이벤트가 먼저 올 수 있다.
    RunTimeTable table(kTicksPerSecond);

    table.contextSwitch(0, 1, 0);
    table.threadStarted(100, 1);
    table.contextSwitch(0, 0, 250);

    REQUIRE_THAT(secondsFor(table.drain(), 100, 0), WithinAbs(0.25, 1e-9));
}

TEST_CASE("a thread that ended while running is credited to the pid it had", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);

    table.contextSwitch(0, 1, 0);
    table.threadEnded(1);  // 스레드는 자기 문맥에서 끝난 뒤 코어를 내놓는다
    table.contextSwitch(0, 0, 400);

    REQUIRE_THAT(secondsFor(table.drain(), 100, 0), WithinAbs(0.4, 1e-9));
}

TEST_CASE("an event older than the last drain does not reach back into the closed window",
          "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);
    table.contextSwitch(1, 2, 1000);
    const RawThreadMapping first = table.drain();  // 창 [0, 1000]
    REQUIRE_THAT(secondsFor(first, 100, 0), WithinAbs(1.0, 1e-9));

    // 코어 0 의 전환이 900 ms 시각으로 늦게 도착한다. 이미 정산한 구간이다.
    table.contextSwitch(0, 2, 900);
    table.contextSwitch(0, 1, 1500);
    const RawThreadMapping second = table.drain();

    REQUIRE_THAT(second.window_seconds, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(secondsFor(second, 100, 0), WithinAbs(0.0, 1e-9));
    // 스레드 2 의 코어 0 조각은 창 시작(1000)부터 1500 까지다.
    REQUIRE_THAT(secondsFor(second, 200, 0), WithinAbs(0.5, 1e-9));
}
```

- [ ] **Step 3: CMake 에 등록**

`engine/tests/CMakeLists.txt` 의 `add_executable(pulse-tests` 목록에서 `test_flow_estimator.cpp` 다음 줄에 넣는다:

```
  test_run_time_table.cpp
```

`engine/CMakeLists.txt` 의 `add_library(pulse_core` 목록에서 `src/core/FlowEstimator.cpp` 다음 줄에 넣는다:

```
  src/core/RunTimeTable.cpp
```

- [ ] **Step 4: 실패 확인 (RED)**

Run: `cmake --build --preset default`
Expected: FAIL — `RunTimeTable.h` 또는 `RunTimeTable.cpp` 를 찾을 수 없다는 오류.

- [ ] **Step 5: `engine/src/core/RunTimeTable.h`**

```cpp
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
```

- [ ] **Step 6: `engine/src/core/RunTimeTable.cpp`**

```cpp
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
```

- [ ] **Step 7: 통과 확인 (GREEN)**

Run: `cmake --build --preset default && ./build/tests/Debug/pulse-tests.exe "[runtime]"`
Expected: 빌드 경고 0, `All tests passed (... in 8 test cases)`.

- [ ] **Step 8: 전체 확인**

Run: `./build/tests/Debug/pulse-tests.exe`
Expected: `test cases: 183 | 183 passed`.

- [ ] **Step 9: 커밋**

```
git add src/platform/RawTypes.h src/core/RunTimeTable.h src/core/RunTimeTable.cpp tests/test_run_time_table.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(engine): accumulate per-process per-core run time from context switches"
```

---

## Task 2: 실측 흐름과 DataAggregator 분기

**Files:**
- Create: `engine/src/core/MeasuredFlows.h`, `engine/src/core/MeasuredFlows.cpp`
- Modify (전체 교체): `engine/src/core/DataAggregator.h`, `engine/src/core/DataAggregator.cpp`, `engine/src/core/Snapshot.h`
- Modify: `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`, `engine/tests/test_aggregator.cpp` (끝에 붙임)
- Test: `engine/tests/test_measured_flows.cpp`

**Interfaces:**
- Consumes: Task 1 의 `RawThreadMapping`, `RawRunTime`, `RawSample::thread_mapping`. 기존 `FlowConfig { min_weight = 0.05; max_flows_per_group = 4; }` (`core/FlowEstimator.h`), `Flow`, `ProcessGroup`, `ChildProcess` (`core/Snapshot.h`).
- Produces: `std::vector<Flow> measuredFlows(const std::vector<ProcessGroup>& groups, const RawThreadMapping& mapping, const FlowConfig& cfg)` — weight = 구성원 실행 시간 합 ÷ `window_seconds` (1 에서 자름), `source = "measured"`. `DataAggregator::aggregate` 는 `sample.thread_mapping` 이 있으면 이것을, 없으면 `FlowEstimator::estimate` 를 쓴다.

- [ ] **Step 1: 실패하는 테스트 — `engine/tests/test_measured_flows.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

#include "core/MeasuredFlows.h"

using namespace pulse;
using Catch::Matchers::WithinAbs;

namespace {

ProcessGroup makeGroup(std::string name, uint32_t root_pid, std::vector<uint32_t> child_pids = {}) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = root_pid;
    g.key = name + ":" + std::to_string(root_pid);
    for (const uint32_t pid : child_pids) {
        ChildProcess child;
        child.pid = pid;
        g.children.push_back(child);
    }
    return g;
}

RawThreadMapping window(double seconds, std::vector<RawRunTime> runs) {
    RawThreadMapping m;
    m.window_seconds = seconds;
    m.run_times = std::move(runs);
    return m;
}

const Flow* find(const std::vector<Flow>& flows, const std::string& key, uint32_t core) {
    const auto it = std::find_if(flows.begin(), flows.end(), [&](const Flow& f) {
        return f.group == key && f.core == core;
    });
    return it == flows.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("measured weight is the share of the window a group ran on a core", "[measured]") {
    const auto flows = measuredFlows({makeGroup("a.exe", 10)},
                                     window(2.0, {{10, 3, 0.5}}), FlowConfig{});

    const Flow* f = find(flows, "a.exe:10", 3);
    REQUIRE(f != nullptr);
    REQUIRE_THAT(f->weight, WithinAbs(0.25, 1e-9));
    REQUIRE(f->source == "measured");
}

TEST_CASE("run time of every group member is summed per core", "[measured]") {
    const auto flows = measuredFlows(
        {makeGroup("a.exe", 10, {11, 12})},
        window(1.0, {{10, 0, 0.1}, {11, 0, 0.2}, {12, 0, 0.3}, {12, 1, 0.4}}), FlowConfig{});

    REQUIRE_THAT(find(flows, "a.exe:10", 0)->weight, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(find(flows, "a.exe:10", 1)->weight, WithinAbs(0.4, 1e-9));
}

TEST_CASE("a weight above one core is clamped to one", "[measured]") {
    // 창 경계에서 이월된 조각 때문에 드물게 창보다 길게 잡힐 수 있다.
    const auto flows = measuredFlows({makeGroup("a.exe", 10, {11})},
                                     window(1.0, {{10, 0, 0.7}, {11, 0, 0.6}}), FlowConfig{});

    REQUIRE_THAT(find(flows, "a.exe:10", 0)->weight, WithinAbs(1.0, 1e-9));
}

TEST_CASE("weak flows are dropped and each group keeps its strongest cores", "[measured]") {
    FlowConfig cfg;
    cfg.min_weight = 0.05;
    cfg.max_flows_per_group = 2;

    const auto flows = measuredFlows(
        {makeGroup("a.exe", 10)},
        window(1.0, {{10, 0, 0.04}, {10, 1, 0.3}, {10, 2, 0.1}, {10, 3, 0.2}}), cfg);

    REQUIRE(flows.size() == 2);
    REQUIRE(flows[0].core == 1);
    REQUIRE(flows[1].core == 3);
}

TEST_CASE("processes outside the shown groups are ignored", "[measured]") {
    const auto flows = measuredFlows({makeGroup("a.exe", 10)},
                                     window(1.0, {{10, 0, 0.5}, {99, 1, 0.9}}), FlowConfig{});

    REQUIRE(flows.size() == 1);
    REQUIRE(flows[0].group == "a.exe:10");
}

TEST_CASE("an empty window yields no flows", "[measured]") {
    REQUIRE(measuredFlows({makeGroup("a.exe", 10)}, window(0.0, {{10, 0, 0.5}}), FlowConfig{})
                .empty());
}
```

- [ ] **Step 2: `engine/tests/test_aggregator.cpp` 끝에 붙인다**

```cpp
TEST_CASE("a sample carrying a thread mapping yields measured flows", "[aggregate][measured]") {
    // ETW 스펙 6절: 실측 매핑이 실려 오면 추정 대신 그것으로 흐름을 만든다.
    DataAggregator aggregator(2);
    RawSample sample = makeSample({makeProcess(10, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000);
    RawThreadMapping mapping;
    mapping.window_seconds = 1.0;
    mapping.run_times = {RawRunTime{10, 1, 0.5}};
    sample.thread_mapping = mapping;

    const auto snap = aggregator.aggregate(sample);

    REQUIRE(snap.flows.size() == 1);
    REQUIRE(snap.flows[0].group == "a.exe:10");
    REQUIRE(snap.flows[0].core == 1);
    REQUIRE(snap.flows[0].source == "measured");
    REQUIRE_THAT(snap.flows[0].weight, Catch::Matchers::WithinAbs(0.5, 0.0001));
}

TEST_CASE("a sample without a thread mapping keeps estimating flows", "[aggregate][measured]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(makeSample({makeProcess(10, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));

    const auto snap = aggregator.aggregate(
        makeSample({makeProcess(10, 0, "a.exe", 1500, 100ull * 1024 * 1024)}, 2000));

    REQUIRE_FALSE(snap.flows.empty());
    for (const Flow& f : snap.flows) {
        REQUIRE(f.source == "estimated");
    }
}
```

- [ ] **Step 3: CMake 에 등록**

`engine/tests/CMakeLists.txt` 에서 `test_run_time_table.cpp` 다음 줄에:

```
  test_measured_flows.cpp
```

`engine/CMakeLists.txt` 에서 `src/core/RunTimeTable.cpp` 다음 줄에:

```
  src/core/MeasuredFlows.cpp
```

- [ ] **Step 4: 실패 확인 (RED)**

Run: `cmake --build --preset default`
Expected: FAIL — `MeasuredFlows.h` 를 찾을 수 없다는 오류.

- [ ] **Step 5: `engine/src/core/MeasuredFlows.h`**

```cpp
#pragma once

#include <vector>

#include "core/FlowEstimator.h"
#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

// ETW 스펙 6절. 실측 실행 시간을 흐름으로 바꾼다. weight 는 그룹 구성원이 그 코어에서
// 돈 시간 ÷ 창 길이 — "이 그룹이 코어를 몇 % 썼나" 이다 (1 에서 자른다).
// 걸러내기는 FlowEstimator 와 같다: min_weight 미만은 버리고 그룹당 상위
// max_flows_per_group 개만 남긴다. 결과는 전부 source == "measured" 다.
std::vector<Flow> measuredFlows(const std::vector<ProcessGroup>& groups,
                                const RawThreadMapping& mapping, const FlowConfig& cfg);

}  // namespace pulse
```

- [ ] **Step 6: `engine/src/core/MeasuredFlows.cpp`**

```cpp
#include "core/MeasuredFlows.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace pulse {

std::vector<Flow> measuredFlows(const std::vector<ProcessGroup>& groups,
                                const RawThreadMapping& mapping, const FlowConfig& cfg) {
    std::vector<Flow> flows;
    if (mapping.window_seconds <= 0.0) {
        return flows;
    }

    // pid -> 그룹 순번. 화면에 남은 그룹의 구성원만 담긴다.
    std::unordered_map<uint32_t, std::size_t> group_by_pid;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        group_by_pid.emplace(groups[i].root_pid, i);
        for (const ChildProcess& child : groups[i].children) {
            group_by_pid.emplace(child.pid, i);
        }
    }

    // 그룹마다 코어별 실행 시간을 더한다. 코어 순서를 고정하려고 map 을 쓴다.
    std::vector<std::map<uint32_t, double>> seconds_by_core(groups.size());
    for (const RawRunTime& run : mapping.run_times) {
        const auto it = group_by_pid.find(run.pid);
        if (it != group_by_pid.end()) {
            seconds_by_core[it->second][run.core] += run.seconds;
        }
    }

    std::vector<Flow> per_group;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        per_group.clear();
        for (const auto& [core, seconds] : seconds_by_core[i]) {
            const double weight = std::min(1.0, seconds / mapping.window_seconds);
            if (weight < cfg.min_weight) {
                continue;
            }
            Flow f;
            f.group = groups[i].key;
            f.core = core;
            f.weight = weight;
            f.source = "measured";
            per_group.push_back(std::move(f));
        }

        std::stable_sort(per_group.begin(), per_group.end(),
                         [](const Flow& a, const Flow& b) { return a.weight > b.weight; });
        if (per_group.size() > cfg.max_flows_per_group) {
            per_group.resize(cfg.max_flows_per_group);
        }
        flows.insert(flows.end(), per_group.begin(), per_group.end());
    }

    return flows;
}

}  // namespace pulse
```

- [ ] **Step 7: `engine/src/core/DataAggregator.h` 전체 교체**

```cpp
#pragma once

#include <string>
#include <unordered_set>

#include "core/CpuDelta.h"
#include "core/FlowEstimator.h"
#include "core/GroupBuilder.h"
#include "core/LifecycleTracker.h"
#include "core/ProcessFilter.h"
#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

struct AggregatorConfig {
    FilterConfig filter;
    FlowConfig flow;
};

// 원시 표본 하나를 받아 SystemSnapshot 하나를 만든다.
// 이 클래스만이 core/ 의 나머지 부품들을 안다.
class DataAggregator {
public:
    explicit DataAggregator(unsigned core_count, AggregatorConfig cfg = {});

    SystemSnapshot aggregate(const RawSample& sample);

private:
    CpuDelta cpu_delta_;
    GroupBuilder group_builder_;
    ProcessFilter filter_;
    LifecycleTracker lifecycle_;
    // 실측 흐름(measuredFlows)도 추정과 같은 걸러내기를 쓴다.
    FlowConfig flow_config_;
    FlowEstimator flow_estimator_;
    // 직전 스냅샷에 실린 그룹 key. 다음 선택에서 유지 보너스를 받는다.
    std::unordered_set<std::string> shown_keys_;
    uint64_t seq_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 8: `engine/src/core/DataAggregator.cpp` 전체 교체**

```cpp
#include "core/DataAggregator.h"

#include <unordered_map>

#include "core/MeasuredFlows.h"

namespace pulse {
namespace {

constexpr double kBytesPerMb = 1024.0 * 1024.0;

double toMb(uint64_t bytes) {
    return static_cast<double>(bytes) / kBytesPerMb;
}

}  // namespace

DataAggregator::DataAggregator(unsigned core_count, AggregatorConfig cfg)
    : cpu_delta_(core_count),
      filter_(cfg.filter),
      flow_config_(cfg.flow),
      flow_estimator_(cfg.flow) {}

SystemSnapshot DataAggregator::aggregate(const RawSample& sample) {
    SystemSnapshot snapshot;
    snapshot.seq = ++seq_;
    snapshot.t = sample.timestamp_ms;

    // 1. pid 별 순간 CPU 사용률. 이 단계는 필터 전 전체를 대상으로 한다.
    std::unordered_map<uint32_t, std::optional<double>> cpu_by_pid;
    cpu_by_pid.reserve(sample.processes.size());
    for (const RawProcess& p : sample.processes) {
        // 한 표본에 같은 pid 가 두 번 들어오면 두 번째 update 는 방금 저장한
        // 표본과 비교해 nullopt 를 돌려주고, 유효한 값을 덮어쓴다. 첫 항목만 쓴다.
        if (cpu_by_pid.find(p.pid) != cpu_by_pid.end()) {
            continue;
        }
        cpu_by_pid[p.pid] =
            cpu_delta_.update(p.pid, p.cpu_cumulative_ms, sample.timestamp_ms);
    }

    // 2. 생명주기도 필터 전 전체를 대상으로 한다 (스펙 4.6).
    snapshot.lifecycle = lifecycle_.update(sample.processes);
    for (const uint32_t pid : snapshot.lifecycle.terminated) {
        cpu_delta_.forget(pid);
    }

    // 3. 전체 합계.
    snapshot.system.mem_used_mb = toMb(sample.memory.used_bytes);
    snapshot.system.mem_total_mb = toMb(sample.memory.total_bytes);
    snapshot.system.process_total = static_cast<uint32_t>(sample.processes.size());
    for (const RawProcess& p : sample.processes) {
        snapshot.system.thread_total += p.thread_count;
    }

    // 4. 코어 부하와 시스템 평균.
    double core_pct_sum = 0.0;
    snapshot.cores.reserve(sample.cores.size());
    for (const RawCore& c : sample.cores) {
        snapshot.cores.push_back(CoreLoad{c.id, c.pct});
        core_pct_sum += c.pct;
    }
    // system.cpu_pct 는 sample.cores(PDH)의 평균이다. PDH 는 리더 생성자에서
    // 이미 첫 수집을 해 두므로 첫 주기부터 유효한 값이 있다 — 델타 기반 값과
    // 달리 seq_ 에 의존하지 않는다. cores 가 비어 있을 때만 값을 비운다.
    if (!sample.cores.empty()) {
        snapshot.system.cpu_pct = core_pct_sum / static_cast<double>(sample.cores.size());
    }

    // 5. 그룹화 후 그룹별 CPU 를 구성원 합으로 채운다.
    GroupingResult grouped = group_builder_.build(sample.processes);
    snapshot.ambient = grouped.ambient;

    for (ProcessGroup& g : grouped.groups) {
        double sum = 0.0;
        bool any = false;

        const auto accumulate = [&](uint32_t pid) {
            const auto it = cpu_by_pid.find(pid);
            if (it != cpu_by_pid.end() && it->second.has_value()) {
                sum += *it->second;
                any = true;
            }
        };

        accumulate(g.root_pid);
        for (ChildProcess& child : g.children) {
            const auto it = cpu_by_pid.find(child.pid);
            if (it != cpu_by_pid.end()) {
                child.cpu_pct = it->second;
            }
            accumulate(child.pid);
        }

        // 그룹 CPU 는 값을 가진 구성원들의 합이다. 값을 가진 구성원이 하나도
        // 없을 때만 비운다. 자식이 막 생겨나 아직 두 번째 표본을 못 받은
        // 주기에는 부분합이 나오는데, 이는 의도된 하한값이다 — 매번 자식이
        // 하나 늘 때마다 그룹 전체를 비우면 화면이 계속 깜빡인다.
        if (any) {
            g.cpu_pct = sum;
        }
    }

    // 5b. lifecycle.spawned[] 의 group 필드를 채운다. 필터 전 grouped.groups
    // 기준으로 pid -> group key 조회 테이블을 만든다: 제외된 svchost 트리에
    // 속한 프로세스라도 그 그룹 key 자체는 유효한 정보이기 때문이다.
    // LifecycleTracker 는 그룹을 모르므로 그 값은 항상 빈 문자열이다.
    if (!snapshot.lifecycle.spawned.empty()) {
        std::unordered_map<uint32_t, std::string> group_key_by_pid;
        for (const ProcessGroup& g : grouped.groups) {
            group_key_by_pid.emplace(g.root_pid, g.key);
            for (const ChildProcess& child : g.children) {
                group_key_by_pid.emplace(child.pid, g.key);
            }
        }
        for (SpawnedProcess& spawned : snapshot.lifecycle.spawned) {
            const auto it = group_key_by_pid.find(spawned.pid);
            if (it != group_key_by_pid.end()) {
                spawned.group = it->second;
            }
        }
    }

    // 6. 상위 N개 선택. 합계와 생명주기는 이미 전체 기준으로 계산됐다.
    // 직전에 보였던 그룹은 유지 보너스를 받는다.
    snapshot.groups = filter_.select(std::move(grouped.groups),
                                     snapshot.system.mem_total_mb, shown_keys_);
    shown_keys_.clear();
    for (const ProcessGroup& g : snapshot.groups) {
        shown_keys_.insert(g.key);
    }

    // 7. 화면에 남은 그룹에 대해서만 흐름을 만든다. 실측 매핑이 실려 왔으면 그것을,
    // 아니면 코어 부하로 추정한다 (ETW 스펙 6절).
    snapshot.flows = sample.thread_mapping.has_value()
                         ? measuredFlows(snapshot.groups, *sample.thread_mapping, flow_config_)
                         : flow_estimator_.estimate(snapshot.groups, snapshot.cores);

    return snapshot;
}

}  // namespace pulse
```

- [ ] **Step 9: `engine/src/core/Snapshot.h` 전체 교체 (Flow::source 주석)**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pulse {

enum class Account { User, System };

struct SpawnedProcess {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string name;
    // 이 pid 가 속한 그룹의 key. GroupBuilder 결과에서 못 찾으면 빈 문자열로
    // 남는다 (제외된 svchost 트리 등). LifecycleTracker 는 그룹을 모르므로
    // 채우지 않는다 — DataAggregator 가 그룹핑 이후에 채운다.
    std::string group;
};

struct LifecycleDelta {
    std::vector<SpawnedProcess> spawned;
    std::vector<uint32_t> terminated;
};

struct ChildProcess {
    uint32_t pid = 0;
    std::string name;
    // M1 에서는 항상 "child". 실제 role(renderer, gpu-process ...) 추출은
    // 프로세스 커맨드라인이 필요하므로 M5 로 미룬다.
    std::string role = "child";
    std::optional<double> cpu_pct;
    double mem_mb = 0.0;
    uint32_t threads = 0;
};

struct ProcessGroup {
    std::string key;  // "<name>:<root_pid>"
    std::string name;
    uint32_t root_pid = 0;
    std::optional<double> cpu_pct;
    double mem_mb = 0.0;
    uint32_t proc_count = 0;
    uint32_t thread_count = 0;
    uint64_t started_at = 0;
    Account account = Account::System;
    std::string image_path;
    std::vector<ChildProcess> children;
};

struct CoreLoad {
    uint32_t id = 0;
    double pct = 0.0;
};

struct Flow {
    std::string group;  // ProcessGroup::key
    uint32_t core = 0;
    double weight = 0.0;
    // FlowEstimator 는 "estimated", 실측 매핑(measuredFlows)은 "measured" 를 단다.
    // network/Serializer.h 의 HelloInfo::thread_mapping 이 프로토콜 계층에서
    // 연결 시점의 능력을 나른다 — 수집기가 살아 있는 동안 두 값은 같다 (ETW 스펙 8절).
    std::string source = "estimated";
};

struct Ambient {
    uint32_t service_proc_count = 0;
    double service_mem_mb = 0.0;
};

struct SystemTotals {
    std::optional<double> cpu_pct;
    double mem_used_mb = 0.0;
    double mem_total_mb = 0.0;
    uint32_t process_total = 0;
    uint32_t thread_total = 0;
};

struct SystemSnapshot {
    uint64_t seq = 0;
    uint64_t t = 0;
    SystemTotals system;
    std::vector<CoreLoad> cores;
    std::vector<ProcessGroup> groups;
    std::vector<Flow> flows;
    LifecycleDelta lifecycle;
    Ambient ambient;
};

}  // namespace pulse
```

- [ ] **Step 10: 통과 확인 (GREEN)**

Run: `cmake --build --preset default && ./build/tests/Debug/pulse-tests.exe "[measured]"`
Expected: 빌드 경고 0, `All tests passed (... in 8 test cases)` (measured 6 + aggregate 2).

- [ ] **Step 11: 전체 확인**

Run: `./build/tests/Debug/pulse-tests.exe`
Expected: `test cases: 191 | 191 passed`.

- [ ] **Step 12: 커밋**

```
git add src/core/MeasuredFlows.h src/core/MeasuredFlows.cpp src/core/DataAggregator.h src/core/DataAggregator.cpp src/core/Snapshot.h tests/test_measured_flows.cpp tests/test_aggregator.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(engine): build measured flows from run time and prefer them over the estimate"
```

---

## Task 3: `--mapping` 옵션

**Files:**
- Modify (전체 교체): `engine/src/cli/Options.h`, `engine/src/cli/Options.cpp`
- Modify: `engine/tests/test_options.cpp` (끝에 붙임)

**Interfaces:**
- Produces: `enum class Mapping { Auto, Estimated, Measured }`, `Options::mapping` (기본 `Mapping::Auto`). `--mapping auto|estimated|measured`, 그 밖의 값이나 값 없음은 `ParseResult::Error` + `error == "invalid --mapping"`. 사용법 글에 `--mapping` 이 있다.

- [ ] **Step 1: 실패하는 테스트 — `engine/tests/test_options.cpp` 끝에 붙인다**

```cpp
TEST_CASE("mapping defaults to auto", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Auto);
    REQUIRE(usageText().find("--mapping") != std::string::npos);
}

TEST_CASE("mapping accepts auto, estimated and measured", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--mapping", "estimated"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Estimated);
    REQUIRE(parse({"--serve", "--mapping", "measured"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Measured);
    REQUIRE(parse({"--json", "--mapping", "auto"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Auto);
}

TEST_CASE("mapping rejects unknown or missing values", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--mapping", "exact"}, options, error) == ParseResult::Error);
    REQUIRE(error == "invalid --mapping");
    REQUIRE(parse({"--serve", "--mapping"}, options, error) == ParseResult::Error);
    REQUIRE(error == "invalid --mapping");
}
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `cmake --build --preset default`
Expected: FAIL — `Mapping` 이 선언되지 않았다는 오류.

- [ ] **Step 3: `engine/src/cli/Options.h` 전체 교체**

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pulse {

enum class Mode { None, Dump, Json, Serve };

// 스레드-코어 매핑 출처 (ETW 스펙 8절). Auto 는 실측을 시도하고 안 되면 추정한다.
enum class Mapping { Auto, Estimated, Measured };

struct Options {
    Mode mode = Mode::None;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
    unsigned port = 9000;
    std::vector<std::string> allowed_origins;  // --serve 에서만 쓰인다.
    std::string web_root;
    Mapping mapping = Mapping::Auto;
};

enum class ParseResult { Ok, ShowUsage, Error };

// argv 를 파싱한다. Ok 일 때만 out 을 덮어쓴다.
// 실패하면 error 에 사람이 읽을 이유를 담는다.
ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error);

std::string usageText();

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/cli/Options.cpp` 전체 교체**

```cpp
#include "cli/Options.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pulse {
namespace {

// strtoul 은 앞의 '-' 를 받아들여 랩어라운드시킨다. 그래서 숫자만으로
// 이루어진 문자열인지 먼저 확인한다. 그러지 않으면 "-5" 가 42억이 된다.
bool parseUnsigned(const char* text, unsigned& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    for (const char* c = text; *c != '\0'; ++c) {
        if (std::isdigit(static_cast<unsigned char>(*c)) == 0) {
            return false;
        }
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    if (value > std::numeric_limits<unsigned>::max()) {
        return false;
    }

    out = static_cast<unsigned>(value);
    return true;
}

}  // namespace

std::string usageText() {
    return
        "pulse-engine 0.2.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump  [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "  pulse-engine --json  [--interval-ms N] [--max-groups N]\n"
        "  pulse-engine --serve [--port N] [--web-root DIR] [--iterations N]\n"
        "                       [--interval-ms N] [--max-groups N] [--allow-origin URL]\n"
        "  Every mode also takes [--mapping auto|estimated|measured].\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --json            Print one snapshot as contract-shaped JSON and exit.\n"
        "  --serve           Stream snapshots over WebSocket on 127.0.0.1.\n"
        "  --port N          Listen port for --serve (default 9000).\n"
        "  --web-root DIR    Serve the built frontend from DIR (default: websocket only).\n"
        "  --allow-origin V  Allow an additional Origin for --serve (repeatable).\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000, minimum 1).\n"
        "  --iterations N    Stop after N snapshots for --dump and --serve (default: run "
        "until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n"
        "  --mapping M       Thread-to-core mapping. auto (default) measures with ETW when\n"
        "                    run as administrator and estimates otherwise; estimated never\n"
        "                    measures; measured exits if ETW cannot start.\n";
}

ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error) {
    Options parsed;

    const auto setMode = [&](Mode mode, const char* flag) {
        if (parsed.mode != Mode::None) {
            error = std::string("only one mode may be given, saw ") + flag;
            return false;
        }
        parsed.mode = mode;
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            if (!setMode(Mode::Dump, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--json") == 0) {
            if (!setMode(Mode::Json, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--serve") == 0) {
            if (!setMode(Mode::Serve, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--port") == 0) {
            unsigned value = 0;
            if (i + 1 >= argc || !parseUnsigned(argv[++i], value) || value == 0 ||
                value > 65535) {
                error = "invalid --port";
                return ParseResult::Error;
            }
            parsed.port = value;
        } else if (std::strcmp(arg, "--web-root") == 0) {
            if (i + 1 >= argc) {
                error = "invalid --web-root";
                return ParseResult::Error;
            }
            parsed.web_root = argv[++i];
            if (parsed.web_root.empty()) {
                error = "invalid --web-root";
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--allow-origin") == 0) {
            if (i + 1 >= argc) {
                error = "invalid --allow-origin";
                return ParseResult::Error;
            }
            parsed.allowed_origins.emplace_back(argv[++i]);
        } else if (std::strcmp(arg, "--interval-ms") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.interval_ms) ||
                // 0 을 허용하면 표본 사이에 잠들지 않는 바쁜 루프가 되어, 이 도구가
                // 측정하려는 바로 그 CPU 를 잡아먹는다. 게다가 시계가 전진하지 않아
                // CpuDelta 가 값을 내지 못해 모든 cpu_pct 가 '-' 로 나온다.
                parsed.interval_ms == 0) {
                error = "invalid --interval-ms";
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--iterations") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.iterations)) {
                error = "invalid --iterations";
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--max-groups") == 0) {
            unsigned value = 0;
            if (i + 1 >= argc || !parseUnsigned(argv[++i], value)) {
                error = "invalid --max-groups";
                return ParseResult::Error;
            }
            parsed.max_groups = value;
        } else if (std::strcmp(arg, "--mapping") == 0) {
            const char* value = i + 1 < argc ? argv[++i] : "";
            if (std::strcmp(value, "auto") == 0) {
                parsed.mapping = Mapping::Auto;
            } else if (std::strcmp(value, "estimated") == 0) {
                parsed.mapping = Mapping::Estimated;
            } else if (std::strcmp(value, "measured") == 0) {
                parsed.mapping = Mapping::Measured;
            } else {
                error = "invalid --mapping";
                return ParseResult::Error;
            }
        } else {
            error = std::string("unknown argument: ") + arg;
            return ParseResult::Error;
        }
    }

    if (parsed.mode == Mode::None) {
        return ParseResult::ShowUsage;
    }

    // 플래그는 어떤 순서로도 올 수 있으므로, 모드가 확정된 뒤인 여기서
    // 한 번에 검사한다. --json 은 늘 정확히 두 번 표본을 뜨므로
    // --iterations 는 조용히 무시하는 대신 거절한다.
    if (parsed.mode == Mode::Json && parsed.iterations != 0) {
        error = "--iterations cannot be combined with --json";
        return ParseResult::Error;
    }

    out = parsed;
    return ParseResult::Ok;
}

}  // namespace pulse
```

- [ ] **Step 5: 통과 확인 (GREEN)**

Run: `cmake --build --preset default && ./build/tests/Debug/pulse-tests.exe "[options]"`
Expected: 빌드 경고 0, 모두 통과.

- [ ] **Step 6: 전체 확인**

Run: `./build/tests/Debug/pulse-tests.exe`
Expected: `test cases: 194 | 194 passed`.

- [ ] **Step 7: 커밋**

```
git add src/cli/Options.h src/cli/Options.cpp tests/test_options.cpp
git commit -m "feat(engine): add --mapping auto|estimated|measured"
```

---

## Task 4: ETW 수집기와 연결

**Files:**
- Create: `engine/src/platform/windows/EtwSchedulerCollector.h`, `engine/src/platform/windows/EtwSchedulerCollector.cpp`
- Modify (전체 교체): `engine/src/platform/windows/WindowsSystemReader.h`, `engine/src/platform/windows/WindowsSystemReader.cpp`, `engine/src/main.cpp`, `engine/src/app/ServeApp.cpp`, `engine/src/network/Serializer.h`
- Modify: `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`, `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`
- Test: `engine/tests/test_etw_collector.cpp`

**Interfaces:**
- Consumes: Task 1 의 `RunTimeTable`, `RawThreadMapping`, `HostInfo::thread_mapping`. Task 3 의 `Mapping`, `Options::mapping`. 기존 `HelloInfo::thread_mapping` (`network/Serializer.h`).
- Produces:
  - `class EtwSchedulerCollector { static constexpr const wchar_t* kSessionName = L"PulseUniverse-Sched"; static std::unique_ptr<EtwSchedulerCollector> start(std::string& error); std::optional<RawThreadMapping> drain(); }` — 권한이 없으면 `start` 가 nullptr, `error == "ETW kernel events need administrator rights"`.
  - `explicit WindowsSystemReader(bool measure_threads = false)`, `bool measuringThreads() const`, `const std::string& mappingError() const`. `hostInfo().thread_mapping` 은 수집기가 있으면 `"measured"`.
  - `main`: `--mapping` 이 `estimated` 가 아니면 수집기를 시도하고 stderr 에 한 줄. `measured` 인데 실패하면 종료 코드 1.
  - hello 의 `capabilities.thread_mapping` = `HostInfo::thread_mapping`.

구현자의 셸은 관리자 권한이 아니다. `test_etw_collector.cpp` 의 두 테스트는 `SKIP` 으로 끝나야 정상이다 (`ETW collector unavailable: ETW kernel events need administrator rights`). 관리자 권한 확인은 컨트롤러가 한다.

- [ ] **Step 1: `engine/src/platform/windows/EtwSchedulerCollector.h`**

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/RunTimeTable.h"
#include "platform/RawTypes.h"

namespace pulse {

// ETW 스펙 7절. 전용 시스템 로거 세션으로 스레드 생성·종료와 문맥 전환(CSwitch)
// 이벤트를 받아 RunTimeTable 에 쌓는다. 관리자 권한이 필요하다.
// 이벤트는 별도 스레드의 ProcessTrace 가 받고, 샘플링 스레드는 drain() 으로 창을 가져간다.
class EtwSchedulerCollector {
public:
    // 세션 이름. 엔진이 죽으며 남긴 세션은 다음 start() 가 먼저 멈춘다.
    static constexpr const wchar_t* kSessionName = L"PulseUniverse-Sched";

    // 세션을 열고 소비 스레드를 띄운다. 실패하면 error 에 이유를 담고 nullptr.
    static std::unique_ptr<EtwSchedulerCollector> start(std::string& error);

    ~EtwSchedulerCollector();

    EtwSchedulerCollector(const EtwSchedulerCollector&) = delete;
    EtwSchedulerCollector& operator=(const EtwSchedulerCollector&) = delete;

    // 소비 스레드가 살아 있으면 창을 닫아 돌려준다. 멈췄으면 nullopt — 호출자는
    // 추정으로 돌아간다.
    std::optional<RawThreadMapping> drain();

    // 이벤트 콜백. ProcessTrace 스레드에서 불린다. 외부에서 부르지 않는다.
    void onThreadStarted(uint32_t pid, uint32_t tid);
    void onThreadEnded(uint32_t tid);
    void onContextSwitch(uint32_t core, uint32_t new_tid, int64_t ts);

private:
    EtwSchedulerCollector();

    // 세션의 유실 이벤트 수가 늘었으면 stderr 에 경고한다 (10초에 한 번).
    void warnIfEventsLost();

    uint64_t session_ = 0;   // TRACEHANDLE (StartTrace)
    uint64_t consumer_ = 0;  // TRACEHANDLE (OpenTrace)
    std::vector<unsigned char> properties_;  // EVENT_TRACE_PROPERTIES + 이름
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    RunTimeTable table_;
    unsigned long events_lost_ = 0;
    int64_t last_warning_ms_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 2: `engine/src/platform/windows/EtwSchedulerCollector.cpp`**

```cpp
#include "platform/windows/EtwSchedulerCollector.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <evntcons.h>
#include <evntrace.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace pulse {
namespace {

// 커널 Thread 이벤트 공급자 (MOF 클래스 Thread_V2 ~ V5 와 CSwitch 가 이 GUID 로 온다).
constexpr GUID kThreadProvider = {
    0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};

// 세션 식별용. 시스템 로거 모드 세션은 SystemTraceControlGuid 가 아니라 자기 GUID 를 쓴다.
constexpr GUID kSessionGuid = {
    0x6a3f1c52, 0x8e0b, 0x4d47, {0x9a, 0x61, 0x2f, 0x5b, 0x0c, 0x7e, 0x94, 0xd3}};

constexpr UCHAR kOpcodeThreadStart = 1;
constexpr UCHAR kOpcodeThreadEnd = 2;
constexpr UCHAR kOpcodeThreadDcStart = 3;
constexpr UCHAR kOpcodeContextSwitch = 36;

// 측정(ETW 스펙 2절): 부하 중 초당 약 15만 건(약 7 MB). 커널은 코어마다 버퍼를 따로
// 잡으므로 상주 메모리는 대략 코어 수 × 버퍼 크기 × 2(세션과 소비자)다. 1 MB 버퍼에서는
// 28 코어 기계의 작업 집합이 +59 MB 였다. 코어 하나가 초당 쓰는 양은 수백 KB 라 256 KB 로
// 충분하다.
constexpr ULONG kBufferKb = 256;
constexpr ULONG kMinBuffers = 16;
constexpr ULONG kMaxBuffers = 128;
// 이벤트가 적을 때도 1초 안에 전달되게 한다.
constexpr ULONG kFlushSeconds = 1;

// ProcessTrace 는 PROCESS_TRACE_MODE_RAW_TIMESTAMP 없이 열면 시각을 FILETIME(100ns)
// 으로 바꿔 준다.
constexpr double kTicksPerSecond = 1e7;

// 유실 경고를 이보다 자주 찍지 않는다.
constexpr int64_t kLostWarningIntervalMs = 10000;

// EVENT_TRACE_PROPERTIES 뒤에 세션 이름을 붙인 버퍼. StartTrace·ControlTrace 가
// 이 모양을 요구한다.
std::vector<unsigned char> makeProperties() {
    const size_t name_bytes =
        (std::wcslen(EtwSchedulerCollector::kSessionName) + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(EVENT_TRACE_PROPERTIES) + name_bytes, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    props->Wnode.BufferSize = static_cast<ULONG>(buffer.size());
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;  // QPC
    props->Wnode.Guid = kSessionGuid;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    props->EnableFlags =
        EVENT_TRACE_FLAG_PROCESS | EVENT_TRACE_FLAG_THREAD | EVENT_TRACE_FLAG_CSWITCH;
    props->BufferSize = kBufferKb;
    props->MinimumBuffers = kMinBuffers;
    props->MaximumBuffers = kMaxBuffers;
    props->FlushTimer = kFlushSeconds;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return buffer;
}

EVENT_TRACE_PROPERTIES* asProperties(std::vector<unsigned char>& buffer) {
    return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
}

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint32_t readU32(const unsigned char* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// ProcessTrace 스레드에서 불린다. Thread 공급자의 Start·DCStart·End·CSwitch 만 본다.
// 페이로드: Thread_V* 는 ProcessId, TThreadId 로 시작하고, CSwitch 는 NewThreadId 로 시작한다.
void WINAPI onEventRecord(PEVENT_RECORD record) {
    auto* collector = static_cast<EtwSchedulerCollector*>(record->UserContext);
    const EVENT_HEADER& header = record->EventHeader;
    if (collector == nullptr || !IsEqualGUID(header.ProviderId, kThreadProvider)) {
        return;
    }
    const auto* data = static_cast<const unsigned char*>(record->UserData);
    const USHORT length = record->UserDataLength;

    switch (header.EventDescriptor.Opcode) {
        case kOpcodeThreadStart:
        case kOpcodeThreadDcStart:
            if (length >= 8) {
                collector->onThreadStarted(readU32(data, 0), readU32(data, 4));
            }
            break;
        case kOpcodeThreadEnd:
            if (length >= 8) {
                collector->onThreadEnded(readU32(data, 4));
            }
            break;
        case kOpcodeContextSwitch:
            if (length >= 4) {
                collector->onContextSwitch(GetEventProcessorIndex(record), readU32(data, 0),
                                           header.TimeStamp.QuadPart);
            }
            break;
        default:
            break;
    }
}

}  // namespace

EtwSchedulerCollector::EtwSchedulerCollector()
    : properties_(makeProperties()), table_(kTicksPerSecond) {}

std::unique_ptr<EtwSchedulerCollector> EtwSchedulerCollector::start(std::string& error) {
    std::unique_ptr<EtwSchedulerCollector> collector(new EtwSchedulerCollector());

    // 엔진이 강제 종료되면 세션이 커널에 남는다. 같은 이름으로 다시 열기 전에 멈춘다.
    std::vector<unsigned char> stale = makeProperties();
    ::ControlTraceW(0, kSessionName, asProperties(stale), EVENT_TRACE_CONTROL_STOP);

    TRACEHANDLE session = 0;
    const ULONG started =
        ::StartTraceW(&session, kSessionName, asProperties(collector->properties_));
    if (started != ERROR_SUCCESS) {
        error = started == ERROR_ACCESS_DENIED
                    ? "ETW kernel events need administrator rights"
                    : "StartTrace failed with error " + std::to_string(started);
        return nullptr;
    }
    collector->session_ = session;

    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = const_cast<LPWSTR>(kSessionName);
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = onEventRecord;
    logfile.Context = collector.get();
    const TRACEHANDLE consumer = ::OpenTraceW(&logfile);
    if (consumer == INVALID_PROCESSTRACE_HANDLE) {
        error = "OpenTrace failed with error " + std::to_string(::GetLastError());
        return nullptr;  // 소멸자가 세션을 멈춘다
    }
    collector->consumer_ = consumer;

    collector->running_ = true;
    EtwSchedulerCollector* self = collector.get();
    collector->thread_ = std::thread([self] {
        TRACEHANDLE handle = self->consumer_;
        // 세션이 멈추거나 CloseTrace 가 불릴 때까지 돌아오지 않는다.
        ::ProcessTrace(&handle, 1, nullptr, nullptr);
        self->running_ = false;
    });
    return collector;
}

EtwSchedulerCollector::~EtwSchedulerCollector() {
    if (session_ != 0) {
        ::ControlTraceW(session_, nullptr, asProperties(properties_), EVENT_TRACE_CONTROL_STOP);
    }
    if (consumer_ != 0) {
        ::CloseTrace(consumer_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void EtwSchedulerCollector::onThreadStarted(uint32_t pid, uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.threadStarted(pid, tid);
}

void EtwSchedulerCollector::onThreadEnded(uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.threadEnded(tid);
}

void EtwSchedulerCollector::onContextSwitch(uint32_t core, uint32_t new_tid, int64_t ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.contextSwitch(core, new_tid, ts);
}

std::optional<RawThreadMapping> EtwSchedulerCollector::drain() {
    if (!running_) {
        return std::nullopt;
    }
    warnIfEventsLost();
    std::lock_guard<std::mutex> lock(mutex_);
    return table_.drain();
}

void EtwSchedulerCollector::warnIfEventsLost() {
    std::vector<unsigned char> query = makeProperties();
    if (::ControlTraceW(session_, nullptr, asProperties(query), EVENT_TRACE_CONTROL_QUERY) !=
        ERROR_SUCCESS) {
        return;
    }
    const unsigned long lost = asProperties(query)->EventsLost;
    if (lost <= events_lost_) {
        return;
    }
    const int64_t now = steadyMs();
    if (last_warning_ms_ != 0 && now - last_warning_ms_ < kLostWarningIntervalMs) {
        return;
    }
    std::fprintf(stderr, "thread mapping: ETW dropped %lu events so far\n", lost);
    events_lost_ = lost;
    last_warning_ms_ = now;
}

}  // namespace pulse
```

- [ ] **Step 3: `engine/src/platform/windows/WindowsSystemReader.h` 전체 교체**

```cpp
#pragma once

#include <memory>
#include <string>

#include "platform/ISystemReader.h"
#include "platform/windows/EtwSchedulerCollector.h"

namespace pulse {

// Toolhelp32 로 프로세스를 열거하고, PSAPI 로 메모리를, PDH 로 코어별 부하를 읽는다.
// PDH 카운터는 두 번째 수집부터 값이 나오므로 생성자에서 한 번 수집해 둔다.
// measure_threads 가 참이면 ETW 수집기를 띄워 표본마다 실측 스레드-코어 매핑을
// 싣는다 (ETW 스펙 7절). 띄우지 못하면 이유를 mappingError() 에 남기고 추정으로 간다.
class WindowsSystemReader final : public ISystemReader {
public:
    explicit WindowsSystemReader(bool measure_threads = false);
    ~WindowsSystemReader() override;

    WindowsSystemReader(const WindowsSystemReader&) = delete;
    WindowsSystemReader& operator=(const WindowsSystemReader&) = delete;

    RawSample read() override;
    unsigned coreCount() const override;
    HostInfo hostInfo() const override;

    // ETW 수집기가 돌고 있는가.
    bool measuringThreads() const;
    // 수집기를 띄우지 못한 이유. 띄웠거나 시도하지 않았으면 빈 문자열.
    const std::string& mappingError() const;

private:
    void* query_ = nullptr;    // PDH_HQUERY
    void* counter_ = nullptr;  // PDH_HCOUNTER
    unsigned core_count_ = 0;
    std::unique_ptr<EtwSchedulerCollector> collector_;
    std::string mapping_error_;
};

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/platform/windows/WindowsSystemReader.cpp` 전체 교체**

```cpp
#include "platform/windows/WindowsSystemReader.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

namespace pulse {
namespace {

// FILETIME 은 100ns 단위다.
uint64_t fileTimeTo100ns(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

// Windows epoch(1601-01-01) 과 Unix epoch(1970-01-01) 의 차이, 100ns 단위.
constexpr uint64_t kUnixEpochOffset100ns = 116444736000000000ull;

uint64_t fileTimeToUnixMs(const FILETIME& ft) {
    const uint64_t raw = fileTimeTo100ns(ft);
    if (raw <= kUnixEpochOffset100ns) {
        return 0;
    }
    return (raw - kUnixEpochOffset100ns) / 10000ull;
}

uint64_t nowUnixMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string toUtf8(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

bool isAllDigits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

unsigned logicalCoreCount() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwNumberOfProcessors == 0 ? 1u : info.dwNumberOfProcessors;
}

// 관리자 권한 토큰이라도 SeDebugPrivilege 는 기본적으로 비활성이다.
// 활성화하면 보호되지 않은 다른 세션의 프로세스까지 열 수 있다.
// 권한이 없으면 조용히 실패한다 — 비권한 실행에서는 정상이다.
void enableDebugPrivilege() {
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token) == 0) {
        return;
    }

    LUID luid{};
    if (::LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid) != 0) {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ::AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    }

    ::CloseHandle(token);
}

// 현재 프로세스가 승격된 토큰으로 도는지 조회한다.
// 계약서 4.3 절의 host.elevated 를 채우고, 프론트엔드가
// 비권한 실행 시 일부 프로세스의 메모리가 0 인 이유를 안내할 수 있게 한다.
bool isProcessElevated() {
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == 0) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &returned) != 0;
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

// 핸들에서 시각/경로를 읽는다. 메모리는 전체 접근 경로에서만 읽는다 -
// PROCESS_VM_READ 없이 GetProcessMemoryInfo 를 부르면 실패하기 때문이다.
void readTimesAndPath(HANDLE handle, RawProcess& process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (::GetProcessTimes(handle, &creation, &exit, &kernel, &user) != 0) {
        process.start_time_ms = fileTimeToUnixMs(creation);
        const uint64_t busy_100ns = fileTimeTo100ns(kernel) + fileTimeTo100ns(user);
        process.cpu_cumulative_ms = busy_100ns / 10000ull;
    }

    wchar_t path[MAX_PATH] = {};
    DWORD path_len = MAX_PATH;
    if (::QueryFullProcessImageNameW(handle, 0, path, &path_len) != 0) {
        process.image_path = toUtf8(path);
    }
}

// 프로세스 핸들을 열어 얻을 수 있는 것만 채운다.
// 전체 접근(PROCESS_VM_READ 포함)이 거부되면 QUERY_LIMITED 만으로 다시
// 시도한다 - 시각과 경로는 그것만으로도 읽히고, LifecycleTracker 의
// pid 재사용 판별과 GroupBuilder 의 재사용 가드가 start_time_ms 에 기대기
// 때문에 완전히 포기하는 것보다 낫다. 메모리 질의만 전체 접근 경로에서
// 시도한다. 어느 경로든 핸들은 반드시 닫는다.
void enrichFromHandle(RawProcess& process) {
    HANDLE handle = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, process.pid);
    if (handle != nullptr) {
        readTimesAndPath(handle, process);

        // 작업 관리자의 "메모리" 열은 private working set(상주 + 비공유)이지만
        // GetProcessMemoryInfo 로는 얻을 수 없다. 선택지는 둘뿐이고 둘 다 과대 계상한다.
        // WorkingSetSize 는 공유 페이지를 그룹 구성원 수만큼 중복 계산하고,
        // PROCESS_MEMORY_COUNTERS_EX::PrivateUsage 는 상주하지 않는 커밋까지 포함한다.
        // 실측에서 후자가 더 크게 벗어나 전자를 쓴다.
        // 실제 private working set 은 NtQueryInformationProcess 가 필요하다 — 후속 과제.
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (::GetProcessMemoryInfo(handle, &counters, sizeof(counters)) != 0) {
            process.mem_bytes = counters.WorkingSetSize;
        }

        ::CloseHandle(handle);
        return;
    }

    handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
    if (handle == nullptr) {
        return;
    }

    readTimesAndPath(handle, process);
    ::CloseHandle(handle);
}

// 세션 0 은 서비스와 시스템 프로세스 전용이다. 사용자가 띄운 것은 세션 1 이상이다.
Account accountForPid(uint32_t pid) {
    DWORD session_id = 0;
    if (::ProcessIdToSessionId(pid, &session_id) == 0) {
        return Account::System;
    }
    return session_id == 0 ? Account::System : Account::User;
}

}  // namespace

WindowsSystemReader::WindowsSystemReader(bool measure_threads)
    : core_count_(logicalCoreCount()) {
    enableDebugPrivilege();

    if (measure_threads) {
        collector_ = EtwSchedulerCollector::start(mapping_error_);
    }

    PDH_HQUERY query = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return;
    }
    query_ = query;

    PDH_HCOUNTER counter = nullptr;
    // English 카운터 이름을 쓰면 OS 표시 언어와 무관하게 동작한다.
    if (::PdhAddEnglishCounterW(query, L"\\Processor(*)\\% Processor Time", 0,
                                &counter) != ERROR_SUCCESS) {
        ::PdhCloseQuery(query);
        query_ = nullptr;
        return;
    }
    counter_ = counter;

    // PDH 는 두 번째 수집부터 값을 낸다. 첫 수집을 여기서 해 둔다.
    ::PdhCollectQueryData(query);
}

WindowsSystemReader::~WindowsSystemReader() {
    if (query_ != nullptr) {
        ::PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
    }
}

unsigned WindowsSystemReader::coreCount() const {
    return core_count_;
}

HostInfo WindowsSystemReader::hostInfo() const {
    HostInfo info;
    info.os = "Windows";
    info.elevated = isProcessElevated();
    info.thread_mapping = collector_ != nullptr ? "measured" : "estimated";
    return info;
}

bool WindowsSystemReader::measuringThreads() const {
    return collector_ != nullptr;
}

const std::string& WindowsSystemReader::mappingError() const {
    return mapping_error_;
}

RawSample WindowsSystemReader::read() {
    RawSample sample;
    sample.timestamp_ms = nowUnixMs();

    // 실측 매핑 창. 수집 스레드가 멈췄으면 비어 있고, 흐름은 추정으로 돌아간다.
    if (collector_ != nullptr) {
        sample.thread_mapping = collector_->drain();
    }

    // --- 프로세스 열거 ---
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry) != 0) {
            do {
                // pid 0 은 System Idle Process 다. 실제 프로세스가 아니므로 건너뛴다.
                if (entry.th32ProcessID == 0) {
                    continue;
                }
                RawProcess process;
                process.pid = entry.th32ProcessID;
                process.ppid = entry.th32ParentProcessID;
                process.name = toUtf8(entry.szExeFile);
                process.thread_count = entry.cntThreads;
                process.account = accountForPid(process.pid);
                enrichFromHandle(process);
                sample.processes.push_back(std::move(process));
            } while (::Process32NextW(snapshot, &entry) != 0);
        }
        ::CloseHandle(snapshot);
    }

    // --- 코어별 부하 ---
    sample.cores.reserve(core_count_);
    bool cores_filled = false;
    if (query_ != nullptr && counter_ != nullptr) {
        const auto query = static_cast<PDH_HQUERY>(query_);
        const auto counter = static_cast<PDH_HCOUNTER>(counter_);
        if (::PdhCollectQueryData(query) == ERROR_SUCCESS) {
            DWORD buffer_size = 0;
            DWORD item_count = 0;
            PDH_STATUS status = ::PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
            if (status == PDH_MORE_DATA && buffer_size > 0) {
                std::vector<unsigned char> buffer(buffer_size);
                auto* items =
                    reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
                if (::PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size,
                                                   &item_count, items) == ERROR_SUCCESS) {
                    for (DWORD i = 0; i < item_count; ++i) {
                        const std::string name = toUtf8(items[i].szName);
                        // "_Total" 은 합계 항목이라 코어가 아니므로 숫자만으로
                        // 이루어진 이름만 받아 걸러낸다. 이 레거시 \Processor(*)
                        // 카운터는 첫 번째 프로세서 그룹만 보고하므로, 논리
                        // 프로세서가 64개를 넘는 장비에서는 그 이상이 보이지
                        // 않는다 (">64 코어" 이름 표기는 "Processor Information"
                        // PDH 오브젝트 얘기지 이 카운터 얘기가 아니다) - 뒤로
                        // 미룬 사항으로 남겨둔다.
                        if (!isAllDigits(name)) {
                            continue;
                        }
                        RawCore core;
                        core.id = static_cast<uint32_t>(std::stoul(name));
                        double pct = items[i].FmtValue.doubleValue;
                        if (pct < 0.0) pct = 0.0;
                        if (pct > 100.0) pct = 100.0;
                        core.pct = pct;
                        sample.cores.push_back(core);
                    }
                    cores_filled = !sample.cores.empty();
                }
            }
        }
    }
    if (!cores_filled) {
        // PDH 가 아직 값을 내지 않았다. 0 으로 채워 코어 수 불변식을 지킨다.
        sample.cores.clear();
        for (unsigned i = 0; i < core_count_; ++i) {
            sample.cores.push_back(RawCore{i, 0.0});
        }
    }

    // PDH 는 인스턴스 이름을 문자열 순으로 돌려주므로 정렬하지 않으면
    // 0, 1, 10, 11, ... 19, 2, 20 순으로 나온다. 표를 눈으로 대조할 때
    // 코어를 찾을 수 없게 되므로 id 순으로 정렬한다.
    std::sort(sample.cores.begin(), sample.cores.end(),
              [](const RawCore& a, const RawCore& b) { return a.id < b.id; });

    // --- 메모리 ---
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory) != 0) {
        sample.memory.total_bytes = memory.ullTotalPhys;
        sample.memory.used_bytes = memory.ullTotalPhys - memory.ullAvailPhys;
    }

    return sample;
}

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/main.cpp` 전체 교체**

```cpp
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "app/EngineLoop.h"
#include "app/ServeApp.h"
#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "network/Serializer.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

int runDump(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::EngineLoop loop(reader, cfg, [](const pulse::SystemSnapshot& snapshot) {
        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);
    });
    loop.run();

    if (!loop.error().empty()) {
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }
    return 0;
}

int runJson(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = 2;  // 첫 스냅샷은 CPU 델타가 없어 버린다 — 계약서 6.1 절
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::SystemSnapshot latest;
    pulse::EngineLoop loop(reader, cfg,
                           [&](const pulse::SystemSnapshot& s) { latest = s; });
    loop.run();

    if (!loop.error().empty()) {
        std::fprintf(stderr, "sampling failed: %s\n", loop.error().c_str());
        return 1;
    }

    std::printf("%s\n", pulse::serializeSnapshot(latest).c_str());
    return 0;
}

int runServe(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::ServeConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.max_groups = options.max_groups;
    cfg.server.port = static_cast<unsigned short>(options.port);
    if (!options.allowed_origins.empty()) {
        for (const auto& origin : options.allowed_origins) {
            cfg.server.allowed_origins.push_back(origin);
        }
    }
    if (!options.web_root.empty()) {
        std::error_code dir_ec;
        if (!std::filesystem::is_directory(options.web_root, dir_ec) || dir_ec) {
            std::fprintf(stderr, "web root is not a directory: %s\n",
                         options.web_root.c_str());
            return 2;
        }
        cfg.server.web_root = options.web_root;
    }

    const bool has_web_root = !cfg.server.web_root.empty();
    const pulse::ServeResult result =
        pulse::runServe(reader, cfg, [has_web_root](unsigned short port) {
            std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                        static_cast<unsigned>(port));
            if (has_web_root) {
                std::printf("open http://127.0.0.1:%u/ in a browser\n",
                            static_cast<unsigned>(port));
            }
            std::fflush(stdout);
        });

    if (!result.message.empty()) {
        std::fprintf(stderr, "%s\n", result.message.c_str());
    }
    return result.exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    pulse::Options options;
    std::string error;

    switch (pulse::parseOptions(argc, argv, options, error)) {
        case pulse::ParseResult::Ok:
            break;
        case pulse::ParseResult::ShowUsage:
            std::printf("%s", pulse::usageText().c_str());
            return 0;
        case pulse::ParseResult::Error:
            std::printf("%s\n\n%s", error.c_str(), pulse::usageText().c_str());
            return 2;
    }

    // ETW 스펙 8절. auto 와 measured 는 실측을 시도한다. 결과는 stderr 에 한 줄 남긴다 —
    // --json 의 stdout 을 더럽히지 않는다.
    const bool want_measured = options.mapping != pulse::Mapping::Estimated;
    pulse::WindowsSystemReader reader(want_measured);
    if (want_measured) {
        if (reader.measuringThreads()) {
            std::fprintf(stderr, "thread mapping: measured (ETW)\n");
        } else if (options.mapping == pulse::Mapping::Measured) {
            std::fprintf(stderr, "thread mapping: cannot measure - %s\n",
                         reader.mappingError().c_str());
            return 1;
        } else {
            std::fprintf(stderr, "thread mapping: estimated - %s\n",
                         reader.mappingError().c_str());
        }
    }

    switch (options.mode) {
        case pulse::Mode::Dump:
            return runDump(reader, options);
        case pulse::Mode::Json:
            return runJson(reader, options);
        case pulse::Mode::Serve:
            return runServe(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
```

- [ ] **Step 6: `engine/src/app/ServeApp.cpp` 전체 교체**

```cpp
#include "app/ServeApp.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>

#include <cstdint>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

#include "app/EngineLoop.h"
#include "network/Serializer.h"
#include "network/WebSocketServer.h"

namespace pulse {

namespace {

// 프로세스의 한 번의 실행을 식별하는 16자리 소문자 16진수 문자열을 만든다.
// std::random_device 로 시드해 std::mt19937_64 를 돌린다 — 플랫폼 API 를
// 쓰지 않아 src/app/ 에 <windows.h> 가 들어오지 않는다.
std::string generateSessionId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    const uint64_t value = gen();

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

}  // namespace

ServeResult runServe(ISystemReader& reader, const ServeConfig& cfg,
                     std::function<void(unsigned short)> on_listening) {
    namespace net = boost::asio;

    net::io_context ioc;

    std::unique_ptr<WebSocketServer> server;
    try {
        server = std::make_unique<WebSocketServer>(ioc, cfg.server);
    } catch (const std::exception& e) {
        return {2, "cannot listen on 127.0.0.1:" + std::to_string(cfg.server.port) +
                        " — " + e.what()};
    }

    if (on_listening) {
        on_listening(server->port());
    }

    HelloInfo hello;
    hello.interval_ms = cfg.interval_ms;
    hello.core_count = reader.coreCount();
    const HostInfo host = reader.hostInfo();
    hello.os = host.os;
    hello.elevated = host.elevated;
    hello.thread_mapping = host.thread_mapping;
    hello.session = generateSessionId();
    server->setHello(std::make_shared<const std::string>(serializeHello(hello)));

    EngineLoopConfig loop_cfg;
    loop_cfg.interval_ms = cfg.interval_ms;
    loop_cfg.iterations = cfg.iterations;
    loop_cfg.aggregator.filter.max_groups = cfg.max_groups;

    EngineLoop loop(reader, loop_cfg, [&](const SystemSnapshot& snapshot) {
        server->broadcast(
            std::make_shared<const std::string>(serializeSnapshot(snapshot)));
    });

    // Ctrl+C 로 종료한다. 신호는 io_context 에서 받고 샘플링 루프에 정지를 알린다.
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        loop.stop();
        server->stop();
    });

    std::thread sampler([&] {
        loop.run();

        // ioc.stop() 을 부르지 않는다. run() 은 남은 작업이 없을 때 돌아오고,
        // post 된 채 아직 실행되지 않은 핸들러도 작업으로 친다. 따라서 stop()
        // 이 post 한 세션 정리는 반드시 실행된 뒤에야 run() 이 돌아온다.
        // io_context 를 살려두는 것은 signal_set 의 대기뿐이므로 그것만 취소한다.
        server->stop();
        net::post(ioc, [&] {
            boost::system::error_code ignored;
            signals.cancel(ignored);
        });
    });

    ioc.run();
    loop.stop();
    sampler.join();

    if (!loop.error().empty()) {
        return {1, "sampling failed: " + loop.error()};
    }
    return {0, ""};
}

}  // namespace pulse
```

- [ ] **Step 7: `engine/src/network/Serializer.h` 전체 교체 (주석)**

```cpp
#pragma once

#include <string>

#include "core/Snapshot.h"

namespace pulse {

// 계약서 4.3~4.4 절 메시지의 v 필드.
inline constexpr int kProtocolVersion = 1;

struct HelloInfo {
    unsigned interval_ms = 1000;
    unsigned core_count = 0;
    bool elevated = false;
    std::string os;
    // 엔진이 시작할 때 ETW 수집기가 돌았으면 "measured", 아니면 "estimated"
    // (HostInfo::thread_mapping). core/Snapshot.h 의 Flow::source 는 스냅샷마다의
    // 실제 출처다 — 수집기가 살아 있는 동안 두 값은 같다 (ETW 스펙 8절).
    std::string thread_mapping = "estimated";
    // 엔진 프로세스의 한 번의 실행을 식별한다. 재시작할 때마다 바뀐다.
    // 클라이언트는 이 값으로 재연결(같은 세션)과 재시작(다른 세션)을 구별한다.
    std::string session;
};

// 계약서 4.3 절 형식.
std::string serializeHello(const HelloInfo& info);

// 계약서 4.4 절 형식. type 과 v 는 SystemSnapshot 에 없고 여기서 붙인다 —
// 구조체는 도메인 값이고 프로토콜 봉투는 전송 계층의 관심사다.
std::string serializeSnapshot(const SystemSnapshot& snapshot);

}  // namespace pulse
```

- [ ] **Step 8: `engine/tests/test_etw_collector.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "platform/windows/EtwSchedulerCollector.h"

using namespace pulse;

// ETW 스펙 11절. 관리자 권한으로 돌 때만 실제 세션을 연다. 권한이 없으면 SKIP 이다.
TEST_CASE("the ETW collector measures this process's run time when elevated", "[etw]") {
    std::string error;
    std::unique_ptr<EtwSchedulerCollector> collector = EtwSchedulerCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW collector unavailable: " + error);
    }

    // 첫 창은 세션을 켠 뒤부터다. 비워 두고 새 창을 잰다.
    collector->drain();

    // 이 스레드가 0.5 초 동안 코어를 쓴다.
    const auto busy_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    volatile unsigned long long spin = 0;
    while (std::chrono::steady_clock::now() < busy_until) {
        spin = spin + 1;
    }
    // 이벤트는 버퍼 플러시(1 초)마다 전달된다. 넉넉히 기다린다.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    const std::optional<RawThreadMapping> mapping = collector->drain();
    REQUIRE(mapping.has_value());
    REQUIRE(mapping->window_seconds > 1.0);

    const uint32_t self = ::GetCurrentProcessId();
    double seconds = 0.0;
    for (const RawRunTime& run : mapping->run_times) {
        if (run.pid == self) {
            seconds += run.seconds;
        }
    }
    CAPTURE(seconds, mapping->run_times.size());
    REQUIRE(seconds > 0.4);
}

TEST_CASE("the ETW session is gone after the collector is destroyed", "[etw]") {
    std::string error;
    std::unique_ptr<EtwSchedulerCollector> collector = EtwSchedulerCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW collector unavailable: " + error);
    }
    collector.reset();

    // 같은 이름으로 곧바로 다시 열 수 있어야 한다 (남은 세션이 없다).
    std::unique_ptr<EtwSchedulerCollector> again = EtwSchedulerCollector::start(error);
    REQUIRE(again != nullptr);
}
```

- [ ] **Step 9: CMake 에 등록**

`engine/CMakeLists.txt` — `add_library(pulse_core` 목록에서 `src/platform/windows/WindowsSystemReader.cpp` 다음 줄에:

```
  src/platform/windows/EtwSchedulerCollector.cpp
```

같은 파일의 링크 줄을 이렇게 바꾼다 (ETW 제어 API 는 advapi32 에 있다):

```
  target_link_libraries(pulse_core PUBLIC pdh psapi ws2_32 mswsock advapi32)
```

`engine/tests/CMakeLists.txt` — `test_windows_reader.cpp` 다음 줄에:

```
  test_etw_collector.cpp
```

- [ ] **Step 10: 계약서 정정 두 곳**

`docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`

(a) 4.3절 끝, `**정정 (M2 구현 후).**` 문단 바로 다음(그리고 `### 4.4 메시지: snapshot` 앞)에 빈 줄을 두고 이 문단을 넣는다:

```text
**정정 (ETW 확장 후).** 두 값의 뜻을 나눈다. `capabilities.thread_mapping` 은 엔진이 시작할 때 ETW 수집기가 돌았는지다 (`--mapping auto|estimated|measured`, 관리자 권한이 없으면 `auto` 는 `"estimated"`). `flows[].source` 는 스냅샷마다의 실제 출처다. 수집기가 살아 있는 동안 두 값은 같다. 수집기가 도중에 멈추면 이후 흐름은 `"estimated"` 로 돌아가고 hello 는 `"measured"` 로 남는다 — 이 경우에만 어긋난다. 프론트엔드는 흐름마다 `source` 를 읽으므로 영향이 없다. 설계: `2026-09-30-etw-thread-mapping-design.md`.
```

(b) 6.2절, `측정값의 형태가 확정되기 전에 미리 자리를 비워두는 것은 이득이 없으므로, M1 시점에는 이 제약을 기록만 하고 구조를 바꾸지 않는다.` 로 끝나는 문단 바로 다음에 빈 줄을 두고 이 문단을 넣는다:

```text
**정정 (ETW 확장 후).** 위 정정이 예고한 세 자리를 만들었다. `RawSample::thread_mapping`(한 창 동안 (pid, 코어)별 실행 시간), 이를 흐름으로 바꾸는 `core/MeasuredFlows`, 그리고 `DataAggregator` 의 분기다. 실측 흐름의 `weight` 는 "그룹 구성원이 그 코어에서 돈 시간 ÷ 창 길이" 이고, 걸러내기(5% 미만 버림, 그룹당 상위 4개)는 추정과 같다. 시각화 레이어는 바뀌지 않았다. 설계: `2026-09-30-etw-thread-mapping-design.md`.
```

- [ ] **Step 11: 빌드와 전체 확인**

Run: `cmake --build --preset default && ./build/tests/Debug/pulse-tests.exe`
Expected: 빌드 경고 0, `test cases: 196 | 194 passed | 2 skipped` (두 SKIP 은 `[etw]`).

Run: `./build/Debug/pulse-engine.exe --json --mapping estimated > /dev/null; echo $?`
Expected: `0`, stderr 에 아무것도 없다.

Run: `./build/Debug/pulse-engine.exe --json --mapping measured; echo $?`
Expected: stdout 비어 있음, stderr 에 `thread mapping: cannot measure - ETW kernel events need administrator rights`, 종료 코드 `1`.

Run: `./build/Debug/pulse-engine.exe --json 2>&1 >/dev/null | head -1`
Expected: `thread mapping: estimated - ETW kernel events need administrator rights`.

- [ ] **Step 12: 관리자 권한 확인은 하지 않는다**

UAC 를 띄우지 않는다. 컨트롤러가 관리자 권한으로 `[etw]` 테스트와 `--serve` 를 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 13: 커밋**

```
git add src/platform/windows/EtwSchedulerCollector.h src/platform/windows/EtwSchedulerCollector.cpp src/platform/windows/WindowsSystemReader.h src/platform/windows/WindowsSystemReader.cpp src/main.cpp src/app/ServeApp.cpp src/network/Serializer.h tests/test_etw_collector.cpp CMakeLists.txt tests/CMakeLists.txt ../docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md
git commit -m "feat(engine): measure thread-to-core mapping with an ETW scheduler session when elevated"
```

---

## 완료 조건 (컨트롤러가 확인)

- [ ] 빌드 경고 0, `pulse-tests` 196 (권한 없이 194 통과 + `[etw]` 2 SKIP).
- [ ] `engine/src/core/` 에 Win32 헤더가 없다.
- [ ] 관리자 권한 (UAC):
  - `pulse-tests "[etw]"` 2개 통과.
  - `--json --mapping measured`: flows 가 모두 `source: "measured"`, 그룹마다 코어가 다르다.
  - `--serve --allow-origin http://localhost:5173` + dev 서버: hello `capabilities.thread_mapping == "measured"`, 배지 `elevated`, 스냅샷 flows 가 `measured`.
  - Release 엔진 CPU·작업 집합 (시제품: measured 17~22 ms/s, 29 MB), 버퍼 유실 0.
  - `--iterations` 로 끝난 뒤 `logman query -ets` 에 `PulseUniverse-Sched` 가 없다.
- [ ] 권한 없이 `--serve`: 지금과 같다 (`estimated`, stderr 한 줄).
