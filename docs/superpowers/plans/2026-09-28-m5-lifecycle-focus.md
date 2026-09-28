# M5 — 생성·종료 연출, Focus, 카메라 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 프로세스의 실제 생성·종료가 파티클로 형성·붕괴하고, 순위 변동은 조용히 페이드하며, 천체를 클릭하면 카메라가 다가가 자식 프로세스가 위성으로 펼쳐진다. 엔진은 목록을 안정화하고 정확히 1초마다 샘플한다.

**Architecture:** 엔진은 직전 목록에 유지 보너스(×2.0)를 주고 고정 주기로 깨어난다. 프론트엔드는 순수 모듈(`visual/`)이 존재 추적(PresenceTracker), lifecycle 소비, 버스트 궤적, 위성 궤도, 카메라 자세를 계산하고, 장면(`scene/`)은 매 프레임 그 결과로 ref 를 바꾼다. GSAP 은 Focus 전환 진행도 하나만 트윈한다.

**Tech Stack:** C++20 / MSVC / Catch2 · three 0.186 / @react-three/fiber 9.8 / drei 10 / gsap 3.15 / zustand 5 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-28-m5-lifecycle-focus-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`. M4 스펙: `docs/superpowers/specs/2026-09-28-m4-universe-scene-design.md`.
- 작업 브랜치는 `feature/m5-lifecycle-focus` 다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다. 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 가장 안전한 방법은 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이다. "전체 교체" 라고 적힌 파일은 파일 전체를 블록 내용으로 바꾼다.
- 엔진: C++20, MSVC `/W4 /permissive- /utf-8` 경고 0, 네임스페이스 `pulse`, `src/core/`·`src/app/` 에 `<windows.h>` 금지.
- 프론트엔드: `cpu_pct` 의 `null`(모름)과 `0`(측정된 0)을 절대 같게 취급하지 않는다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `@react-three/*`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다.
- 프레임 값은 React 상태에 넣지 않는다. 시계는 `performance.now()` 다.
- GSAP 은 Focus 전환 진행도 트윈에만 쓴다. 노드마다 GSAP 타임라인을 두지 않는다 (계약서 9절).
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료는 BLOCKED 로 보고한다.** 파라미터·sleep·타임아웃·기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다. 계획의 코드로 테스트가 실패해도 마찬가지다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` 를 붙인다. `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
웹 명령:   C:\dev\pulse-uni\web 에서 실행
```

엔진 빌드·테스트 (PowerShell):

```
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
C:\dev\pulse-uni\engine\build\tests\Debug\pulse-tests.exe
```

bash 에서는 `export PATH="/c/Program Files/CMake/bin:$PATH" VCPKG_ROOT="C:/vcpkg"` 뒤 같은 명령. 링크가 `LNK1168` 로 실패하면 `pulse-engine.exe` 가 아직 돌고 있는 것이다 (`taskkill /IM pulse-engine.exe /F`).

기준선(작업 전 `main` 상태): 엔진 166 test cases 통과, 웹 145 tests 통과.

## File Structure

```
engine/src/core/ProcessFilter.{h,cpp}   incumbent_bonus, select(..., incumbents)
engine/src/core/DataAggregator.{h,cpp}  직전 선택 key 집합 (shown_keys_)
engine/src/app/EngineLoop.{h,cpp}       고정 주기 (sleepUntil)
engine/tests/test_process_filter.cpp    +4
engine/tests/test_aggregator.cpp        +2
engine/tests/test_engine_loop.cpp       +3 (타이밍)

web/src/visual/easing.ts          clamp01, easeOutCubic, easeInCubic
web/src/visual/presence.ts        PresenceTracker, presenceVisual
web/src/visual/lifecycleEvents.ts LifecycleConsumer
web/src/visual/bursts.ts          BurstPool, particleAt
web/src/visual/orbits.ts          orbitFor, satellitePosition
web/src/visual/camera.ts          OVERVIEW_POSE, focusPose, focusDirection, blendPose
web/src/visual/layout.ts          (수정) 사전 수렴 뒤 새 key 는 무리 바깥
web/src/visual/frameCache.ts      (수정) layoutNodesFrom(그룹 목록)
web/src/scene/framePriority.ts    useFrame 우선순위
web/src/scene/focusStore.ts       초점 key
web/src/scene/sceneContext.ts     (전체 교체)
web/src/scene/nodeList.ts         (전체 교체) nodeIdsOf
web/src/scene/SceneRoot.tsx       (전체 교체)
web/src/scene/ProcessNode.tsx     (전체 교체)
web/src/scene/Tooltip.tsx         (전체 교체)
web/src/scene/CameraRig.tsx
web/src/scene/Satellites.tsx
web/src/scene/SatelliteNode.tsx
web/src/scene/Particles.tsx
web/src/scene/FocusPanel.tsx
web/src/scene/Universe.tsx        (전체 교체)
web/src/shell/shell.css           (전체 교체) focus-panel
web/src/shell/Shell.tsx           (전체 교체) D 키
web/.oxlintrc.json                (전체 교체) visual/ 에 gsap 금지
```

---

## Task 1: 엔진 — 유지 보너스

**Files:**
- Modify (전체 교체): `engine/src/core/ProcessFilter.h`, `engine/src/core/ProcessFilter.cpp`, `engine/src/core/DataAggregator.h`, `engine/src/core/DataAggregator.cpp`
- Test (전체 교체): `engine/tests/test_process_filter.cpp`, `engine/tests/test_aggregator.cpp`
- Modify: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md` (5.3절)

**Interfaces:**
- Produces: `FilterConfig::incumbent_bonus` (double, 기본 2.0). `ProcessFilter::select(std::vector<ProcessGroup> groups, double total_mem_mb, const std::unordered_set<std::string>& incumbents = {}) const`. `DataAggregator` 는 선택 직후 `shown_keys_` 에 이번 목록의 key 를 저장하고 다음 `select` 에 넘긴다. 기존 호출부(2인자)는 그대로 컴파일된다.

두 테스트 파일은 기존 테스트를 그대로 두고 끝에 새 테스트를 덧붙인 전체 내용이다.

- [ ] **Step 1: 테스트 파일 교체 — `engine/tests/test_process_filter.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/ProcessFilter.h"

using namespace pulse;

namespace {

ProcessGroup makeGroup(std::string name,
                       uint32_t root_pid,
                       double mem_mb,
                       std::optional<double> cpu_pct = 0.0,
                       Account account = Account::User) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = root_pid;
    g.key = name + ":" + std::to_string(root_pid);
    g.mem_mb = mem_mb;
    g.cpu_pct = cpu_pct;
    g.account = account;
    g.proc_count = 1;
    return g;
}

}  // namespace

TEST_CASE("fewer groups than the cap are all returned", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 40;
    const ProcessFilter filter(cfg);

    const auto out = filter.select({makeGroup("a.exe", 1, 10.0)}, 32768.0);

    REQUIRE(out.size() == 1);
}

TEST_CASE("the cap is honoured", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 2;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("small.exe", 1, 10.0),
            makeGroup("big.exe", 2, 3000.0),
            makeGroup("mid.exe", 3, 500.0),
        },
        32768.0);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].name == "big.exe");
    REQUIRE(out[1].name == "mid.exe");
}

TEST_CASE("cpu outweighs memory when its weight is higher", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    cfg.cpu_weight = 10.0;
    cfg.mem_weight = 1.0;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("busy.exe", 1, 10.0, 80.0),
            makeGroup("fat.exe", 2, 3000.0, 0.0),
        },
        32768.0);

    REQUIRE(out[0].name == "busy.exe");
}

TEST_CASE("a user account group outranks a system group of equal weight", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    cfg.user_account_bonus = 1.5;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("service.exe", 1, 500.0, 5.0, Account::System),
            makeGroup("mine.exe", 2, 500.0, 5.0, Account::User),
        },
        32768.0);

    REQUIRE(out[0].name == "mine.exe");
}

TEST_CASE("a missing cpu reading scores as zero rather than excluding the group", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 2;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("warming.exe", 1, 900.0, std::nullopt),
            makeGroup("tiny.exe", 2, 1.0, 0.0),
        },
        32768.0);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].name == "warming.exe");
}

TEST_CASE("ties preserve input order", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 3;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("first.exe", 1, 100.0, 1.0),
            makeGroup("second.exe", 2, 100.0, 1.0),
            makeGroup("third.exe", 3, 100.0, 1.0),
        },
        32768.0);

    REQUIRE(out[0].name == "first.exe");
    REQUIRE(out[1].name == "second.exe");
    REQUIRE(out[2].name == "third.exe");
}

TEST_CASE("a zero total memory does not produce a division by zero", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    const ProcessFilter filter(cfg);

    const auto out = filter.select({makeGroup("a.exe", 1, 100.0, 4.0)}, 0.0);

    REQUIRE(out.size() == 1);
}

TEST_CASE("an empty input yields an empty result", "[filter]") {
    const ProcessFilter filter;

    REQUIRE(filter.select({}, 32768.0).empty());
}

TEST_CASE("the configured weights change the ranking", "[filter]") {
    // mem_norm: 6553.6 / 32768 * 100 = 20.0,  1638.4 / 32768 * 100 = 5.0
    const std::vector<ProcessGroup> groups = {
        makeGroup("mem_heavy.exe", 1, 6553.6, 10.0),
        makeGroup("cpu_heavy.exe", 2, 1638.4, 30.0),
    };

    FilterConfig balanced;
    balanced.max_groups = 2;
    // both groups are Account::User, so the 1.5x bonus applies to both:
    // (10*1.0 + 20*1.0) * 1.5 = 45  loses to  (30*1.0 + 5*1.0) * 1.5 = 52.5
    const auto by_balanced = ProcessFilter(balanced).select(groups, 32768.0);
    REQUIRE(by_balanced[0].name == "cpu_heavy.exe");

    FilterConfig memory_led;
    memory_led.max_groups = 2;
    memory_led.cpu_weight = 0.1;
    // (10*0.1 + 20*1.0) * 1.5 = 31.5  beats  (30*0.1 + 5*1.0) * 1.5 = 12
    const auto by_memory = ProcessFilter(memory_led).select(groups, 32768.0);
    REQUIRE(by_memory[0].name == "mem_heavy.exe");
}

TEST_CASE("an incumbent group survives a newcomer that is only slightly ahead", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    const ProcessFilter filter(cfg);

    // 신참이 10% 앞서지만 기존 그룹의 ×2.0 보너스를 넘지 못한다.
    const auto out = filter.select(
        {
            makeGroup("newcomer.exe", 2, 110.0),
            makeGroup("incumbent.exe", 1, 100.0),
        },
        32768.0, {"incumbent.exe:1"});

    REQUIRE(out.size() == 1);
    REQUIRE(out[0].name == "incumbent.exe");
}

TEST_CASE("a newcomer more than the bonus ahead displaces an incumbent", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    const ProcessFilter filter(cfg);

    // 210 / 100 = 2.1 > 2.0.
    const auto out = filter.select(
        {
            makeGroup("newcomer.exe", 2, 210.0),
            makeGroup("incumbent.exe", 1, 100.0),
        },
        32768.0, {"incumbent.exe:1"});

    REQUIRE(out[0].name == "newcomer.exe");
}

TEST_CASE("the incumbent bonus never changes how many groups are selected", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 2;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("a.exe", 1, 100.0),
            makeGroup("b.exe", 2, 200.0),
            makeGroup("c.exe", 3, 300.0),
        },
        32768.0, {"a.exe:1", "b.exe:2", "c.exe:3", "gone.exe:9"});

    REQUIRE(out.size() == 2);
}

TEST_CASE("the incumbent bonus is configurable and 1.0 turns it off", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    cfg.incumbent_bonus = 1.0;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("newcomer.exe", 2, 110.0),
            makeGroup("incumbent.exe", 1, 100.0),
        },
        32768.0, {"incumbent.exe:1"});

    REQUIRE(out[0].name == "newcomer.exe");
}
```

- [ ] **Step 2: 테스트 파일 교체 — `engine/tests/test_aggregator.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/DataAggregator.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid, uint32_t ppid, std::string name,
                       uint64_t cpu_cumulative_ms, uint64_t mem_bytes,
                       uint32_t thread_count = 1, uint64_t start_time_ms = 1000) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.cpu_cumulative_ms = cpu_cumulative_ms;
    p.mem_bytes = mem_bytes;
    p.thread_count = thread_count;
    p.start_time_ms = start_time_ms;
    p.account = Account::User;
    return p;
}

RawSample makeSample(std::vector<RawProcess> processes, uint64_t timestamp_ms) {
    RawSample s;
    s.processes = std::move(processes);
    s.cores = {RawCore{0, 50.0}, RawCore{1, 50.0}};
    s.memory = RawMemory{8ull * 1024 * 1024 * 1024, 32ull * 1024 * 1024 * 1024};
    s.timestamp_ms = timestamp_ms;
    return s;
}

}  // namespace

TEST_CASE("the first snapshot has no cpu readings", "[aggregate]") {
    // 스펙 6.1: 기동 직후 첫 주기에는 CPU 값이 존재하지 않는다.
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE_FALSE(snap.groups[0].cpu_pct.has_value());
    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("system cpu is present on the first snapshot when cores are populated",
         "[aggregate]") {
    // PDH 는 리더 생성자에서 이미 첫 수집을 해 두므로, 델타 기반 그룹 cpu 와
    // 달리 system.cpu_pct 는 seq 1 부터 유효하다.
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("an empty cores list leaves system cpu empty", "[aggregate]") {
    DataAggregator aggregator(2);

    RawSample sample = makeSample({}, 1000);
    sample.cores.clear();

    const auto snap = aggregator.aggregate(sample);

    REQUIRE_FALSE(snap.system.cpu_pct.has_value());
}

TEST_CASE("the second snapshot carries cpu readings", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));

    const auto snap = aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024)}, 2000));

    REQUIRE(snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(5.0, 0.0001));
}

TEST_CASE("group cpu is the sum of its member processes", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 0, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 0, 1024 * 1024, 1, 2000),
        },
        1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 100, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 100, 1024 * 1024, 1, 2000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(10.0, 0.0001));
}

TEST_CASE("a pid duplicated within one sample keeps its cpu reading", "[aggregate]") {
    // 같은 pid 가 한 표본에 두 번 들어오면, 두 번째 update() 호출은 방금
    // 저장한 표본과 비교해 "시계가 안 흘렀다" 로 판정되어 nullopt 를 반환한다.
    // 첫 항목의 유효한 값을 덮어써서는 안 된다.
    DataAggregator single(2);
    single.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));
    const auto single_snap = single.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024)}, 2000));

    DataAggregator duped(2);
    duped.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));
    const auto duped_snap = duped.aggregate(makeSample(
        {
            makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024),
            makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024),
        },
        2000));

    REQUIRE(duped_snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*duped_snap.groups[0].cpu_pct,
                Catch::Matchers::WithinAbs(*single_snap.groups[0].cpu_pct, 0.0001));
}

TEST_CASE("the sequence number advances with each snapshot", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto first = aggregator.aggregate(makeSample({}, 1000));
    const auto second = aggregator.aggregate(makeSample({}, 2000));

    REQUIRE(first.seq == 1);
    REQUIRE(second.seq == 2);
}

TEST_CASE("system totals count every process including filtered ones", "[aggregate]") {
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 10),
            makeProcess(2, 0, "small.exe", 0, 1024 * 1024, 5),
            makeProcess(3, 0, "svchost.exe", 0, 1024 * 1024, 3),
        },
        1000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.system.process_total == 3);
    REQUIRE(snap.system.thread_total == 18);
}

TEST_CASE("ambient service totals survive filtering", "[aggregate]") {
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024),
            makeProcess(2, 0, "svchost.exe", 0, 100ull * 1024 * 1024),
        },
        1000));

    REQUIRE(snap.ambient.service_proc_count == 1);
    REQUIRE_THAT(snap.ambient.service_mem_mb, Catch::Matchers::WithinAbs(100.0, 0.01));
}

TEST_CASE("lifecycle is tracked across the unfiltered set", "[aggregate]") {
    // 필터 상한이 1이어도, 목록에 오르지 못한 프로세스의 생성이 보고되어야 한다.
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024)}, 1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024),
            makeProcess(2, 0, "tiny.exe", 0, 1024 * 1024),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].name == "big.exe");
    REQUIRE(snap.lifecycle.spawned.size() == 1);
    REQUIRE(snap.lifecycle.spawned[0].name == "tiny.exe");
}

TEST_CASE("memory totals are converted to megabytes", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE_THAT(snap.system.mem_total_mb, Catch::Matchers::WithinAbs(32768.0, 0.01));
    REQUIRE_THAT(snap.system.mem_used_mb, Catch::Matchers::WithinAbs(8192.0, 0.01));
}

TEST_CASE("core loads are carried through", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE(snap.cores.size() == 2);
    REQUIRE(snap.cores[0].id == 0);
    REQUIRE_THAT(snap.cores[0].pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("system cpu is the mean of core loads", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(makeSample({}, 1000));

    const auto snap = aggregator.aggregate(makeSample({}, 2000));

    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("timestamps are carried through", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1758531600123));

    REQUIRE(snap.t == 1758531600123);
}

TEST_CASE("a spawned process into a filtered-out group still reports its group key",
         "[aggregate]") {
    // 필터 상한이 1이면 두 그룹 중 하나만 화면에 남는다. 새로 생긴 프로세스가
    // 살아남지 못한 그룹에 속하더라도, group 필드는 필터 전 grouping 결과에서
    // 구해지므로 올바른 key 를 담아야 한다.
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 1, 1000),
            makeProcess(2, 0, "small.exe", 0, 1ull * 1024 * 1024, 1, 2000),
        },
        1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 1, 1000),
            makeProcess(2, 0, "small.exe", 0, 1ull * 1024 * 1024, 1, 2000),
            makeProcess(3, 2, "small.exe", 0, 1ull * 1024 * 1024, 1, 3000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].name == "big.exe");

    REQUIRE(snap.lifecycle.spawned.size() == 1);
    REQUIRE(snap.lifecycle.spawned[0].pid == 3);
    REQUIRE(snap.lifecycle.spawned[0].group == "small.exe:2");
}

TEST_CASE("a group's cpu is the sum of members that have a reading", "[aggregate]") {
    // 루트는 값을 갖지만 막 생겨난 자식은 아직 두 번째 표본을 못 받아 값이
    // 없다. 그룹 cpu 는 값을 가진 구성원의 합, 즉 루트만의 기여분이어야 한다
    // -- 자식이 하나 늘 때마다 그룹 전체가 비어버리면 안 된다.
    DataAggregator aggregator(2);
    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "app.exe", 0, 1024 * 1024, 1, 1000)}, 1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 100, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 0, 1024 * 1024, 1, 2000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(5.0, 0.0001));
}

TEST_CASE("a group shown last time keeps its place against a slightly bigger newcomer",
          "[aggregate]") {
    // 첫 집계에서 110 MB 짜리가 아직 없으므로 100 MB 짜리가 뽑힌다. 둘째 집계에서
    // 110 MB 짜리가 나타나도 10% 차이는 유지 보너스(×2.0)를 넘지 못한다.
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto first = aggregator.aggregate(
        makeSample({makeProcess(1, 0, "incumbent.exe", 0, 100ull * 1024 * 1024)}, 1000));
    REQUIRE(first.groups.at(0).name == "incumbent.exe");

    const auto second = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "incumbent.exe", 0, 100ull * 1024 * 1024),
            makeProcess(2, 0, "newcomer.exe", 0, 110ull * 1024 * 1024),
        },
        2000));
    REQUIRE(second.groups.size() == 1);
    REQUIRE(second.groups.at(0).name == "incumbent.exe");
}

TEST_CASE("the very first selection has no incumbents", "[aggregate]") {
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto first = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "smaller.exe", 0, 100ull * 1024 * 1024),
            makeProcess(2, 0, "bigger.exe", 0, 110ull * 1024 * 1024),
        },
        1000));

    REQUIRE(first.groups.at(0).name == "bigger.exe");
}
```

- [ ] **Step 3: 빌드가 실패하는지 확인 (RED)**

Run: 엔진 빌드 명령 (환경 절)
Expected: 컴파일 오류 — `select` 가 인자 3개를 받지 않음 (`test_process_filter.cpp`), 또는 `incumbent_bonus` 가 `FilterConfig` 의 멤버가 아님.

- [ ] **Step 4: `engine/src/core/ProcessFilter.h` 전체 교체**

```cpp
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
```

- [ ] **Step 5: `engine/src/core/ProcessFilter.cpp` 전체 교체**

```cpp
#include "core/ProcessFilter.h"

#include <algorithm>

namespace pulse {

ProcessFilter::ProcessFilter(FilterConfig cfg) : cfg_(cfg) {}

std::vector<ProcessGroup> ProcessFilter::select(
    std::vector<ProcessGroup> groups,
    double total_mem_mb,
    const std::unordered_set<std::string>& incumbents) const {
    const auto score = [&](const ProcessGroup& g) {
        const double cpu = g.cpu_pct.value_or(0.0);
        const double mem_norm =
            total_mem_mb > 0.0 ? (g.mem_mb / total_mem_mb) * 100.0 : 0.0;
        const double base = cpu * cfg_.cpu_weight + mem_norm * cfg_.mem_weight;
        const double account_bonus =
            g.account == Account::User ? cfg_.user_account_bonus : 1.0;
        const double incumbent =
            incumbents.find(g.key) != incumbents.end() ? cfg_.incumbent_bonus : 1.0;
        return base * account_bonus * incumbent;
    };

    // stable_sort 라야 동점일 때 입력 순서가 보존된다.
    std::stable_sort(groups.begin(), groups.end(),
                     [&](const ProcessGroup& a, const ProcessGroup& b) {
                         return score(a) > score(b);
                     });

    if (groups.size() > cfg_.max_groups) {
        groups.resize(cfg_.max_groups);
    }
    return groups;
}

}  // namespace pulse
```

- [ ] **Step 6: `engine/src/core/DataAggregator.h` 전체 교체**

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
    FlowEstimator flow_estimator_;
    // 직전 스냅샷에 실린 그룹 key. 다음 선택에서 유지 보너스를 받는다.
    std::unordered_set<std::string> shown_keys_;
    uint64_t seq_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 7: `engine/src/core/DataAggregator.cpp` 전체 교체**

```cpp
#include "core/DataAggregator.h"

#include <unordered_map>

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

    // 7. 화면에 남은 그룹에 대해서만 흐름을 추정한다.
    snapshot.flows = flow_estimator_.estimate(snapshot.groups, snapshot.cores);

    return snapshot;
}

}  // namespace pulse
```

- [ ] **Step 8: 통과 확인 (GREEN)**

Run: 엔진 빌드 후 `pulse-tests.exe "[filter],[aggregate]"`, 이어서 `pulse-tests.exe`
Expected: 빌드 경고 0. 필터·집계 테스트 전부 통과. 전체 `All tests passed (… in 172 test cases)`.

- [ ] **Step 9: 계약서 5.3절 보완**

`docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md` 의 5.3절 첫 문단 바로 아래에 다음 문단을 추가한다:

```markdown
직전 스냅샷에 실렸던 그룹은 점수에 `incumbent_bonus`(기본 2.0)를 곱한다(M5). 경계 순위의 작은 그룹들이 점수의 미세한 변동만으로 매초 목록을 드나드는 것을 막는다 — 실측 72초 동안 목록 진입이 간격당 0.96회(보너스 없음)에서 0.13회(×2.0)로 줄었다. 목록 개수는 그대로다. 그룹 key 는 `이름:루트pid` 이므로 재시작한 프로그램은 새 그룹이고 보너스를 이어받지 않는다.
```

- [ ] **Step 10: 커밋**

```
git add engine/src/core/ProcessFilter.h engine/src/core/ProcessFilter.cpp engine/src/core/DataAggregator.h engine/src/core/DataAggregator.cpp engine/tests/test_process_filter.cpp engine/tests/test_aggregator.cpp docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md
git commit -m "feat(engine): keep groups that were shown last time unless a newcomer doubles them"
```

---

## Task 2: 엔진 — 고정 주기

**Files:**
- Modify (전체 교체): `engine/src/app/EngineLoop.h`, `engine/src/app/EngineLoop.cpp`
- Test (전체 교체): `engine/tests/test_engine_loop.cpp`
- Modify: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md` (4.4절)

**Interfaces:**
- Produces: `EngineLoop` 의 공개 인터페이스는 그대로다. private `sleepInterval()` 이 `sleepUntil(std::chrono::steady_clock::time_point)` 로 바뀐다. 테스트 태그 `[timing]` 이 새로 생긴다.

타이밍 테스트는 Windows 기본 타이머 해상도(약 15.6 ms)를 견디도록 경계를 넉넉히 잡았고, 옛 동작과는 확실히 갈린다 (시제품에서 옛 동작: 총 430 ms, 첫 간격 76 ms — 둘 다 실패). 한 번이라도 실패하면 경계를 넓히지 말고 BLOCKED 로 출력을 보고한다.

- [ ] **Step 1: 테스트 파일 교체 — `engine/tests/test_engine_loop.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "app/EngineLoop.h"
#include "fakes/FakeSystemReader.h"

using namespace pulse;

namespace {

RawSample makeSample(uint64_t timestamp_ms) {
    RawSample s;
    s.timestamp_ms = timestamp_ms;
    s.cores = {RawCore{0, 25.0}};
    s.memory = RawMemory{1024ull * 1024 * 1024, 4096ull * 1024 * 1024};

    RawProcess p;
    p.pid = 100;
    p.ppid = 4;
    p.name = "app.exe";
    p.cpu_cumulative_ms = timestamp_ms / 10;
    p.mem_bytes = 64ull * 1024 * 1024;
    p.thread_count = 4;
    p.start_time_ms = 1000;
    p.account = Account::User;
    s.processes.push_back(p);
    return s;
}

EngineLoopConfig fastConfig(unsigned iterations) {
    EngineLoopConfig cfg;
    cfg.interval_ms = 1;
    cfg.iterations = iterations;
    return cfg;
}

}  // namespace

TEST_CASE("the loop calls the handler once per iteration", "[loop]") {
    FakeSystemReader reader({makeSample(1000), makeSample(2000), makeSample(3000)}, 4);
    int calls = 0;

    EngineLoop loop(reader, fastConfig(3), [&](const SystemSnapshot&) { ++calls; });
    loop.run();

    REQUIRE(calls == 3);
    REQUIRE(reader.readCount() == 3);
    REQUIRE(loop.error().empty());
}

TEST_CASE("sequence numbers advance across iterations", "[loop]") {
    FakeSystemReader reader({makeSample(1000), makeSample(2000)}, 4);
    std::vector<uint64_t> seqs;

    EngineLoop loop(reader, fastConfig(2),
                    [&](const SystemSnapshot& s) { seqs.push_back(s.seq); });
    loop.run();

    REQUIRE(seqs == std::vector<uint64_t>{1, 2});
}

TEST_CASE("stop ends a loop that would otherwise run forever", "[loop]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    std::atomic<int> calls{0};

    EngineLoopConfig cfg;
    cfg.interval_ms = 5;
    cfg.iterations = 0;  // 무한

    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { ++calls; });

    std::thread worker([&] { loop.run(); });
    while (calls.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    loop.stop();
    worker.join();

    REQUIRE(calls.load() >= 2);
    REQUIRE(loop.error().empty());
}

TEST_CASE("an exception from the reader stops the loop and is reported", "[loop]") {
    ThrowingSystemReader reader(2);
    int calls = 0;

    EngineLoop loop(reader, fastConfig(10), [&](const SystemSnapshot&) { ++calls; });
    loop.run();

    REQUIRE(calls == 2);
    REQUIRE(loop.error() == "reader exploded");
}

TEST_CASE("the aggregator config reaches the snapshot", "[loop]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    EngineLoopConfig cfg = fastConfig(1);
    cfg.aggregator.filter.max_groups = 0;

    std::size_t groups = 99;
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot& s) { groups = s.groups.size(); });
    loop.run();

    REQUIRE(groups == 0);
}

namespace {

// read() 마다 정해진 시간만큼 걸리는 리더. 목록이 끝나면 마지막 지연을 반복한다.
class SlowSystemReader final : public ISystemReader {
public:
    explicit SlowSystemReader(std::vector<unsigned> delays_ms)
        : delays_ms_(std::move(delays_ms)) {}

    RawSample read() override {
        const std::size_t index = std::min(next_++, delays_ms_.size() - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(delays_ms_[index]));
        return makeSample(1000 * (next_ + 1));
    }

    unsigned coreCount() const override { return 4; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

private:
    std::vector<unsigned> delays_ms_;
    std::size_t next_ = 0;
};

using Clock = std::chrono::steady_clock;

// 각 핸들러 호출 사이의 간격(ms).
std::vector<double> gapsBetweenCalls(ISystemReader& reader, unsigned interval_ms,
                                     unsigned iterations) {
    EngineLoopConfig cfg;
    cfg.interval_ms = interval_ms;
    cfg.iterations = iterations;

    std::vector<Clock::time_point> calls;
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { calls.push_back(Clock::now()); });
    loop.run();

    std::vector<double> gaps;
    for (std::size_t i = 1; i < calls.size(); ++i) {
        gaps.push_back(std::chrono::duration<double, std::milli>(calls[i] - calls[i - 1]).count());
    }
    return gaps;
}

}  // namespace

// 타이밍 테스트다. Windows 의 기본 타이머 해상도(약 15.6 ms)를 견디도록 경계를
// 넉넉하게 잡되, 옛 동작(샘플링 시간 + 주기)과는 확실히 갈리게 했다.
TEST_CASE("the loop starts samples on a fixed period regardless of how long sampling takes",
          "[loop][timing]") {
    // 샘플 30 ms, 주기 50 ms. 옛 동작이면 호출 간격이 80 ms 이상이다.
    SlowSystemReader reader({30});
    const auto gaps = gapsBetweenCalls(reader, 50, 5);

    REQUIRE(gaps.size() == 4);
    double total = 0.0;
    for (const double gap : gaps) {
        total += gap;
    }
    REQUIRE(total >= 190.0);
    REQUIRE(total < 280.0);
}

TEST_CASE("a late sample is followed immediately, then the period resumes from now",
          "[loop][timing]") {
    // 첫 샘플만 100 ms (주기 50 ms 를 넘김), 나머지는 즉시.
    // 기준을 다시 잡으면: 2번은 곧바로, 3번은 그로부터 50 ms 뒤.
    // 밀린 주기를 따라잡으면: 2·3번이 연달아 곧바로 온다.
    SlowSystemReader reader({100, 0});
    const auto gaps = gapsBetweenCalls(reader, 50, 4);

    REQUIRE(gaps.size() == 3);
    REQUIRE(gaps[0] < 30.0);   // 1 → 2: 쉬지 않았다
    REQUIRE(gaps[1] >= 35.0);  // 2 → 3: 몰아서 따라잡지 않았다
}

TEST_CASE("stop interrupts a long sleep promptly", "[loop][timing]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    EngineLoopConfig cfg;
    cfg.interval_ms = 10000;
    cfg.iterations = 0;

    std::atomic<int> calls{0};
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { ++calls; });

    const auto started = Clock::now();
    std::jthread worker([&] { loop.run(); });
    while (calls.load() < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    loop.stop();
    worker.join();

    const double elapsed =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    REQUIRE(elapsed < 500.0);
}
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: 엔진 빌드 후 `pulse-tests.exe "[timing]"`
Expected: `the loop starts samples on a fixed period…` 가 `total < 280.0` 에서, `a late sample is followed immediately…` 가 `gaps[0] < 30.0` 에서 실패. `stop interrupts a long sleep promptly` 는 통과한다 (옛 코드도 조각 단위로 잔다).

- [ ] **Step 3: `engine/src/app/EngineLoop.h` 전체 교체**

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>

#include "core/DataAggregator.h"
#include "core/Snapshot.h"
#include "platform/ISystemReader.h"

namespace pulse {

struct EngineLoopConfig {
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 stop() 까지 계속
    AggregatorConfig aggregator;
};

// 표본을 주기적으로 떠서 스냅샷으로 만들고 콜백에 넘긴다.
// WebSocket 도 콘솔도 모른다 — 콜백이 무엇을 하는지는 호출자의 일이다.
class EngineLoop {
public:
    using SnapshotHandler = std::function<void(const SystemSnapshot&)>;

    EngineLoop(ISystemReader& reader, EngineLoopConfig cfg, SnapshotHandler handler);

    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    // 호출한 스레드에서 루프를 돈다. stop() 이 불리거나 iterations 를 채우면
    // 돌아온다. 표본 수집 중 예외는 잡아 error() 에 담고 멈춘다 —
    // 샘플링 스레드에서는 프로세스를 끝낼 수 없기 때문이다.
    void run();

    // 다른 스레드에서 부를 수 있다.
    void stop();

    // run() 이 예외로 끝났으면 사람이 읽을 이유, 아니면 빈 문자열.
    // run() 을 도는 스레드만 이 값을 쓴다 — 그 스레드가 돌아왔거나 join 된
    // 뒤에만 읽어야 한다.
    const std::string& error() const;

private:
    // deadline 까지 잔다. 정지 요청에 빨리 반응하도록 잘게 쪼개 잔다.
    void sleepUntil(std::chrono::steady_clock::time_point deadline);

    ISystemReader& reader_;
    EngineLoopConfig cfg_;
    SnapshotHandler handler_;
    DataAggregator aggregator_;
    std::atomic<bool> stop_requested_{false};
    std::string error_;
};

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/app/EngineLoop.cpp` 전체 교체**

```cpp
#include "app/EngineLoop.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

namespace pulse {
namespace {

// stop() 이 최대 이만큼만 기다리면 반응한다.
constexpr unsigned kSleepSliceMs = 20;

}  // namespace

EngineLoop::EngineLoop(ISystemReader& reader, EngineLoopConfig cfg, SnapshotHandler handler)
    : reader_(reader),
      cfg_(std::move(cfg)),
      handler_(std::move(handler)),
      aggregator_(reader.coreCount(), cfg_.aggregator) {}

void EngineLoop::run() {
    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::milliseconds(cfg_.interval_ms);

    try {
        // 다음 샘플을 시작할 시각. 샘플링에 걸린 시간을 주기에서 빼기 위해
        // "끝나고 interval 만큼 쉰다" 가 아니라 "시작 시각 + interval" 에 깨어난다.
        // 그래야 hello.interval_ms 가 실제 주기가 된다 (M5 스펙 4.2).
        auto next_start = Clock::now();
        for (unsigned n = 0; cfg_.iterations == 0 || n < cfg_.iterations; ++n) {
            if (stop_requested_.load()) {
                return;
            }

            const SystemSnapshot snapshot = aggregator_.aggregate(reader_.read());
            handler_(snapshot);

            const bool is_last = cfg_.iterations != 0 && n + 1 == cfg_.iterations;
            if (is_last) {
                return;
            }

            next_start += interval;
            const auto now = Clock::now();
            if (next_start <= now) {
                // 샘플링이 주기보다 오래 걸렸다. 쉬지 않고 바로 다음 샘플을 뜨고
                // 기준을 지금으로 다시 잡는다 — 밀린 주기를 몰아서 따라잡지 않는다.
                next_start = now;
                continue;
            }
            sleepUntil(next_start);
        }
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void EngineLoop::stop() {
    stop_requested_.store(true);
}

const std::string& EngineLoop::error() const {
    return error_;
}

void EngineLoop::sleepUntil(std::chrono::steady_clock::time_point deadline) {
    using Clock = std::chrono::steady_clock;
    const auto slice = std::chrono::milliseconds(kSleepSliceMs);
    while (!stop_requested_.load()) {
        const auto now = Clock::now();
        if (now >= deadline) {
            return;
        }
        std::this_thread::sleep_for(std::min<Clock::duration>(deadline - now, slice));
    }
}

}  // namespace pulse
```

- [ ] **Step 5: 통과 확인 (GREEN) — 반복**

Run: 엔진 빌드 후 `pulse-tests.exe "[timing]"` 를 **10번** 반복, 이어서 `pulse-tests.exe`
Expected: 10번 모두 `All tests passed (7 assertions in 3 test cases)`. 전체 `All tests passed (… in 175 test cases)`. 빌드 경고 0.

bash 반복 예: `for i in $(seq 1 10); do ./build/tests/Debug/pulse-tests.exe "[timing]" 2>&1 | grep -E "passed|failed"; done`

- [ ] **Step 6: 계약서 4.4절 보완**

같은 계약서의 4.4절에서 `` `interval_ms` 마다 전송한다. `` 줄을 다음으로 바꾼다:

```markdown
`interval_ms` 마다 전송한다. 이 값은 샘플을 **시작하는** 간격이다 — 엔진은 샘플링에 걸린 시간을 빼고 다음 시작 시각까지 잔다. (M5 정정: 그 전에는 샘플링이 끝난 뒤 `interval_ms` 를 쉬어 실제 주기가 약 1.55초였고, 1초를 믿는 보간기가 매 주기 약 0.5초씩 멈췄다.)
```

- [ ] **Step 7: 커밋**

```
git add engine/src/app/EngineLoop.h engine/src/app/EngineLoop.cpp engine/tests/test_engine_loop.cpp docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md
git commit -m "fix(engine): start samples on a fixed period so interval_ms is the real period"
```

---

## Task 3: 존재 추적

**Files:**
- Create: `web/src/visual/easing.ts`, `web/src/visual/presence.ts`
- Test: `web/tests/visual/presence.test.ts`
- Modify (전체 교체): `web/.oxlintrc.json`

**Interfaces:**
- Produces:
  - `clamp01(x: number): number`, `easeOutCubic(t: number): number`, `easeInCubic(t: number): number`
  - `type Phase = 'forming' | 'fading-in' | 'present' | 'fading-out' | 'collapsing'`
  - `FORM_SEC = 1.2`, `FADE_SEC = 0.6`, `COLLAPSE_SEC = 1.0`
  - `interface PresenceEntry<T> { key: string; phase: Phase; progress: number; value: T; startSec: number }`
  - `interface PresenceInput<T> { key: string; value: T }`
  - `presenceVisual(entry: Pick<PresenceEntry<unknown>, 'phase' | 'progress'>): { scale: number; opacity: number }`
  - `class PresenceTracker<T> { update(current: readonly PresenceInput<T>[], born: ReadonlySet<string>, died: ReadonlySet<string>, nowSec: number): void; entries(): PresenceEntry<T>[]; get(key: string): PresenceEntry<T> | undefined; reset(): void }`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/presence.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { clamp01, easeInCubic, easeOutCubic } from '../../src/visual/easing';
import {
  COLLAPSE_SEC,
  FADE_SEC,
  FORM_SEC,
  PresenceTracker,
  presenceVisual,
  type PresenceInput,
} from '../../src/visual/presence';

const NONE = new Set<string>();

function items(...keys: string[]): PresenceInput<number>[] {
  return keys.map((key, i) => ({ key, value: i }));
}

// 첫 입력(= 이미 있던 것)으로 시작한 추적기.
function started(...keys: string[]): PresenceTracker<number> {
  const tracker = new PresenceTracker<number>();
  tracker.update(items(...keys), NONE, NONE, 0);
  return tracker;
}

describe('easing', () => {
  it('clamps to [0, 1]', () => {
    expect(clamp01(-1)).toBe(0);
    expect(clamp01(2)).toBe(1);
    expect(clamp01(0.3)).toBe(0.3);
  });

  it('pins both ends and bends the middle the right way', () => {
    expect(easeOutCubic(0)).toBe(0);
    expect(easeOutCubic(1)).toBe(1);
    expect(easeInCubic(0)).toBe(0);
    expect(easeInCubic(1)).toBe(1);
    expect(easeOutCubic(0.5)).toBeGreaterThan(0.5);
    expect(easeInCubic(0.5)).toBeLessThan(0.5);
  });
});

describe('PresenceTracker', () => {
  it('treats the first input as already present', () => {
    const tracker = started('a', 'b');
    expect(tracker.entries().map((e) => [e.key, e.phase])).toEqual([
      ['a', 'present'],
      ['b', 'present'],
    ]);
  });

  it('forms a key that was really born', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), new Set(['b']), NONE, 1);
    expect(tracker.get('b')?.phase).toBe('forming');

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FORM_SEC / 2);
    expect(tracker.get('b')?.progress).toBeCloseTo(0.5, 6);

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FORM_SEC);
    expect(tracker.get('b')?.phase).toBe('present');
  });

  it('fades in a key that only climbed into the list', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), NONE, NONE, 1);
    expect(tracker.get('b')?.phase).toBe('fading-in');

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC);
    expect(tracker.get('b')?.phase).toBe('present');
  });

  it('collapses a key that really died, then removes it', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, new Set(['b']), 1);
    expect(tracker.get('b')?.phase).toBe('collapsing');

    tracker.update(items('a'), NONE, NONE, 1 + COLLAPSE_SEC / 2);
    expect(tracker.get('b')?.phase).toBe('collapsing');

    tracker.update(items('a'), NONE, NONE, 1 + COLLAPSE_SEC);
    expect(tracker.get('b')).toBeUndefined();
  });

  it('quietly fades out a key that only dropped out of the list', () => {
    // 계약서 4.6 — 순위에서 빠진 것을 붕괴시키면 살아 있는 프로그램이 터지는 장면이 된다.
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, NONE, 1);
    expect(tracker.get('b')?.phase).toBe('fading-out');

    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC);
    expect(tracker.get('b')).toBeUndefined();
  });

  it('keeps the last value of a leaving key', () => {
    const tracker = new PresenceTracker<number>();
    tracker.update([{ key: 'b', value: 42 }], NONE, NONE, 0);
    tracker.update([], NONE, NONE, 1);
    tracker.update([], NONE, NONE, 1.1);
    expect(tracker.get('b')?.value).toBe(42);
  });

  it('updates the value of a present key every call', () => {
    const tracker = new PresenceTracker<number>();
    tracker.update([{ key: 'a', value: 1 }], NONE, NONE, 0);
    tracker.update([{ key: 'a', value: 2 }], NONE, NONE, 0.1);
    expect(tracker.get('a')?.value).toBe(2);
  });

  it('brightens a returning key from the opacity it had reached', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, NONE, 1);
    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC * 0.25);
    const dimmed = presenceVisual(tracker.get('b')!).opacity;
    expect(dimmed).toBeCloseTo(0.75, 6);

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC * 0.25);
    const entry = tracker.get('b')!;
    expect(entry.phase).toBe('fading-in');
    expect(presenceVisual(entry).opacity).toBeCloseTo(dimmed, 6);
  });

  it('dims a key that leaves mid fade-in from the opacity it had reached', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), NONE, NONE, 1);
    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC * 0.4);
    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC * 0.4);

    const entry = tracker.get('b')!;
    expect(entry.phase).toBe('fading-out');
    expect(presenceVisual(entry).opacity).toBeCloseTo(0.4, 6);
  });

  it('does not restart a collapse on later calls', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, new Set(['b']), 1);
    tracker.update(items('a'), NONE, NONE, 1.5);
    expect(tracker.get('b')?.startSec).toBe(1);
  });

  it('forgets everything on reset and treats the next input as already present', () => {
    const tracker = started('a');
    tracker.reset();
    expect(tracker.entries()).toEqual([]);

    tracker.update(items('x'), new Set(['x']), NONE, 5);
    expect(tracker.get('x')?.phase).toBe('present');
  });
});

describe('presenceVisual', () => {
  it('matches the spec table', () => {
    expect(presenceVisual({ phase: 'present', progress: 1 })).toEqual({ scale: 1, opacity: 1 });
    expect(presenceVisual({ phase: 'forming', progress: 0 })).toEqual({ scale: 0, opacity: 0 });
    expect(presenceVisual({ phase: 'forming', progress: 1 })).toEqual({ scale: 1, opacity: 1 });
    expect(presenceVisual({ phase: 'fading-in', progress: 0.3 })).toEqual({
      scale: 1,
      opacity: 0.3,
    });
    expect(presenceVisual({ phase: 'fading-out', progress: 0.3 }).opacity).toBeCloseTo(0.7, 6);
    expect(presenceVisual({ phase: 'collapsing', progress: 1 })).toEqual({ scale: 0, opacity: 0 });
    expect(presenceVisual({ phase: 'collapsing', progress: 0.5 }).opacity).toBeCloseTo(0.75, 6);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/presence.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/easing"`.

- [ ] **Step 3: `web/src/visual/easing.ts`**

```ts
// 연출 진행도에 쓰는 이징. 입력은 0~1 로 잘라서 쓴다.
export function clamp01(x: number): number {
  return Math.min(1, Math.max(0, x));
}

// 빠르게 시작해 천천히 멈춘다. 형성처럼 "자리를 잡는" 움직임.
export function easeOutCubic(t: number): number {
  const u = 1 - clamp01(t);
  return 1 - u * u * u;
}

// 천천히 시작해 빠르게 끝난다. 수렴·붕괴처럼 "빨려 드는" 움직임.
export function easeInCubic(t: number): number {
  const c = clamp01(t);
  return c * c * c;
}
```

- [ ] **Step 4: `web/src/visual/presence.ts`**

```ts
import { clamp01, easeInCubic, easeOutCubic } from './easing';

// M5 스펙 5절. 화면에 무엇이 있는지를 스토어의 key 목록이 아니라 이 추적기가
// 정한다. 떠나는 항목은 연출이 끝날 때까지 마지막 값을 고정한 채 남는다.
// 그룹(key = 그룹 key)과 Focus 위성(key = 자식 pid)이 같은 추적기를 쓴다.

export type Phase = 'forming' | 'fading-in' | 'present' | 'fading-out' | 'collapsing';

export const FORM_SEC = 1.2;
export const FADE_SEC = 0.6;
export const COLLAPSE_SEC = 1.0;

const DURATION: Record<Exclude<Phase, 'present'>, number> = {
  forming: FORM_SEC,
  'fading-in': FADE_SEC,
  'fading-out': FADE_SEC,
  collapsing: COLLAPSE_SEC,
};

export interface PresenceEntry<T> {
  key: string;
  phase: Phase;
  // 현재 phase 의 진행도 0~1. present 는 항상 1.
  progress: number;
  // 떠나는 항목은 마지막으로 본 값이 고정되어 있다.
  value: T;
  startSec: number;
}

export interface PresenceInput<T> {
  key: string;
  value: T;
}

function isLeaving(phase: Phase): boolean {
  return phase === 'fading-out' || phase === 'collapsing';
}

// 5절 표. 크기 배율과 불투명도.
export function presenceVisual(entry: Pick<PresenceEntry<unknown>, 'phase' | 'progress'>): {
  scale: number;
  opacity: number;
} {
  const p = clamp01(entry.progress);
  switch (entry.phase) {
    case 'forming':
      return { scale: easeOutCubic(p), opacity: p };
    case 'fading-in':
      return { scale: 1, opacity: p };
    case 'present':
      return { scale: 1, opacity: 1 };
    case 'fading-out':
      return { scale: 1, opacity: 1 - p };
    case 'collapsing':
      return { scale: 1 - easeInCubic(p), opacity: 1 - p * p };
  }
}

export class PresenceTracker<T> {
  private readonly items = new Map<string, PresenceEntry<T>>();
  private initialized = false;

  // born: 이번 프레임에 실제로 생성된 key. died: 실제로 종료된 key.
  // 두 집합은 lifecycle 을 소비한 프레임에만 비어 있지 않다.
  update(
    current: readonly PresenceInput<T>[],
    born: ReadonlySet<string>,
    died: ReadonlySet<string>,
    nowSec: number,
  ): void {
    // 첫 입력은 이미 있던 것들이다. 앱을 열 때 전부가 한꺼번에 페이드인하지 않는다.
    if (!this.initialized) {
      this.initialized = true;
      for (const { key, value } of current) {
        this.items.set(key, { key, phase: 'present', progress: 1, value, startSec: nowSec });
      }
      return;
    }

    const seen = new Set<string>();
    for (const { key, value } of current) {
      seen.add(key);
      const existing = this.items.get(key);
      if (existing === undefined) {
        this.items.set(key, {
          key,
          phase: born.has(key) ? 'forming' : 'fading-in',
          progress: 0,
          value,
          startSec: nowSec,
        });
        continue;
      }
      existing.value = value;
      if (existing.phase === 'fading-out') {
        // 사라지던 것이 다시 나타났다. 지금 불투명도에서 이어서 밝아진다.
        const opacity = 1 - existing.progress;
        existing.phase = 'fading-in';
        existing.startSec = nowSec - opacity * FADE_SEC;
      } else if (existing.phase === 'collapsing') {
        // 종료된 key 가 다시 나타나는 것은 pid 재사용뿐이다. 새로 들어온 것으로 본다.
        existing.phase = 'fading-in';
        existing.startSec = nowSec;
      }
    }

    for (const entry of this.items.values()) {
      if (seen.has(entry.key) || isLeaving(entry.phase)) {
        continue;
      }
      if (died.has(entry.key)) {
        entry.phase = 'collapsing';
        entry.startSec = nowSec;
      } else {
        // 순위 이탈. 지금 불투명도에서 이어서 어두워진다.
        const opacity = presenceVisual(entry).opacity;
        entry.phase = 'fading-out';
        entry.startSec = nowSec - (1 - opacity) * FADE_SEC;
      }
    }

    for (const entry of [...this.items.values()]) {
      if (entry.phase === 'present') {
        entry.progress = 1;
        continue;
      }
      entry.progress = clamp01((nowSec - entry.startSec) / DURATION[entry.phase]);
      if (entry.progress >= 1) {
        if (isLeaving(entry.phase)) {
          this.items.delete(entry.key);
        } else {
          entry.phase = 'present';
        }
      }
    }
  }

  entries(): PresenceEntry<T>[] {
    return [...this.items.values()];
  }

  get(key: string): PresenceEntry<T> | undefined {
    return this.items.get(key);
  }

  // 세션이 바뀌거나 스토어가 비었을 때. 다음 입력은 다시 "이미 있던 것" 이 된다.
  reset(): void {
    this.items.clear();
    this.initialized = false;
  }
}
```

- [ ] **Step 5: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/presence.test.ts`
Expected: PASS, `Tests  14 passed (14)`.

- [ ] **Step 6: `web/.oxlintrc.json` 전체 교체 (visual/ 에 gsap 금지 추가)**

```json
{
  "$schema": "./node_modules/oxlint/configuration_schema.json",
  "plugins": ["react", "typescript", "oxc"],
  "rules": {
    "react/rules-of-hooks": "error",
    "react/only-export-components": ["warn", { "allowConstantExport": true }]
  },
  "overrides": [
    {
      "files": ["src/dashboard/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "patterns": [
              {
                "group": ["*stream/*", "*/stream", "**/stream/**"],
                "message": "dashboard/ reads only state/, never stream/ directly."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/scene/**", "src/shell/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "patterns": [
              {
                "group": ["*stream/*", "*/stream", "**/stream/**"],
                "message": "scene/ and shell/ read only state/, never stream/ directly."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/visual/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "paths": [
              { "name": "react", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "react-dom", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "three", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "zustand", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "gsap", "message": "visual/ is pure: transitions are computed, not tweened. GSAP lives in scene/." }
            ],
            "patterns": [
              {
                "group": ["@react-three/*", "*snapshotStore*", "*stream/*", "**/stream/**"],
                "message": "visual/ is pure: it may import types from state/interpolator and protocol/ only."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/state/interpolator.ts", "src/stream/SystemStream.ts"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "paths": [
              {
                "name": "react",
                "message": "This file must not import react or zustand."
              },
              {
                "name": "zustand",
                "message": "This file must not import react or zustand."
              }
            ]
          }
        ]
      }
    }
  ]
}
```

확인: `src/visual/_probe.ts` 에 `import gsap from 'gsap'; void gsap;` 를 쓰고 `npx oxlint src/visual/_probe.ts` → `error eslint(no-restricted-imports): 'gsap' import is restricted` 가 나온 뒤 파일을 지운다.

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  159 passed (159)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 8: 커밋**

```
git add src/visual/easing.ts src/visual/presence.ts tests/visual/presence.test.ts .oxlintrc.json
git commit -m "feat(web): track what is on screen through forming, fading and collapsing"
```

---

## Task 4: lifecycle 소비

**Files:**
- Create: `web/src/visual/lifecycleEvents.ts`
- Test: `web/tests/visual/lifecycleEvents.test.ts`

**Interfaces:**
- Consumes: `InterpolatedSnapshot` 타입 (`web/src/state/interpolator.ts`) — `seq`, `groups`, `lifecycle` 만 쓴다.
- Produces:
  - `type LifecycleEvent = { kind: 'group-born' | 'child-born' | 'group-died' | 'child-died'; key: string; pid: number }` (판별 유니언)
  - `class LifecycleConsumer { consume(snapshot: Pick<InterpolatedSnapshot, 'seq' | 'groups' | 'lifecycle'> | null): LifecycleEvent[]; reset(): void }`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/lifecycleEvents.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import type { ProcessGroup } from '../../src/protocol/schema';
import { LifecycleConsumer } from '../../src/visual/lifecycleEvents';

function group(name: string, rootPid: number, childPids: number[] = []): ProcessGroup {
  return {
    key: `${name}:${rootPid}`,
    name,
    root_pid: rootPid,
    cpu_pct: 0,
    mem_mb: 100,
    proc_count: 1 + childPids.length,
    thread_count: 1,
    started_at: 0,
    account: 'user',
    image_path: '',
    children: childPids.map((pid) => ({
      pid,
      name: `${name}-child`,
      role: 'child',
      cpu_pct: 0,
      mem_mb: 10,
      threads: 1,
    })),
  };
}

function snap(
  seq: number,
  groups: ProcessGroup[],
  spawned: number[] = [],
  terminated: number[] = [],
) {
  return {
    seq,
    groups,
    lifecycle: {
      spawned: spawned.map((pid) => ({ pid, ppid: 1, name: 'p.exe', group: '' })),
      terminated,
    },
  };
}

// 첫 스냅샷(기준선)을 이미 소비한 소비기.
function primed(groups: ProcessGroup[]): LifecycleConsumer {
  const consumer = new LifecycleConsumer();
  consumer.consume(snap(1, groups));
  return consumer;
}

describe('LifecycleConsumer', () => {
  it('treats the first snapshot as a baseline', () => {
    const consumer = new LifecycleConsumer();
    expect(consumer.consume(snap(1, [group('a.exe', 10, [11])], [11], [99]))).toEqual([]);
  });

  it('turns a spawned root of a visible group into group-born', () => {
    const consumer = primed([group('a.exe', 10)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10), group('b.exe', 20)], [20]));
    expect(events).toEqual([{ kind: 'group-born', key: 'b.exe:20', pid: 20 }]);
  });

  it('turns a spawned child of a visible group into child-born', () => {
    const consumer = primed([group('a.exe', 10)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10, [11])], [11]));
    expect(events).toEqual([{ kind: 'child-born', key: 'a.exe:10', pid: 11 }]);
  });

  it('maps a terminated root through the previous snapshot to group-died', () => {
    // 종료된 그룹은 현재 스냅샷에 없다 — 직전 스냅샷에서 찾아야 한다.
    const consumer = primed([group('a.exe', 10), group('b.exe', 20)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10)], [], [20]));
    expect(events).toEqual([{ kind: 'group-died', key: 'b.exe:20', pid: 20 }]);
  });

  it('maps a terminated child through the previous snapshot to child-died', () => {
    const consumer = primed([group('a.exe', 10, [11, 12])]);
    const events = consumer.consume(snap(2, [group('a.exe', 10, [12])], [], [11]));
    expect(events).toEqual([{ kind: 'child-died', key: 'a.exe:10', pid: 11 }]);
  });

  it('ignores processes outside the visible groups', () => {
    const consumer = primed([group('a.exe', 10)]);
    expect(consumer.consume(snap(2, [group('a.exe', 10)], [500], [600]))).toEqual([]);
  });

  it('yields each snapshot’s events only once', () => {
    const consumer = primed([group('a.exe', 10)]);
    const next = snap(2, [group('a.exe', 10, [11])], [11]);
    expect(consumer.consume(next)).toHaveLength(1);
    expect(consumer.consume(next)).toEqual([]);
    expect(consumer.consume(next)).toEqual([]);
  });

  it('starts over after a null snapshot', () => {
    const consumer = primed([group('a.exe', 10, [11])]);
    expect(consumer.consume(null)).toEqual([]);
    // 다음 스냅샷은 다시 기준선이다. 직전 스냅샷의 자식 11 을 기억하지 않는다.
    expect(consumer.consume(snap(7, [group('a.exe', 10)], [], [11]))).toEqual([]);
  });

  it('starts over after reset', () => {
    const consumer = primed([group('a.exe', 10, [11])]);
    consumer.reset();
    expect(consumer.consume(snap(2, [group('a.exe', 10)], [], [11]))).toEqual([]);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/lifecycleEvents.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/lifecycleEvents"`.

- [ ] **Step 3: `web/src/visual/lifecycleEvents.ts`**

```ts
import type { InterpolatedSnapshot } from '../state/interpolator';

// M5 스펙 6절. lifecycle 은 한 주기 내내 같은 값으로 통과한다. 매 프레임 그대로
// 읽으면 같은 생성을 60 번 연출하므로, seq 가 바뀔 때 한 번만 이벤트로 바꾼다.

export type LifecycleEvent =
  // 보이는 그룹의 루트가 새로 생겼다.
  | { kind: 'group-born'; key: string; pid: number }
  // 보이는 그룹 안에서 자식이 새로 생겼다.
  | { kind: 'child-born'; key: string; pid: number }
  // 직전에 보이던 그룹의 루트가 종료됐다.
  | { kind: 'group-died'; key: string; pid: number }
  // 직전에 보이던 그룹의 자식이 종료됐다.
  | { kind: 'child-died'; key: string; pid: number };

type SnapshotLike = Pick<InterpolatedSnapshot, 'seq' | 'groups' | 'lifecycle'>;

interface Membership {
  key: string;
  isRoot: boolean;
}

function membershipOf(snapshot: SnapshotLike): Map<number, Membership> {
  const map = new Map<number, Membership>();
  for (const group of snapshot.groups) {
    map.set(group.root_pid, { key: group.key, isRoot: true });
    for (const child of group.children) {
      map.set(child.pid, { key: group.key, isRoot: false });
    }
  }
  return map;
}

export class LifecycleConsumer {
  private lastSeq: number | null = null;
  // 직전에 소비한 스냅샷의 pid → 그룹. 종료된 프로세스는 현재 스냅샷에 없으므로
  // 어느 그룹이었는지는 직전 스냅샷에서 찾아야 한다.
  private previous = new Map<number, Membership>();

  consume(snapshot: SnapshotLike | null): LifecycleEvent[] {
    if (snapshot === null) {
      this.reset();
      return [];
    }
    if (snapshot.seq === this.lastSeq) {
      return [];
    }

    const current = membershipOf(snapshot);
    const isFirst = this.lastSeq === null;
    const events: LifecycleEvent[] = [];

    // 첫 스냅샷은 기준선이다. 직전 스냅샷이 없어 종료를 그룹에 대응시킬 수 없고,
    // 앱을 연 순간 이미 있던 것들을 "생성" 으로 연출하지도 않는다.
    if (!isFirst) {
      for (const spawned of snapshot.lifecycle.spawned) {
        const member = current.get(spawned.pid);
        if (member?.isRoot) {
          events.push({ kind: 'group-born', key: member.key, pid: spawned.pid });
        } else if (member !== undefined) {
          events.push({ kind: 'child-born', key: member.key, pid: spawned.pid });
        }
        // 목록 밖 프로세스의 생성은 연출하지 않는다.
      }
      for (const pid of snapshot.lifecycle.terminated) {
        const member = this.previous.get(pid);
        if (member === undefined) {
          continue;
        }
        events.push({
          kind: member.isRoot ? 'group-died' : 'child-died',
          key: member.key,
          pid,
        });
      }
    }

    this.lastSeq = snapshot.seq;
    this.previous = current;
    return events;
  }

  reset(): void {
    this.lastSeq = null;
    this.previous = new Map();
  }
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/lifecycleEvents.test.ts`
Expected: PASS, `Tests  9 passed (9)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  168 passed (168)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 6: 커밋**

```
git add src/visual/lifecycleEvents.ts tests/visual/lifecycleEvents.test.ts
git commit -m "feat(web): turn each snapshot's lifecycle into group and child events once"
```

---

## Task 5: 버스트·궤도·카메라 계산과 배치 보완

**Files:**
- Create: `web/src/visual/bursts.ts`, `web/src/visual/orbits.ts`, `web/src/visual/camera.ts`
- Modify (전체 교체): `web/src/visual/layout.ts`, `web/src/visual/frameCache.ts`
- Modify (한 줄): `web/src/scene/SceneRoot.tsx`
- Test: `web/tests/visual/bursts.test.ts`, `web/tests/visual/orbitsCamera.test.ts`
- Test (전체 교체): `web/tests/visual/layout.test.ts`, `web/tests/visual/frameCache.test.ts`

**Interfaces:**
- Consumes: `clamp01`, `easeInCubic`, `easeOutCubic` (Task 3), `hash01` (`visual/hash.ts`), `Vec3` (`visual/layout.ts`).
- Produces:
  - `bursts.ts`: `type BurstKind = 'converge' | 'implode' | 'scatter'`, `interface BurstAnchor { kind: 'group' | 'satellite'; key: string }`, `interface BurstSpec { kind; anchor; count; durationSec; radius; color: [number, number, number]; seed }`, `interface Burst extends BurstSpec { id; startSec; directions: Float32Array; jitter: Float32Array; lastCenter: Vec3 | null }`, 상수 `GROUP_FORM`, `GROUP_COLLAPSE`, `CHILD_BORN`, `CHILD_DIED`, `POOL_CAPACITY = 2048`, `CONVERGE_START`, `SCATTER_DISTANCE`, `particleAt(burst, index, nowSec, center): ParticleState | null`, `class BurstPool { constructor(capacity?); add(spec, startSec): Burst; active(nowSec): readonly Burst[]; used(): number; clear(): void }`
  - `orbits.ts`: `interface Orbit { radius; tilt; node; phase; angularSpeed }`, `SATELLITE_SCALE = 0.45`, `orbitFor(parentRadius: number, pid: number): Orbit`, `satellitePosition(orbit, center: Vec3, timeSec: number, spread: number): Vec3`
  - `camera.ts`: `interface Pose { position: Vec3; target: Vec3 }`, `OVERVIEW_POSE`, `focusDistance(radius)`, `focusDirection(camera: Vec3, node: Vec3): Vec3`, `focusPose(node: Vec3, radius: number, direction: Vec3): Pose`, `blendPose(from: Pose, to: Pose, t: number): Pose`
  - `layout.ts`: 새 상수 `OUTSIDE_MARGIN = 4`. 사전 수렴이 끝난 뒤 들어온 key 는 무리의 가장 먼 노드 거리 + 4 인 구면(key 해시 방향)에 놓인다.
  - `frameCache.ts`: `layoutNodesFrom(groups: Iterable<Pick<InterpolatedGroup, 'key' | 'mem_mb'>>): LayoutNode[]` — 인자가 `FrameCache` 에서 그룹 목록으로 바뀐다.

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/bursts.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import {
  BurstPool,
  CHILD_DIED,
  CONVERGE_START,
  GROUP_COLLAPSE,
  GROUP_FORM,
  SCATTER_DISTANCE,
  particleAt,
  type BurstSpec,
} from '../../src/visual/bursts';

const ORIGIN = { x: 0, y: 0, z: 0 };

function spec(overrides: Partial<BurstSpec> = {}): BurstSpec {
  return {
    ...GROUP_FORM,
    anchor: { kind: 'group', key: 'a.exe:1' },
    radius: 2,
    color: [1, 1, 1],
    seed: 7,
    ...overrides,
  };
}

function distance(p: { x: number; y: number; z: number }, c = ORIGIN): number {
  return Math.hypot(p.x - c.x, p.y - c.y, p.z - c.z);
}

describe('particleAt', () => {
  it('starts a converge particle far out and brings it to the center', () => {
    const burst = new BurstPool().add(spec(), 10);
    const start = particleAt(burst, 0, 10, ORIGIN)!;
    const almostEnd = particleAt(burst, 0, 10 + burst.durationSec * 0.999, ORIGIN)!;

    // 출발 거리 = 반지름 × 3.5 × 흩뜨림(0.7~1.3).
    expect(distance(start)).toBeGreaterThanOrEqual(2 * CONVERGE_START * 0.7 - 1e-6);
    expect(distance(start)).toBeLessThanOrEqual(2 * CONVERGE_START * 1.3 + 1e-6);
    expect(distance(almostEnd)).toBeLessThan(0.05);
  });

  it('pulls an implode particle from the surface into the center while it fades', () => {
    const burst = new BurstPool().add(spec({ ...CHILD_DIED }), 0);
    const start = particleAt(burst, 3, 0, ORIGIN)!;
    const late = particleAt(burst, 3, burst.durationSec * 0.9, ORIGIN)!;

    expect(distance(start)).toBeLessThanOrEqual(2 * 1.3 + 1e-6);
    expect(distance(late)).toBeLessThan(distance(start));
    expect(start.alpha).toBeCloseTo(1, 6);
    expect(late.alpha).toBeCloseTo(0.1, 6);
  });

  it('throws a scatter particle outward from the surface', () => {
    const burst = new BurstPool().add(spec({ ...GROUP_COLLAPSE }), 0);
    const start = particleAt(burst, 5, 0, ORIGIN)!;
    const late = particleAt(burst, 5, burst.durationSec * 0.9, ORIGIN)!;

    expect(distance(start)).toBeCloseTo(2, 6);
    expect(distance(late)).toBeGreaterThan(distance(start));
    expect(distance(late)).toBeLessThanOrEqual(2 * (1 + SCATTER_DISTANCE * 1.3) + 1e-6);
  });

  it('follows the center it is given', () => {
    const burst = new BurstPool().add(spec(), 0);
    const here = particleAt(burst, 2, 0.3, ORIGIN)!;
    const there = particleAt(burst, 2, 0.3, { x: 10, y: -4, z: 1 })!;
    expect(there.x - here.x).toBeCloseTo(10, 6);
    expect(there.y - here.y).toBeCloseTo(-4, 6);
    expect(there.z - here.z).toBeCloseTo(1, 6);
  });

  it('is inactive before its start and from its end on', () => {
    const burst = new BurstPool().add(spec(), 5);
    expect(particleAt(burst, 0, 4.99, ORIGIN)).toBeNull();
    expect(particleAt(burst, 0, 5 + burst.durationSec, ORIGIN)).toBeNull();
    expect(particleAt(burst, burst.count, 5.1, ORIGIN)).toBeNull();
  });

  it('places the same particles for the same seed', () => {
    const a = new BurstPool().add(spec({ seed: 3 }), 0);
    const b = new BurstPool().add(spec({ seed: 3 }), 0);
    const c = new BurstPool().add(spec({ seed: 4 }), 0);
    expect(particleAt(a, 9, 0.2, ORIGIN)).toEqual(particleAt(b, 9, 0.2, ORIGIN));
    expect(particleAt(a, 9, 0.2, ORIGIN)).not.toEqual(particleAt(c, 9, 0.2, ORIGIN));
  });
});

describe('BurstPool', () => {
  it('drops finished bursts', () => {
    const pool = new BurstPool();
    pool.add(spec({ durationSec: 1 }), 0);
    pool.add(spec({ durationSec: 3 }), 0);
    expect(pool.active(2)).toHaveLength(1);
    expect(pool.used()).toBe(160);
  });

  it('drops the oldest bursts when a new one would overflow the capacity', () => {
    const pool = new BurstPool(400);
    const first = pool.add(spec({ count: 200 }), 0);
    const second = pool.add(spec({ count: 150 }), 0.1);
    const third = pool.add(spec({ count: 100 }), 0.2);

    const ids = pool.active(0.3).map((b) => b.id);
    expect(ids).toEqual([second.id, third.id]);
    expect(ids).not.toContain(first.id);
    expect(pool.used()).toBeLessThanOrEqual(400);
  });

  it('clamps a single burst larger than the whole pool', () => {
    const pool = new BurstPool(100);
    expect(pool.add(spec({ count: 500 }), 0).count).toBe(100);
  });

  it('empties on clear', () => {
    const pool = new BurstPool();
    pool.add(spec(), 0);
    pool.clear();
    expect(pool.active(0)).toEqual([]);
  });
});
```

- [ ] **Step 2: 실패하는 테스트 — `web/tests/visual/orbitsCamera.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import {
  OVERVIEW_POSE,
  blendPose,
  focusDirection,
  focusDistance,
  focusPose,
} from '../../src/visual/camera';
import { orbitFor, satellitePosition } from '../../src/visual/orbits';

const ORIGIN = { x: 0, y: 0, z: 0 };

function distance(a: { x: number; y: number; z: number }, b = ORIGIN): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

describe('orbitFor', () => {
  it('is the same for the same pid', () => {
    expect(orbitFor(2, 1234)).toEqual(orbitFor(2, 1234));
  });

  it('keeps the orbit radius on one of four rings outside the parent', () => {
    for (let pid = 100; pid < 300; pid += 1) {
      const orbit = orbitFor(2, pid);
      const ring = (orbit.radius - (2 * 1.8 + 1.2)) / 0.35;
      expect(Number.isInteger(Math.round(ring * 1e6) / 1e6)).toBe(true);
      expect(ring).toBeGreaterThanOrEqual(0);
      expect(ring).toBeLessThanOrEqual(3);
    }
  });

  it('turns outer orbits more slowly than inner ones', () => {
    const orbits = Array.from({ length: 60 }, (_, i) => orbitFor(2, 1000 + i));
    const inner = orbits.reduce((a, b) => (a.radius < b.radius ? a : b));
    const outer = orbits.reduce((a, b) => (a.radius > b.radius ? a : b));
    expect(inner.angularSpeed).toBeGreaterThan(outer.angularSpeed);
  });
});

describe('satellitePosition', () => {
  it('sits on its orbit radius when fully spread', () => {
    const orbit = orbitFor(2, 77);
    for (let t = 0; t < 20; t += 1.3) {
      expect(distance(satellitePosition(orbit, ORIGIN, t, 1))).toBeCloseTo(orbit.radius, 6);
    }
  });

  it('starts at the parent center and scales out with spread', () => {
    const orbit = orbitFor(2, 77);
    expect(distance(satellitePosition(orbit, ORIGIN, 3, 0))).toBeCloseTo(0, 6);
    expect(distance(satellitePosition(orbit, ORIGIN, 3, 0.5))).toBeCloseTo(orbit.radius / 2, 6);
  });

  it('is measured from the center it is given', () => {
    const orbit = orbitFor(2, 77);
    const center = { x: 5, y: -2, z: 9 };
    expect(distance(satellitePosition(orbit, center, 3, 1), center)).toBeCloseTo(orbit.radius, 6);
  });

  it('moves along the orbit over time', () => {
    const orbit = orbitFor(2, 77);
    expect(satellitePosition(orbit, ORIGIN, 0, 1)).not.toEqual(satellitePosition(orbit, ORIGIN, 1, 1));
  });
});

describe('camera', () => {
  it('backs away from the node by radius × 7 + 6', () => {
    expect(focusDistance(2)).toBe(20);
    const pose = focusPose({ x: 1, y: 2, z: 3 }, 2, { x: 0, y: 0, z: 1 });
    expect(pose.position).toEqual({ x: 1, y: 2, z: 23 });
    expect(pose.target).toEqual({ x: 1, y: 2, z: 3 });
  });

  it('points from the node toward the camera', () => {
    const d = focusDirection({ x: 0, y: 0, z: 10 }, { x: 0, y: 0, z: 4 });
    expect(d).toEqual({ x: 0, y: 0, z: 1 });
    expect(distance(focusDirection({ x: 3, y: 4, z: 0 }, ORIGIN))).toBeCloseTo(1, 6);
  });

  it('falls back to +z when the camera sits on the node', () => {
    expect(focusDirection({ x: 1, y: 1, z: 1 }, { x: 1, y: 1, z: 1 })).toEqual({ x: 0, y: 0, z: 1 });
  });

  it('blends two poses, returning each end exactly at 0 and 1', () => {
    const near = focusPose({ x: 4, y: 0, z: 0 }, 1, { x: 1, y: 0, z: 0 });
    expect(blendPose(OVERVIEW_POSE, near, 0)).toEqual(OVERVIEW_POSE);
    expect(blendPose(OVERVIEW_POSE, near, 1)).toEqual(near);
    expect(blendPose(OVERVIEW_POSE, near, 0.5).target).toEqual({ x: 2, y: 0, z: 0 });
  });
});
```

- [ ] **Step 3: 테스트 교체 — `web/tests/visual/layout.test.ts`** (기존 테스트 + 바깥 배치 2개)

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import {
  FIXED_STEP,
  LayoutSim,
  MAX_DT,
  OUTSIDE_MARGIN,
  SPAWN_RADIUS,
  floatOffset,
  floatingPosition,
  type LayoutNode,
  type Vec3,
} from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처의 40 개 그룹. 반지름 0.89 ~ 4.05.
const nodes: LayoutNode[] = fixture.groups.map((group) => ({
  key: group.key,
  radius: radiusFor(group.mem_mb),
}));

function length(v: Vec3): number {
  return Math.hypot(v.x, v.y, v.z);
}

function positionsOf(sim: LayoutSim, list: readonly LayoutNode[]): Vec3[] {
  return list.map((node) => ({ ...sim.position(node.key)! }));
}

// 첫 호출의 사전 수렴 뒤에 10 초를 더 돌린다.
function settled(list: readonly LayoutNode[]): LayoutSim {
  const sim = new LayoutSim();
  for (let i = 0; i < 600; i += 1) {
    sim.step(list, FIXED_STEP);
  }
  return sim;
}

// 어떤 두 구도 서로를 파고들지 않는다. COLLISION_GAP 은 여유분이다.
function expectApart(sim: LayoutSim, list: readonly LayoutNode[]): void {
  const positions = positionsOf(sim, list);
  for (let i = 0; i < list.length; i += 1) {
    for (let j = i + 1; j < list.length; j += 1) {
      const d = length({
        x: positions[i].x - positions[j].x,
        y: positions[i].y - positions[j].y,
        z: positions[i].z - positions[j].z,
      });
      expect(d).toBeGreaterThan(list[i].radius + list[j].radius);
    }
  }
}

describe('LayoutSim', () => {
  it('keeps every pair of spheres apart once settled', () => {
    expectApart(settled(nodes), nodes);
  });

  it('keeps the centroid near the origin', () => {
    const positions = positionsOf(settled(nodes), nodes);
    const centroid = positions.reduce(
      (sum, p) => ({
        x: sum.x + p.x / positions.length,
        y: sum.y + p.y / positions.length,
        z: sum.z + p.z / positions.length,
      }),
      { x: 0, y: 0, z: 0 },
    );
    expect(length(centroid)).toBeLessThan(2);
  });

  it('pulls the ten largest bodies closer to the center than the ten smallest', () => {
    const sim = settled(nodes);
    const bySize = [...nodes].sort((a, b) => b.radius - a.radius);
    const meanDistance = (list: LayoutNode[]) =>
      list.reduce((sum, node) => sum + length(sim.position(node.key)!), 0) / list.length;

    expect(meanDistance(bySize.slice(0, 10))).toBeLessThan(meanDistance(bySize.slice(-10)));
  });

  it('is already settled after the very first step', () => {
    // 첫 화면이 한 점에서 퍼져 나오지 않는다 — 첫 호출이 SETTLE_STEPS 만큼 미리 돈다.
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expectApart(sim, nodes);
  });

  it('settles to the same shape on the first step regardless of the frame time', () => {
    // 새로고침해도 같은 모양 — 첫 호출은 dt 와 무관하다.
    const a = new LayoutSim();
    const b = new LayoutSim();
    a.step(nodes, 1 / 144);
    b.step(nodes, 1 / 24);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('advances at most MAX_DT per call', () => {
    // 백그라운드 탭에서 돌아온 5 초짜리 프레임을 한 번에 따라잡지 않는다.
    const a = settled(nodes);
    const b = settled(nodes);
    a.step(nodes, 5);
    b.step(nodes, MAX_DT);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('treats a NaN or negative frame time as no time at all', () => {
    const reference = settled(nodes);
    const sim = settled(nodes);
    sim.step(nodes, Number.NaN);
    sim.step(nodes, -1);
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));

    // 잘못된 dt 가 누적기를 오염시키면 이후 정상 프레임에서도 멈춰 버린다.
    for (let i = 0; i < 10; i += 1) {
      reference.step(nodes, FIXED_STEP);
      sim.step(nodes, FIXED_STEP);
    }
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));
  });

  it('produces identical positions for identical inputs', () => {
    expect(positionsOf(settled(nodes), nodes)).toEqual(positionsOf(settled(nodes), nodes));
  });

  it('adds bodies for new keys and forgets keys that disappear', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expect(sim.size).toBe(40);

    const fewer = nodes.slice(0, 30);
    sim.step(fewer, FIXED_STEP);
    expect(sim.size).toBe(30);
    expect(sim.position(nodes[35].key)).toBeUndefined();

    const newcomer: LayoutNode = { key: 'newcomer.exe:9999', radius: 1 };
    sim.step([...fewer, newcomer], FIXED_STEP);
    expect(sim.size).toBe(31);
    expect(sim.position(newcomer.key)).toBeDefined();
  });

  it('stays finite and bounded under a long run of huge frame times', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    for (let i = 0; i < 200; i += 1) {
      sim.step(nodes, 5);
    }

    for (const p of positionsOf(sim, nodes)) {
      expect(Number.isFinite(p.x) && Number.isFinite(p.y) && Number.isFinite(p.z)).toBe(true);
      expect(length(p)).toBeLessThan(40);
    }
  });

  it('keeps drawing a lone body toward the center', () => {
    const lone: LayoutNode[] = [{ key: 'solo.exe:1', radius: 1 }];
    const sim = new LayoutSim();
    sim.step(lone, FIXED_STEP);
    const before = length(sim.position('solo.exe:1')!);
    for (let i = 0; i < 600; i += 1) {
      sim.step(lone, FIXED_STEP);
    }
    expect(length(sim.position('solo.exe:1')!)).toBeLessThan(before);
  });

  it('places a key that arrives after settling outside the whole cluster', () => {
    // 무리 안쪽에서 생기면 형성 연출이 다른 천체에 가려진다 (M5 스펙 8절).
    const sim = settled(nodes);
    const farthest = Math.max(...positionsOf(sim, nodes).map(length));

    const newcomer: LayoutNode = { key: 'newcomer.exe:4242', radius: 1 };
    sim.step([...nodes, newcomer], 0);

    expect(length(sim.position(newcomer.key)!)).toBeCloseTo(farthest + OUTSIDE_MARGIN, 6);
  });

  it('still spreads the very first keys inside the spawn sphere', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    // 사전 수렴이 끝난 모양은 반지름 18 구 근처에 머문다.
    expect(Math.max(...positionsOf(sim, nodes).map(length))).toBeLessThan(SPAWN_RADIUS + 5);
  });

  it('does nothing with an empty node list', () => {
    const sim = new LayoutSim();
    sim.step([], FIXED_STEP);
    expect(sim.size).toBe(0);
  });
});

describe('floatOffset', () => {
  it('stays within the float amplitude on every axis', () => {
    for (let t = 0; t < 30; t += 0.37) {
      const offset = floatOffset('app.exe:100', t);
      for (const value of [offset.x, offset.y, offset.z]) {
        expect(Math.abs(value)).toBeLessThanOrEqual(0.2 + 1e-9);
      }
    }
  });

  it('differs between keys at the same moment', () => {
    expect(floatOffset('a.exe:1', 3)).not.toEqual(floatOffset('b.exe:2', 3));
  });
});

describe('floatingPosition', () => {
  it('adds the float offset to the simulated position', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    const key = nodes[0].key;
    const base = sim.position(key)!;
    const offset = floatOffset(key, 4);

    expect(floatingPosition(sim, key, 4)).toEqual({
      x: base.x + offset.x,
      y: base.y + offset.y,
      z: base.z + offset.z,
    });
  });

  it('returns undefined for an unknown key', () => {
    expect(floatingPosition(new LayoutSim(), 'nope', 0)).toBeUndefined();
  });
});
```

- [ ] **Step 4: 테스트 교체 — `web/tests/visual/frameCache.test.ts`** (`layoutNodesFrom` 호출만 바뀜)

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import { sample } from '../../src/state/interpolator';
import {
  createFrameCache,
  layoutNodesFrom,
  updateFrameCache,
} from '../../src/visual/frameCache';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처를 보간기에 한 번 통과시킨 프레임.
const frame = sample({ previous: null, current: fixture, arrivedAt: 0, intervalMs: 1000 }, 0)!;

describe('frameCache', () => {
  it('looks up every group of the real fixture by key', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);

    expect(cache.byKey.size).toBe(fixture.groups.length);
    for (const group of fixture.groups) {
      expect(cache.byKey.get(group.key)?.name).toBe(group.name);
    }
  });

  it('takes the core count from the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    expect(cache.coreCount).toBe(fixture.cores.length);
  });

  it('never reports fewer than one core', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, { ...frame, cores: [] }, 1000);
    expect(cache.coreCount).toBe(1);
  });

  it('keeps a null cpu_pct as null', () => {
    const cache = createFrameCache();
    const withNull = {
      ...frame,
      groups: frame.groups.map((g, i) => (i === 0 ? { ...g, cpu_pct: null } : g)),
    };
    updateFrameCache(cache, withNull, 1000);
    expect(cache.byKey.get(frame.groups[0].key)?.cpu_pct).toBeNull();
  });

  it('empties the lookup when the snapshot goes away', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, null, 1016);

    expect(cache.snapshot).toBeNull();
    expect(cache.byKey.size).toBe(0);
  });

  it('drops groups that are no longer in the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, { ...frame, groups: frame.groups.slice(0, 5) }, 1016);
    expect(cache.byKey.size).toBe(5);
  });

  it('reuses the same Map instead of allocating a new one each frame', () => {
    const cache = createFrameCache();
    const map = cache.byKey;
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, frame, 1016);
    expect(cache.byKey).toBe(map);
  });

  it('reports seconds and the gap since the previous update', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    expect(cache.timeSec).toBe(2);
    expect(cache.dtSec).toBe(0);

    updateFrameCache(cache, frame, 2016);
    expect(cache.timeSec).toBeCloseTo(2.016, 9);
    expect(cache.dtSec).toBeCloseTo(0.016, 9);
  });

  it('never reports a negative gap', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    updateFrameCache(cache, frame, 1000);
    expect(cache.dtSec).toBe(0);
  });

  it('turns groups into layout nodes with their radius', () => {
    const nodes = layoutNodesFrom(frame.groups);

    expect(nodes).toHaveLength(fixture.groups.length);
    expect(nodes[0]).toEqual({
      key: fixture.groups[0].key,
      radius: radiusFor(fixture.groups[0].mem_mb),
    });
  });
});
```

- [ ] **Step 5: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual`
Expected: `bursts`·`orbitsCamera` 는 모듈을 찾지 못해 실패, `layout` 은 `OUTSIDE_MARGIN` 이 없어 실패, `frameCache` 의 `turns groups into layout nodes…` 는 인자 형식 때문에 실패.

- [ ] **Step 6: `web/src/visual/bursts.ts`**

```ts
import { clamp01, easeInCubic, easeOutCubic } from './easing';
import type { Vec3 } from './layout';

// M5 스펙 7절. 버스트 하나는 한 천체(또는 위성)에서 재생되는 입자 묶음이다.
// 입자 위치는 시각과 기준점의 순수 함수다. 모든 버스트가 풀 하나를 나눠 쓴다.

export type BurstKind = 'converge' | 'implode' | 'scatter';

export interface BurstAnchor {
  kind: 'group' | 'satellite';
  key: string;
}

export interface BurstSpec {
  kind: BurstKind;
  anchor: BurstAnchor;
  count: number;
  durationSec: number;
  // 기준 천체의 반지름. 입자의 출발·도착 거리가 여기에 비례한다.
  radius: number;
  // 0~1 RGB.
  color: [number, number, number];
  seed: number;
}

export interface Burst extends BurstSpec {
  id: number;
  startSec: number;
  // 입자별 단위 방향(3 × count)과 거리 흩뜨림(count). 생성 시 한 번 만든다.
  directions: Float32Array;
  jitter: Float32Array;
  // 기준 천체가 사라져도 입자가 제자리에서 끝나도록 마지막 기준점을 기억한다.
  lastCenter: Vec3 | null;
}

// 7절 표.
export const GROUP_FORM: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'converge',
  count: 160,
  durationSec: 1.2,
};
export const GROUP_COLLAPSE: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'scatter',
  count: 200,
  durationSec: 1.0,
};
export const CHILD_BORN: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'converge',
  count: 24,
  durationSec: 0.8,
};
export const CHILD_DIED: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'implode',
  count: 24,
  durationSec: 0.8,
};

export const POOL_CAPACITY = 2048;
// converge 입자가 출발하는 거리 (반지름 배수).
export const CONVERGE_START = 3.5;
// scatter 입자가 흩어지는 거리 (반지름 배수, 표면에서부터).
export const SCATTER_DISTANCE = 2.5;

// 작고 결정적인 의사난수. 같은 seed 는 같은 입자 배치를 만든다.
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function makeParticles(count: number, seed: number): Pick<Burst, 'directions' | 'jitter'> {
  const random = mulberry32(seed);
  const directions = new Float32Array(count * 3);
  const jitter = new Float32Array(count);
  for (let i = 0; i < count; i += 1) {
    // 구면 위 균일한 방향.
    const theta = 2 * Math.PI * random();
    const z = 2 * random() - 1;
    const r = Math.sqrt(1 - z * z);
    directions[i * 3] = r * Math.cos(theta);
    directions[i * 3 + 1] = z;
    directions[i * 3 + 2] = r * Math.sin(theta);
    jitter[i] = 0.7 + 0.6 * random();
  }
  return { directions, jitter };
}

export interface ParticleState {
  x: number;
  y: number;
  z: number;
  // 0~1. 가산 블렌딩이므로 색에 곱해 쓴다.
  alpha: number;
}

// 입자 i 의 현재 상태. 버스트의 시간 밖이면 null.
export function particleAt(
  burst: Burst,
  index: number,
  nowSec: number,
  center: Vec3,
): ParticleState | null {
  const elapsed = nowSec - burst.startSec;
  if (elapsed < 0 || elapsed >= burst.durationSec || index >= burst.count) {
    return null;
  }
  const t = clamp01(elapsed / burst.durationSec);
  const j = burst.jitter[index];
  let distance: number;
  let alpha: number;
  switch (burst.kind) {
    case 'converge':
      // 멀리서 천천히 출발해 중심에 빨려 들 듯 도착한다. 도착 직전에 가장 밝다.
      distance = burst.radius * CONVERGE_START * j * (1 - easeInCubic(t));
      alpha = t < 0.8 ? t / 0.8 : (1 - t) / 0.2;
      break;
    case 'implode':
      distance = burst.radius * j * (1 - easeInCubic(t));
      alpha = 1 - t;
      break;
    case 'scatter':
      distance = burst.radius * (1 + SCATTER_DISTANCE * j * easeOutCubic(t));
      alpha = 1 - t;
      break;
  }
  return {
    x: center.x + burst.directions[index * 3] * distance,
    y: center.y + burst.directions[index * 3 + 1] * distance,
    z: center.z + burst.directions[index * 3 + 2] * distance,
    alpha,
  };
}

export class BurstPool {
  private bursts: Burst[] = [];
  private nextId = 1;
  private readonly capacity: number;

  constructor(capacity = POOL_CAPACITY) {
    this.capacity = capacity;
  }

  // 풀이 가득 차면 가장 오래된 버스트부터 버린다. 한 스냅샷에 이벤트가 몰려도
  // 화면이 입자로 뒤덮이지 않는다.
  add(spec: BurstSpec, startSec: number): Burst {
    const count = Math.min(spec.count, this.capacity);
    while (this.used() + count > this.capacity && this.bursts.length > 0) {
      this.bursts.shift();
    }
    const burst: Burst = {
      ...spec,
      count,
      id: this.nextId,
      startSec,
      lastCenter: null,
      ...makeParticles(count, spec.seed),
    };
    this.nextId += 1;
    this.bursts.push(burst);
    return burst;
  }

  // 끝난 버스트를 치우고 살아 있는 것을 돌려준다.
  active(nowSec: number): readonly Burst[] {
    this.bursts = this.bursts.filter((b) => nowSec < b.startSec + b.durationSec);
    return this.bursts;
  }

  used(): number {
    return this.bursts.reduce((sum, b) => sum + b.count, 0);
  }

  clear(): void {
    this.bursts = [];
  }
}
```

- [ ] **Step 7: `web/src/visual/orbits.ts`**

```ts
import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M5 스펙 9.4. Focus 한 그룹의 자식을 위성으로 돌린다. 궤도는 pid 해시로만
// 정해진다 — 자식 목록의 순서가 바뀌어도 위성이 다른 궤도로 튀지 않는다.

export interface Orbit {
  radius: number;
  // 궤도면을 x 축으로 기울인 각(라디안).
  tilt: number;
  // 기울인 궤도면을 y 축으로 돌린 각(라디안).
  node: number;
  phase: number;
  // rad/s.
  angularSpeed: number;
}

export const SATELLITE_SCALE = 0.45;
const RINGS = 4;
const RING_GAP = 0.35;
const BASE_SPEED = 0.25;

const SALT_TILT = 31;
const SALT_NODE = 32;
const SALT_PHASE = 33;
const SALT_RING = 34;

export function orbitFor(parentRadius: number, pid: number): Orbit {
  const key = String(pid);
  const ring = Math.floor(hash01(key, SALT_RING) * RINGS);
  const inner = parentRadius * 1.8;
  const radius = inner + 1.2 + RING_GAP * ring;
  return {
    radius,
    // ±54°. 모든 위성이 한 평면에 겹치지 않게 한다.
    tilt: (hash01(key, SALT_TILT) - 0.5) * Math.PI * 0.6,
    node: hash01(key, SALT_NODE) * 2 * Math.PI,
    phase: hash01(key, SALT_PHASE) * 2 * Math.PI,
    // 바깥 궤도일수록 느리다.
    angularSpeed: BASE_SPEED * (inner / radius),
  };
}

// spread 는 Focus 전환 진행도(0~1). 0 이면 부모 중심, 1 이면 제 궤도.
export function satellitePosition(
  orbit: Orbit,
  center: Vec3,
  timeSec: number,
  spread: number,
): Vec3 {
  const angle = orbit.phase + orbit.angularSpeed * timeSec;
  const r = orbit.radius * spread;
  // 궤도면(xz) 위의 점.
  const px = r * Math.cos(angle);
  const pz = r * Math.sin(angle);
  // x 축으로 tilt 만큼 기울인다.
  const y1 = -pz * Math.sin(orbit.tilt);
  const z1 = pz * Math.cos(orbit.tilt);
  // y 축으로 node 만큼 돌린다.
  const x2 = px * Math.cos(orbit.node) + z1 * Math.sin(orbit.node);
  const z2 = -px * Math.sin(orbit.node) + z1 * Math.cos(orbit.node);
  return { x: center.x + x2, y: center.y + y1, z: center.z + z2 };
}
```

- [ ] **Step 8: `web/src/visual/camera.ts`**

```ts
import type { Vec3 } from './layout';

// M5 스펙 9.2. GSAP 은 전환 진행도 하나만 움직인다. 카메라 자세는 매 프레임
// 이 함수들로 계산한다 — 그래서 떠다니는 천체를 따라갈 수 있다.

export interface Pose {
  position: Vec3;
  target: Vec3;
}

export const OVERVIEW_POSE: Pose = {
  position: { x: 0, y: 10, z: 58 },
  target: { x: 0, y: 0, z: 0 },
};

export const FOCUS_DISTANCE_PER_RADIUS = 7;
export const FOCUS_DISTANCE_BASE = 6;

export function focusDistance(radius: number): number {
  return radius * FOCUS_DISTANCE_PER_RADIUS + FOCUS_DISTANCE_BASE;
}

// 노드에서 카메라 쪽을 가리키는 단위 벡터. 전환 시작 때 한 번 정해 두면
// 카메라가 지금 보던 쪽에서 곧장 다가간다. 두 점이 겹치면 +z.
export function focusDirection(camera: Vec3, node: Vec3): Vec3 {
  const dx = camera.x - node.x;
  const dy = camera.y - node.y;
  const dz = camera.z - node.z;
  const length = Math.hypot(dx, dy, dz);
  if (length < 1e-6) {
    return { x: 0, y: 0, z: 1 };
  }
  return { x: dx / length, y: dy / length, z: dz / length };
}

export function focusPose(node: Vec3, radius: number, direction: Vec3): Pose {
  const d = focusDistance(radius);
  return {
    position: {
      x: node.x + direction.x * d,
      y: node.y + direction.y * d,
      z: node.z + direction.z * d,
    },
    target: { ...node },
  };
}

function lerpVec(a: Vec3, b: Vec3, t: number): Vec3 {
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t };
}

export function blendPose(from: Pose, to: Pose, t: number): Pose {
  return {
    position: lerpVec(from.position, to.position, t),
    target: lerpVec(from.target, to.target, t),
  };
}
```

- [ ] **Step 9: `web/src/visual/layout.ts` 전체 교체**

```ts
import { hash01 } from './hash';

// 스펙 6절. three 를 모르는 순수 모듈이다. 위치는 평범한 객체로 다룬다.
export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface LayoutNode {
  key: string;
  radius: number;
}

// 시뮬레이션은 항상 1/60 초 고정 스텝으로 진행한다. 프레임 간격을 그대로
// 쓰면 같은 데이터라도 프레임률에 따라 다른 모양으로 수렴한다.
export const FIXED_STEP = 1 / 60;
// 탭이 백그라운드에 있다 돌아오면 수 초짜리 dt 가 들어온다. 그만큼을
// 한 번에 따라잡으려 하면 한 프레임에 수백 스텝을 돌게 된다.
export const MAX_DT = 1 / 30;
// 빈 시뮬레이션에 처음 노드가 들어오면 이만큼 미리 돌려 둔다. 첫 화면이
// 한 점에서 퍼져 나오는 대신 이미 자리 잡은 모양으로 뜨고, 그 모양은
// 프레임 타이밍과 무관하게 같다.
export const SETTLE_STEPS = 600;

// 아래 상수들은 실제 픽스처 40 개로 돌려 정했다 (스펙 6절).
export const SPAWN_RADIUS = 18;
// 사전 수렴이 끝난 뒤 들어오는 key 는 무리의 가장 먼 노드보다 이만큼 바깥에
// 놓는다. 무리 안쪽에서 생기면 형성 연출이 다른 천체에 가려진다 (M5 스펙 8절).
export const OUTSIDE_MARGIN = 4;
export const COLLISION_GAP = 1.0;
export const COLLISION_STIFFNESS = 8;
export const REPULSION = 3;
export const GRAVITY = 0.05;
// 1/60 초당 속도에 곱하는 감쇠.
export const DAMPING = 0.9;

const SALT_X = 11;
const SALT_Y = 12;
const SALT_Z = 13;

interface Body {
  position: Vec3;
  velocity: Vec3;
}

// key 해시로 정한 단위 방향에 거리 r 을 곱한 점.
function hashedPoint(key: string, r: number): Vec3 {
  const theta = 2 * Math.PI * hash01(key, SALT_X);
  const phi = Math.acos(2 * hash01(key, SALT_Y) - 1);
  return {
    x: r * Math.sin(phi) * Math.cos(theta),
    y: r * Math.cos(phi),
    z: r * Math.sin(phi) * Math.sin(theta),
  };
}

// key 해시로 반지름 SPAWN_RADIUS 인 구 안의 한 점을 고른다.
function spawnPosition(key: string): Vec3 {
  return hashedPoint(key, SPAWN_RADIUS * Math.cbrt(hash01(key, SALT_Z)));
}

export class LayoutSim {
  private readonly bodies = new Map<string, Body>();
  private accumulator = 0;

  get size(): number {
    return this.bodies.size;
  }

  position(key: string): Vec3 | undefined {
    return this.bodies.get(key)?.position;
  }

  step(nodes: readonly LayoutNode[], dtSec: number): void {
    const wasEmpty = this.bodies.size === 0;
    this.sync(nodes, wasEmpty);
    if (nodes.length === 0) {
      return;
    }

    if (wasEmpty) {
      for (let i = 0; i < SETTLE_STEPS; i += 1) {
        this.integrate(nodes);
      }
      this.accumulator = 0;
      return;
    }

    // NaN 이 누적기에 들어가면 이후 모든 프레임이 멈춘다.
    const dt = Number.isFinite(dtSec) ? Math.min(Math.max(dtSec, 0), MAX_DT) : 0;
    this.accumulator += dt;
    while (this.accumulator >= FIXED_STEP) {
      this.integrate(nodes);
      this.accumulator -= FIXED_STEP;
    }
  }

  private sync(nodes: readonly LayoutNode[], wasEmpty: boolean): void {
    // 이미 자리 잡은 무리가 있으면 새 key 는 그 바깥에 놓는다.
    let outside = 0;
    if (!wasEmpty) {
      for (const body of this.bodies.values()) {
        outside = Math.max(outside, Math.hypot(body.position.x, body.position.y, body.position.z));
      }
      outside += OUTSIDE_MARGIN;
    }

    const present = new Set<string>();
    for (const node of nodes) {
      present.add(node.key);
      if (!this.bodies.has(node.key)) {
        this.bodies.set(node.key, {
          position: wasEmpty ? spawnPosition(node.key) : hashedPoint(node.key, outside),
          velocity: { x: 0, y: 0, z: 0 },
        });
      }
    }
    for (const key of this.bodies.keys()) {
      if (!present.has(key)) {
        this.bodies.delete(key);
      }
    }
  }

  private integrate(nodes: readonly LayoutNode[]): void {
    const bodies = nodes.map((node) => this.bodies.get(node.key)!);
    const acc = nodes.map(() => ({ x: 0, y: 0, z: 0 }));

    for (let i = 0; i < nodes.length; i += 1) {
      for (let j = i + 1; j < nodes.length; j += 1) {
        const a = bodies[i].position;
        const b = bodies[j].position;
        const dx = b.x - a.x;
        const dy = b.y - a.y;
        const dz = b.z - a.z;
        const d = Math.hypot(dx, dy, dz);

        // 두 노드가 정확히 같은 자리면 방향이 없다. x 축으로 떼어 낸다.
        let ux = 1;
        let uy = 0;
        let uz = 0;
        if (d > 1e-6) {
          ux = dx / d;
          uy = dy / d;
          uz = dz / d;
        }

        // 원거리 반발. d 가 1 보다 작을 때는 1 로 보아 힘이 무한히 커지지 않게 한다.
        let force = REPULSION / Math.max(d, 1) ** 2;
        const minDistance = nodes[i].radius + nodes[j].radius + COLLISION_GAP;
        if (d < minDistance) {
          force += COLLISION_STIFFNESS * (minDistance - d);
        }

        acc[i].x -= ux * force;
        acc[i].y -= uy * force;
        acc[i].z -= uz * force;
        acc[j].x += ux * force;
        acc[j].y += uy * force;
        acc[j].z += uz * force;
      }
    }

    const damping = DAMPING ** (FIXED_STEP * 60);
    for (let i = 0; i < nodes.length; i += 1) {
      const { position, velocity } = bodies[i];
      const r = nodes[i].radius;
      // 중심 인력은 반지름의 제곱(대략 표면적)에 비례한다. 큰 천체가 가운데로 모인다.
      const pull = GRAVITY * (0.5 + r * r);
      acc[i].x -= pull * position.x;
      acc[i].y -= pull * position.y;
      acc[i].z -= pull * position.z;

      velocity.x = (velocity.x + acc[i].x * FIXED_STEP) * damping;
      velocity.y = (velocity.y + acc[i].y * FIXED_STEP) * damping;
      velocity.z = (velocity.z + acc[i].z * FIXED_STEP) * damping;
      position.x += velocity.x * FIXED_STEP;
      position.y += velocity.y * FIXED_STEP;
      position.z += velocity.z * FIXED_STEP;
    }
  }
}

const FLOAT_AMPLITUDE = 0.2;
const SALT_FLOAT_PERIOD = 21;
const SALT_FLOAT_PHASE = 24;

// 부유. 시뮬레이션 상태에는 들어가지 않고 그릴 때만 더한다 — 계약서 7.1 절.
// 축마다 주기(6~10 초)와 위상이 key 해시로 다르다.
export function floatOffset(key: string, timeSec: number): Vec3 {
  const axis = (i: number) => {
    const period = 6 + 4 * hash01(key, SALT_FLOAT_PERIOD + i);
    const phase = 2 * Math.PI * hash01(key, SALT_FLOAT_PHASE + i);
    return FLOAT_AMPLITUDE * Math.sin((2 * Math.PI * timeSec) / period + phase);
  };
  return { x: axis(0), y: axis(1), z: axis(2) };
}

// 노드와 툴팁이 같은 자리를 가리키도록 둘 다 이 함수로 위치를 구한다.
export function floatingPosition(
  sim: LayoutSim,
  key: string,
  timeSec: number,
): Vec3 | undefined {
  const base = sim.position(key);
  if (base === undefined) {
    return undefined;
  }
  const offset = floatOffset(key, timeSec);
  return { x: base.x + offset.x, y: base.y + offset.y, z: base.z + offset.z };
}
```

- [ ] **Step 10: `web/src/visual/frameCache.ts` 전체 교체**

```ts
import type { InterpolatedGroup, InterpolatedSnapshot } from '../state/interpolator';
import type { LayoutNode } from './layout';
import { radiusFor } from './mapping';

// 스펙 4절. 장면 루트가 프레임마다 한 번 채우고, 노드들은 key 로 읽는다.
// React 상태가 아니라 가변 객체다 — 매 프레임 새로 만들지 않는다.
export interface FrameCache {
  snapshot: InterpolatedSnapshot | null;
  byKey: Map<string, InterpolatedGroup>;
  // activityFor 의 코어 수. 현재 스냅샷의 cores.length, 최소 1.
  coreCount: number;
  // performance.now() 기준 초. 노드의 부유와 툴팁이 같은 시각을 쓰게 한다.
  // 한 번도 갱신되지 않았으면 null.
  timeSec: number | null;
  // 직전 갱신과의 간격(초). 첫 갱신은 0.
  dtSec: number;
}

export function createFrameCache(): FrameCache {
  return { snapshot: null, byKey: new Map(), coreCount: 1, timeSec: null, dtSec: 0 };
}

export function updateFrameCache(
  cache: FrameCache,
  snapshot: InterpolatedSnapshot | null,
  nowMs: number,
): void {
  const timeSec = nowMs / 1000;
  cache.dtSec = cache.timeSec === null ? 0 : Math.max(0, timeSec - cache.timeSec);
  cache.timeSec = timeSec;

  cache.snapshot = snapshot;
  cache.byKey.clear();
  if (snapshot === null) {
    return;
  }
  for (const group of snapshot.groups) {
    cache.byKey.set(group.key, group);
  }
  cache.coreCount = Math.max(1, snapshot.cores.length);
}

// 레이아웃 시뮬레이션의 입력. M5 부터는 스냅샷의 그룹이 아니라 존재 추적기의
// 항목(떠나는 중인 그룹 포함)을 넘긴다 — 사라지는 천체도 연출이 끝날 때까지
// 제자리를 지켜야 한다.
export function layoutNodesFrom(
  groups: Iterable<Pick<InterpolatedGroup, 'key' | 'mem_mb'>>,
): LayoutNode[] {
  const nodes: LayoutNode[] = [];
  for (const group of groups) {
    nodes.push({ key: group.key, radius: radiusFor(group.mem_mb) });
  }
  return nodes;
}
```

- [ ] **Step 11: `web/src/scene/SceneRoot.tsx` 한 줄 수정**

`layoutNodesFrom` 의 인자가 바뀌었으므로 M4 의 호출을 맞춘다 (SceneRoot 는 Task 6 에서 전체 교체된다):

```
    context.layout.step(layoutNodesFrom(context.cache), context.cache.dtSec);
```

를

```
    context.layout.step(layoutNodesFrom(context.cache.byKey.values()), context.cache.dtSec);
```

로 바꾼다.

- [ ] **Step 12: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual`
Expected: 전부 통과.

- [ ] **Step 13: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected: `Tests  191 passed (191)`, typecheck 깨끗, lint 기존 경고 1개, build 성공.

- [ ] **Step 14: 커밋**

```
git add src/visual/bursts.ts src/visual/orbits.ts src/visual/camera.ts src/visual/layout.ts src/visual/frameCache.ts src/scene/SceneRoot.tsx tests/visual/bursts.test.ts tests/visual/orbitsCamera.test.ts tests/visual/layout.test.ts tests/visual/frameCache.test.ts
git commit -m "feat(web): compute particle bursts, satellite orbits and focus camera poses"
```

---

## Task 6: 장면 — 형성·페이드·붕괴, 파티클, Focus

**Files:**
- Modify: `web/package.json`, `web/package-lock.json` (gsap 설치)
- Create: `web/src/scene/framePriority.ts`, `web/src/scene/focusStore.ts`, `web/src/scene/CameraRig.tsx`, `web/src/scene/Satellites.tsx`, `web/src/scene/SatelliteNode.tsx`, `web/src/scene/Particles.tsx`, `web/src/scene/FocusPanel.tsx`
- Modify (전체 교체): `web/src/scene/sceneContext.ts`, `web/src/scene/nodeList.ts`, `web/src/scene/SceneRoot.tsx`, `web/src/scene/ProcessNode.tsx`, `web/src/scene/Tooltip.tsx`, `web/src/scene/Universe.tsx`, `web/src/shell/shell.css`
- Test (전체 교체): `web/tests/nodeList.test.ts`
- Test: `web/tests/focus.test.tsx`
- Modify: `web/README.md`

**Interfaces:**
- Consumes: Task 3~5 의 모든 `visual/` 모듈. `useInterpolated` (`state/useInterpolated.ts`), `formatMb`·`formatPct` (`dashboard/format.ts`).
- Produces:
  - `useFocusStore` — `{ focusedKey: string | null; focus(key); toggle(key); clear() }`
  - `FRAME_PRIORITY = { scene: -1, camera: -0.8, satellites: -0.6, tooltip: -0.4, particles: -0.2 }`
  - `sceneContext.ts`: `type Hovered`, `class FocusFrame { key; previousKey; t; weight; begin(key); sync(t, weight) }`, `class FrameEvents { list; replace(events) }`, `interface SatelliteView`, `interface SceneContextValue { cache; layout; presence; events; focus; satellites; setHovered }`
  - `nodeList.ts`: `nodeIdsOf(entries): string[]`, `parseNodeId(id): NodeSpec` (`selectNodeIds` 는 사라진다)

R3F 사항 (코드가 이미 반영하고 있다):
- `useFrame` 우선순위는 전부 `FRAME_PRIORITY` 에서 가져온다. 양수는 R3F 의 자동 렌더를 끈다.
- R3F 는 드래그 끝에도 메시의 `onClick` 을 부른다 — `event.delta > 2` 면 무시한다. `Canvas` 의 `onPointerMissed` 는 R3F 가 2px 이하 이동일 때만 부른다.
- 카메라와 controls 는 `useFrame((state) => …)` 의 `state.camera`·`state.controls` 로 바꾼다. `useThree` 가 준 객체에 직접 대입하면 oxlint `react(immutability)` 경고가 난다. 같은 이유로 context 의 공유 상태는 메서드(`FocusFrame.begin/sync`, `FrameEvents.replace`)로만 바꾼다.

- [ ] **Step 1: gsap 설치**

Run: `npm install gsap@^3.15.0`
Expected: `found 0 vulnerabilities`, `package.json` dependencies 에 `"gsap": "^3.15.0"`.

- [ ] **Step 2: 실패하는 테스트 — `web/tests/focus.test.tsx`**

```tsx
import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { ProcessGroup, Snapshot } from '../src/protocol/schema';
import { FocusPanel } from '../src/scene/FocusPanel';
import { useFocusStore } from '../src/scene/focusStore';
import { useSnapshotStore } from '../src/state/snapshotStore';

function group(): ProcessGroup {
  return {
    key: 'whale.exe:8784',
    name: 'whale.exe',
    root_pid: 8784,
    cpu_pct: 1.5,
    mem_mb: 2755,
    proc_count: 3,
    thread_count: 752,
    started_at: 0,
    account: 'user',
    image_path: 'C:\\Program Files\\Whale\\whale.exe',
    children: [
      { pid: 22180, name: 'whale.exe', role: 'child', cpu_pct: null, mem_mb: 351, threads: 20 },
      { pid: 11008, name: 'whale.exe', role: 'child', cpu_pct: 0, mem_mb: 412, threads: 30 },
    ],
  };
}

function snapshot(groups: ProcessGroup[]): Snapshot {
  return {
    type: 'snapshot',
    v: 1,
    seq: 1,
    t: 0,
    system: { cpu_pct: 0, mem_used_mb: 0, mem_total_mb: 0, process_total: 0, thread_total: 0 },
    cores: [],
    groups,
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  };
}

describe('focusStore', () => {
  beforeEach(() => useFocusStore.getState().clear());

  it('focuses, moves and clears', () => {
    const store = useFocusStore.getState();
    store.focus('a.exe:1');
    expect(useFocusStore.getState().focusedKey).toBe('a.exe:1');
    store.focus('b.exe:2');
    expect(useFocusStore.getState().focusedKey).toBe('b.exe:2');
    store.clear();
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });

  it('toggles off when the same key is chosen again', () => {
    const store = useFocusStore.getState();
    store.toggle('a.exe:1');
    store.toggle('a.exe:1');
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });
});

describe('FocusPanel', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useSnapshotStore.getState().reset();
    useFocusStore.getState().clear();
    useSnapshotStore.getState().pushSnapshot(snapshot([group()]), 0);
  });

  afterEach(() => {
    vi.useRealTimers();
    useFocusStore.getState().clear();
  });

  // useInterpolated 는 100 ms 마다 한 번 값을 넣는다.
  function tick() {
    act(() => {
      vi.advanceTimersByTime(150);
    });
  }

  it('renders nothing without a focus', () => {
    const { container } = render(<FocusPanel />);
    tick();
    expect(container).toBeEmptyDOMElement();
  });

  it('shows the focused group and its children sorted by memory', () => {
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    tick();

    expect(screen.getByText('whale.exe', { selector: 'strong' })).toBeInTheDocument();
    expect(screen.getByText('2,755 MB')).toBeInTheDocument();
    expect(screen.getByText('1.5%')).toBeInTheDocument();
    const rows = screen.getAllByRole<HTMLTableRowElement>('row').slice(1);
    expect(rows.map((row) => row.cells[1].textContent)).toEqual(['11008', '22180']);
  });

  it('keeps an unknown child cpu as "-" and a measured 0 as "0.0"', () => {
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    tick();

    const rows = screen.getAllByRole<HTMLTableRowElement>('row').slice(1);
    expect(rows[0].cells[3].textContent).toBe('0.0');
    expect(rows[1].cells[3].textContent).toBe('-');
  });

  it('clears the focus from its close button', async () => {
    vi.useRealTimers();
    const user = userEvent.setup();
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    await screen.findByText('whale.exe', { selector: 'strong' });

    await user.click(screen.getByRole('button', { name: 'close focus' }));
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });

  it('renders nothing when the focused group is not in the snapshot', () => {
    useFocusStore.getState().focus('gone.exe:1');
    const { container } = render(<FocusPanel />);
    tick();
    expect(container).toBeEmptyDOMElement();
  });
});
```

- [ ] **Step 3: 테스트 교체 — `web/tests/nodeList.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import type { ProcessGroup } from '../src/protocol/schema';
import { nodeIdsOf, parseNodeId } from '../src/scene/nodeList';

function entry(key: string, account: ProcessGroup['account']) {
  return { key, value: { account } };
}

describe('nodeIdsOf', () => {
  it('returns one id per entry in order', () => {
    expect(nodeIdsOf([entry('a.exe:1', 'user'), entry('b.exe:2', 'system')])).toEqual([
      'user|a.exe:1',
      'system|b.exe:2',
    ]);
  });

  it('returns an empty list for no entries', () => {
    expect(nodeIdsOf([])).toEqual([]);
  });
});

describe('parseNodeId', () => {
  it('splits the account from the key', () => {
    expect(parseNodeId('system|svc.exe:44')).toEqual({ account: 'system', key: 'svc.exe:44' });
  });

  it('keeps a | inside the key', () => {
    // 파싱이 key 에 | 가 없다는 가정에 기대지 않는다.
    expect(parseNodeId('user|odd|name.exe:7')).toEqual({ account: 'user', key: 'odd|name.exe:7' });
  });

  it('round-trips every id nodeIdsOf produces', () => {
    const entries = [entry('a.exe:1', 'user'), entry('b b.exe:2', 'system')];
    expect(nodeIdsOf(entries).map(parseNodeId)).toEqual(
      entries.map((e) => ({ key: e.key, account: e.value.account })),
    );
  });
});
```

- [ ] **Step 4: 실패 확인 (RED)**

Run: `npx vitest run --project jsdom tests/focus.test.tsx tests/nodeList.test.ts`
Expected: `focus.test.tsx` 는 `../src/scene/FocusPanel` 을 찾지 못해 실패, `nodeList.test.ts` 는 `nodeIdsOf` 가 없어 실패.

- [ ] **Step 5: `web/src/scene/framePriority.ts`**

```ts
// useFrame 우선순위. 작은 값이 먼저 돈다. 양수는 R3F 의 자동 렌더를 끄므로
// 전부 음수이고, 노드·위성 메시와 drei Html 은 기본값 0 에서 돈다.
//
//   scene       보간 → 존재 추적 → lifecycle → 레이아웃 → 버스트 생성
//   camera      Focus 전환 진행도로 카메라 자세 (노드 위치가 정해진 뒤)
//   satellites  위성 위치 (Focus 진행도가 정해진 뒤)
//   tooltip     툴팁 anchor (위성 위치가 정해진 뒤, Html 이 투영하기 전)
//   particles   입자 버퍼 (모든 기준점이 정해진 뒤)
export const FRAME_PRIORITY = {
  scene: -1,
  camera: -0.8,
  satellites: -0.6,
  tooltip: -0.4,
  particles: -0.2,
} as const;
```

- [ ] **Step 6: `web/src/scene/focusStore.ts`**

```ts
import { create } from 'zustand';

// M5 스펙 9.1. 어느 그룹에 초점이 있는지. 장면과 Canvas 밖 패널이 함께 읽는다.
// 프레임 값(전환 진행도, 카메라 자세)은 여기 두지 않는다 — 그것은 장면의
// 가변 객체(FocusFrame)에 있다.
interface FocusState {
  focusedKey: string | null;
  focus: (key: string) => void;
  // 같은 천체를 다시 누르면 초점을 푼다.
  toggle: (key: string) => void;
  clear: () => void;
}

export const useFocusStore = create<FocusState>((set, get) => ({
  focusedKey: null,
  focus: (key) => set({ focusedKey: key }),
  toggle: (key) => set({ focusedKey: get().focusedKey === key ? null : key }),
  clear: () => set({ focusedKey: null }),
}));
```

- [ ] **Step 7: `web/src/scene/sceneContext.ts` 전체 교체**

```ts
import { createContext, useContext } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import type { InterpolatedGroup } from '../state/interpolator';
import type { FrameCache } from '../visual/frameCache';
import type { LayoutSim, Vec3 } from '../visual/layout';
import type { LifecycleEvent } from '../visual/lifecycleEvents';
import type { PresenceEntry, PresenceTracker } from '../visual/presence';

export type Hovered =
  | { kind: 'group'; key: string }
  | { kind: 'satellite'; pid: number }
  | null;

// CameraRig 가 매 프레임 채운다. 노드는 이것으로 어두워지고, 위성은 펼쳐진다.
// 장면 곳곳이 같은 인스턴스를 읽으므로 값은 메서드로만 바꾼다.
export class FocusFrame {
  // 지금 초점 그룹. 없으면 null.
  key: string | null = null;
  // 직전 초점 그룹. 초점을 푸는 동안 위성이 이 그룹으로 접혀 들어간다.
  previousKey: string | null = null;
  // 카메라 전환 진행도 0~1.
  t = 1;
  // 초점 강도 0~1. 초점이 잡힐수록 1, 풀릴수록 0.
  weight = 0;

  begin(key: string | null): void {
    this.previousKey = this.key;
    this.key = key;
  }

  sync(t: number, weight: number): void {
    this.t = t;
    this.weight = weight;
  }
}

// 이번 프레임에 소비한 lifecycle 이벤트. 대부분의 프레임에서 비어 있다.
export class FrameEvents {
  list: readonly LifecycleEvent[] = [];

  replace(events: readonly LifecycleEvent[]): void {
    this.list = events;
  }
}

// Satellites 가 매 프레임 채운다. 툴팁과 입자가 위성 위치를 읽는다.
export interface SatelliteView {
  position: Vec3;
  radius: number;
  child: ChildProcess;
  entry: PresenceEntry<ChildProcess>;
  account: ProcessGroup['account'];
}

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: LayoutSim;
  presence: PresenceTracker<InterpolatedGroup>;
  events: FrameEvents;
  focus: FocusFrame;
  satellites: Map<number, SatelliteView>;
  setHovered: (update: (current: Hovered) => Hovered) => void;
}

export const SceneContext = createContext<SceneContextValue | null>(null);

export function useSceneContext(): SceneContextValue {
  const value = useContext(SceneContext);
  if (value === null) {
    throw new Error('useSceneContext must be used inside <SceneRoot>');
  }
  return value;
}
```

- [ ] **Step 8: `web/src/scene/nodeList.ts` 전체 교체**

```ts
import type { ProcessGroup } from '../protocol/schema';

// 장면이 React 로 다시 그려야 하는 때는 그려야 할 key 집합이 바뀔 때뿐이다.
// 존재 추적기의 항목을 문자열 id 배열로 만들어 두고, 이어 붙인 서명이 바뀔
// 때만 React 상태를 갱신한다. account 는 색을 정하므로 함께 싣는다.
// account 에는 '|' 가 없으므로 첫 '|' 에서 자르면 key 에 '|' 가 있어도 안전하다.
export interface NodeSpec {
  key: string;
  account: ProcessGroup['account'];
}

export function nodeIdsOf(
  entries: readonly { key: string; value: Pick<ProcessGroup, 'account'> }[],
): string[] {
  return entries.map((entry) => `${entry.value.account}|${entry.key}`);
}

export function parseNodeId(id: string): NodeSpec {
  const bar = id.indexOf('|');
  return {
    account: id.slice(0, bar) as ProcessGroup['account'],
    key: id.slice(bar + 1),
  };
}
```

- [ ] **Step 9: `web/src/scene/SceneRoot.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef, useState } from 'react';
import { Color } from 'three';

import { sample, type InterpolatedGroup } from '../state/interpolator';
import { useSnapshotStore } from '../state/snapshotStore';
import {
  CHILD_BORN,
  CHILD_DIED,
  GROUP_COLLAPSE,
  GROUP_FORM,
  BurstPool,
  type BurstAnchor,
  type BurstSpec,
} from '../visual/bursts';
import { createFrameCache, layoutNodesFrom, updateFrameCache } from '../visual/frameCache';
import { hash01 } from '../visual/hash';
import { LayoutSim } from '../visual/layout';
import { LifecycleConsumer, type LifecycleEvent } from '../visual/lifecycleEvents';
import { colorFor, radiusFor } from '../visual/mapping';
import { PresenceTracker } from '../visual/presence';
import { CameraRig } from './CameraRig';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { nodeIdsOf, parseNodeId } from './nodeList';
import { Particles } from './Particles';
import { ProcessNode } from './ProcessNode';
import { Satellites } from './Satellites';
import {
  FocusFrame,
  FrameEvents,
  SceneContext,
  type Hovered,
  type SceneContextValue,
} from './sceneContext';
import { Tooltip } from './Tooltip';

// 자식 버스트는 부모 천체 안쪽에서 일어나 보이도록 부모 반지름보다 작게 잡는다.
const CHILD_BURST_RADIUS = 0.5;

function rgbOf(group: Pick<InterpolatedGroup, 'account' | 'key'>): [number, number, number] {
  const hsl = colorFor(group.account, group.key);
  const color = new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  return [color.r, color.g, color.b];
}

// 이벤트 하나를 버스트 명세로. 기준 그룹을 모르면(이미 추적기에서도 사라짐) null.
function burstFor(
  event: LifecycleEvent,
  group: InterpolatedGroup | undefined,
  focusedKey: string | null,
  seq: number,
): BurstSpec | null {
  if (group === undefined) {
    return null;
  }
  const radius = radiusFor(group.mem_mb);
  const color = rgbOf(group);
  const seed = Math.floor(hash01(`${event.kind}:${event.pid}`, seq) * 0x100000000);
  const groupAnchor: BurstAnchor = { kind: 'group', key: event.key };
  // Focus 중인 그룹의 자식이면 그 위성 자리에서 재생한다 (M5 스펙 D30).
  const childAnchor: BurstAnchor =
    focusedKey === event.key ? { kind: 'satellite', key: String(event.pid) } : groupAnchor;

  switch (event.kind) {
    case 'group-born':
      return { ...GROUP_FORM, anchor: groupAnchor, radius, color, seed };
    case 'group-died':
      return { ...GROUP_COLLAPSE, anchor: groupAnchor, radius, color, seed };
    case 'child-born':
      return { ...CHILD_BORN, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
    case 'child-died':
      return { ...CHILD_DIED, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
  }
}

// 스펙 4절(M4)과 M5 스펙 5~7절. 프레임당 한 번 보간하고, 존재 추적기와
// lifecycle 소비기를 갱신하고, 레이아웃을 한 스텝 진행하고, 이벤트를 버스트로
// 바꾼다. 노드 목록은 스토어가 아니라 존재 추적기의 항목이다 — 떠나는 천체도
// 연출이 끝날 때까지 그려야 한다.
export function SceneRoot() {
  const [nodeIds, setNodeIds] = useState<string[]>([]);
  const [hovered, setHovered] = useState<Hovered>(null);
  const signature = useRef('');

  const context = useMemo<SceneContextValue>(
    () => ({
      cache: createFrameCache(),
      layout: new LayoutSim(),
      presence: new PresenceTracker<InterpolatedGroup>(),
      events: new FrameEvents(),
      focus: new FocusFrame(),
      satellites: new Map(),
      setHovered,
    }),
    [],
  );
  const consumer = useMemo(() => new LifecycleConsumer(), []);
  const pool = useMemo(() => new BurstPool(), []);

  useFrame(() => {
    // 시계는 performance.now() 다 — arrivedAt 이 같은 시계로 찍힌다.
    const now = performance.now();
    const state = useSnapshotStore.getState();
    const frame = sample(
      {
        previous: state.previous,
        current: state.current,
        arrivedAt: state.arrivedAt,
        intervalMs: state.intervalMs,
      },
      now,
    );
    const { cache, layout, presence } = context;
    updateFrameCache(cache, frame, now);
    const nowSec = now / 1000;

    if (frame === null) {
      // 세션이 바뀌었거나 버전 불일치로 스토어가 비었다. 다음 스냅샷은 다시
      // "이미 있던 것" 으로 시작한다.
      presence.reset();
      consumer.reset();
      pool.clear();
      context.events.replace([]);
    } else {
      const events = consumer.consume(frame);
      context.events.replace(events);
      const born = new Set(events.filter((e) => e.kind === 'group-born').map((e) => e.key));
      const died = new Set(events.filter((e) => e.kind === 'group-died').map((e) => e.key));
      presence.update(
        frame.groups.map((group) => ({ key: group.key, value: group })),
        born,
        died,
        nowSec,
      );

      const focusedKey = useFocusStore.getState().focusedKey;
      for (const event of events) {
        const spec = burstFor(event, presence.get(event.key)?.value, focusedKey, frame.seq);
        if (spec !== null) {
          pool.add(spec, nowSec);
        }
      }
    }

    const entries = presence.entries();
    layout.step(layoutNodesFrom(entries.map((entry) => entry.value)), cache.dtSec);

    // 초점 그룹이 떠나기 시작하면 초점을 푼다 (M5 스펙 9.1).
    const focus = useFocusStore.getState();
    if (focus.focusedKey !== null) {
      const phase = presence.get(focus.focusedKey)?.phase;
      if (phase === undefined || phase === 'fading-out' || phase === 'collapsing') {
        focus.clear();
      }
    }

    const ids = nodeIdsOf(entries);
    const next = ids.join('\n');
    if (next !== signature.current) {
      signature.current = next;
      setNodeIds(ids);
    }
  }, FRAME_PRIORITY.scene);

  const keys = nodeIds.map((id) => parseNodeId(id).key);
  // 호버 중이던 천체가 사라지면 R3F 가 onPointerOut 없이 오브젝트를 지운다.
  // 지금도 있는 그룹일 때만 그룹 툴팁을 그린다. 위성 툴팁은 위성이 없으면
  // 스스로 숨는다.
  const liveHovered =
    hovered !== null && (hovered.kind === 'satellite' || keys.includes(hovered.key))
      ? hovered
      : null;

  return (
    <SceneContext.Provider value={context}>
      <CameraRig />
      {nodeIds.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      <Satellites />
      <Particles pool={pool} />
      {liveHovered !== null && <Tooltip target={liveHovered} />}
    </SceneContext.Provider>
  );
}
```

- [ ] **Step 10: `web/src/scene/ProcessNode.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Group,
  type Mesh,
  type MeshBasicMaterial,
  type MeshStandardMaterial,
} from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { floatingPosition } from '../visual/layout';
import {
  HALO_SCALE,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../visual/mapping';
import { presenceVisual } from '../visual/presence';
import { useFocusStore } from './focusStore';
import { useSceneContext, type FocusFrame } from './sceneContext';

const SALT_PHASE = 2;
// 초점이 잡히면 다른 천체의 발광·불투명도가 이만큼까지 줄어든다 (M5 스펙 9.3).
const DIM_DEPTH = 0.75;
// 드래그 끝에 버튼을 뗀 것은 클릭이 아니다 (px).
const CLICK_SLOP = 2;

// 초점과 무관한 천체일수록 1 보다 작다.
function dimFor(focus: FocusFrame, key: string): number {
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === key) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}

interface Props {
  nodeKey: string;
  account: ProcessGroup['account'];
}

// 그룹 하나. 프레임 값은 React 상태를 거치지 않는다 — useFrame 에서 존재
// 추적기의 항목을 key 로 읽어 ref 를 직접 바꾼다. 떠나는 중인 천체는 고정된
// 마지막 값으로 그린다.
export function ProcessNode({ nodeKey, account }: Props) {
  const { cache, layout, presence, focus, setHovered } = useSceneContext();

  const root = useRef<Group>(null);
  const body = useRef<Mesh>(null);
  const bodyMaterial = useRef<MeshStandardMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 40 개가 동시에 숨 쉬지 않도록 초기 위상을 key 로 흩는다.
  const phase = useRef(hash01(nodeKey, SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, nodeKey);
    return new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  }, [account, nodeKey]);

  useFrame(() => {
    if (
      root.current === null ||
      body.current === null ||
      bodyMaterial.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const entry = presence.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    if (entry === undefined || position === undefined) {
      root.current.visible = false;
      return;
    }
    root.current.visible = true;

    const group = entry.value;
    const visual = presenceVisual(entry);
    const dim = dimFor(focus, nodeKey);
    const activity = activityFor(group.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);
    const scale =
      radiusFor(group.mem_mb) * visual.scale * (1 + pulse.amplitude * Math.sin(phase.current));

    root.current.position.set(position.x, position.y, position.z);
    body.current.scale.setScalar(scale);
    halo.current.scale.setScalar(scale * HALO_SCALE);

    const glow = glowFor(activity);
    const opacity = visual.opacity * dim;
    const material = bodyMaterial.current;
    material.emissiveIntensity = glow.emissiveIntensity * dim;
    material.opacity = opacity;
    // 불투명할 때는 transparent 를 끈다. 정렬 비용과 깊이 문제를 피한다.
    const transparent = opacity < 1;
    if (material.transparent !== transparent) {
      material.transparent = transparent;
      material.depthWrite = !transparent;
      material.needsUpdate = true;
    }
    haloMaterial.current.opacity = glow.haloOpacity * opacity;
  });

  return (
    <group ref={root} visible={false}>
      <mesh
        ref={body}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'group', key: nodeKey }));
        }}
        onPointerOut={() =>
          setHovered((current) =>
            current?.kind === 'group' && current.key === nodeKey ? null : current,
          )
        }
        onClick={(event) => {
          // R3F 는 드래그 끝에도 onClick 을 부른다. 궤도 조작을 초점 전환으로
          // 읽지 않도록 움직인 거리를 본다.
          if (event.delta > CLICK_SLOP) {
            return;
          }
          event.stopPropagation();
          const phase = presence.get(nodeKey)?.phase;
          if (phase === 'fading-out' || phase === 'collapsing') {
            return;
          }
          useFocusStore.getState().toggle(nodeKey);
        }}
      >
        <sphereGeometry args={[1, 32, 32]} />
        <meshStandardMaterial
          ref={bodyMaterial}
          color={color}
          emissive={color}
          roughness={0.55}
          metalness={0.1}
        />
      </mesh>
      {/* 헤일로는 호버 대상이 아니다. 광선 검사에서 빼야 뒤쪽 천체를 가리지 않는다. */}
      <mesh ref={halo} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          color={color}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 11: `web/src/scene/CameraRig.tsx`**

```tsx
import { useFrame, useThree } from '@react-three/fiber';
import gsap from 'gsap';
import { useEffect, useRef } from 'react';
import type { Vector3 } from 'three';

import {
  OVERVIEW_POSE,
  blendPose,
  focusDirection,
  focusPose,
  type Pose,
} from '../visual/camera';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// M5 스펙 9.2. GSAP 은 전환 진행도(t)와 초점 강도(weight) 두 수만 움직인다.
// 카메라 자세는 매 프레임 그 진행도로 계산한다 — 그래서 떠다니는 천체를 따라간다.
export const FOCUS_TRANSITION_SEC = 1.0;
const FOCUS_EASE = 'power2.inOut';

// drei OrbitControls(makeDefault)가 등록하는 controls 에서 쓰는 부분만.
interface Controls {
  target: Vector3;
}

function toVec(v: Vector3): Vec3 {
  return { x: v.x, y: v.y, z: v.z };
}

export function CameraRig() {
  const context = useSceneContext();
  const camera = useThree((state) => state.camera);
  const controls = useThree((state) => state.controls) as unknown as Controls | null;
  const focusedKey = useFocusStore((state) => state.focusedKey);

  // GSAP 이 직접 바꾸는 평범한 객체.
  const tween = useRef({ t: 1, weight: 0 });
  const tweening = useRef(false);
  const from = useRef<Pose>(OVERVIEW_POSE);
  const direction = useRef<Vec3>({ x: 0, y: 0, z: 1 });
  const lastNode = useRef<Vec3 | null>(null);

  useEffect(() => {
    const { focus, cache, layout } = context;
    // 처음 마운트될 때(초점 없음 → 초점 없음)는 전환할 것이 없다. 사용자의
    // 궤도 조작과 자동 회전을 1초 동안 빼앗지 않는다.
    if (focusedKey === null && focus.key === null) {
      return;
    }
    focus.begin(focusedKey);

    from.current = {
      position: toVec(camera.position),
      target: controls === null ? { ...OVERVIEW_POSE.target } : toVec(controls.target),
    };
    if (focusedKey !== null && cache.timeSec !== null) {
      const node = floatingPosition(layout, focusedKey, cache.timeSec);
      if (node !== undefined) {
        direction.current = focusDirection(from.current.position, node);
      }
    }
    lastNode.current = null;

    tween.current.t = 0;
    tweening.current = true;
    const animation = gsap.to(tween.current, {
      t: 1,
      weight: focusedKey === null ? 0 : 1,
      duration: FOCUS_TRANSITION_SEC,
      ease: FOCUS_EASE,
      onComplete: () => {
        tweening.current = false;
      },
    });
    return () => {
      animation.kill();
    };
  }, [focusedKey, camera, controls, context]);

  // 장면이 사라지면(대시보드로 전환) 초점도 푼다.
  useEffect(() => () => useFocusStore.getState().clear(), []);

  // 카메라와 controls 는 useFrame 이 넘겨주는 state 에서 꺼내 쓴다 — R3F 가
  // 렌더 루프 안에서 바꾸라고 내주는 객체다.
  useFrame((state) => {
    const camera = state.camera;
    const controls = state.controls as unknown as Controls | null;
    const { focus, cache, layout, presence } = context;
    focus.sync(tween.current.t, tween.current.weight);
    if (controls === null || cache.timeSec === null) {
      return;
    }

    const key = focus.key;
    const node = key === null ? undefined : floatingPosition(layout, key, cache.timeSec);
    const group = key === null ? undefined : presence.get(key)?.value;

    if (tweening.current) {
      const to =
        node !== undefined && group !== undefined
          ? focusPose(node, radiusFor(group.mem_mb), direction.current)
          : OVERVIEW_POSE;
      const pose = blendPose(from.current, to, focus.t);
      camera.position.set(pose.position.x, pose.position.y, pose.position.z);
      controls.target.set(pose.target.x, pose.target.y, pose.target.z);
      lastNode.current = node ?? null;
      return;
    }

    // 전환이 끝난 뒤에는 사용자가 그 천체 주위를 돌려 볼 수 있어야 한다.
    // 카메라를 고정하지 않고, 천체가 떠다닌 만큼만 카메라와 주시점을 옮긴다.
    if (node !== undefined) {
      if (lastNode.current !== null) {
        camera.position.x += node.x - lastNode.current.x;
        camera.position.y += node.y - lastNode.current.y;
        camera.position.z += node.z - lastNode.current.z;
      }
      controls.target.set(node.x, node.y, node.z);
      lastNode.current = node;
    }
  }, FRAME_PRIORITY.camera);

  return null;
}
```

- [ ] **Step 12: `web/src/scene/Satellites.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef, useState } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import { floatingPosition } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { SATELLITE_SCALE, orbitFor, satellitePosition } from '../visual/orbits';
import { PresenceTracker } from '../visual/presence';
import { FRAME_PRIORITY } from './framePriority';
import { SatelliteNode } from './SatelliteNode';
import { useSceneContext } from './sceneContext';

interface Shown {
  account: ProcessGroup['account'];
  pids: number[];
}

const NOTHING: Shown = { account: 'user', pids: [] };

// M5 스펙 9.4. 초점 그룹의 자식을 위성으로 돌린다. 위성도 존재 추적기를
// 거친다 — 초점 중 자식이 생기면 형성되고, 끝나면 붕괴한다.
export function Satellites() {
  const context = useSceneContext();
  const tracker = useMemo(() => new PresenceTracker<ChildProcess>(), []);
  const owner = useRef<string | null>(null);
  const signature = useRef('');
  const [shown, setShown] = useState<Shown>(NOTHING);

  useFrame(() => {
    const { focus, presence, layout, cache, satellites, events } = context;
    // 초점을 푸는 동안에는 직전 초점 그룹으로 위성이 접혀 들어간다.
    const nextOwner = focus.key ?? (focus.weight > 0 ? focus.previousKey : null);
    if (nextOwner !== owner.current) {
      owner.current = nextOwner;
      tracker.reset();
    }

    const entry = nextOwner === null ? undefined : presence.get(nextOwner);
    const center =
      nextOwner === null || cache.timeSec === null
        ? undefined
        : floatingPosition(layout, nextOwner, cache.timeSec);
    satellites.clear();
    if (entry === undefined || center === undefined || cache.timeSec === null) {
      if (signature.current !== '') {
        signature.current = '';
        setShown(NOTHING);
      }
      return;
    }

    const born = new Set<string>();
    const died = new Set<string>();
    for (const event of events.list) {
      if (event.key !== nextOwner) {
        continue;
      }
      if (event.kind === 'child-born') {
        born.add(String(event.pid));
      } else if (event.kind === 'child-died') {
        died.add(String(event.pid));
      }
    }
    tracker.update(
      entry.value.children.map((child) => ({ key: String(child.pid), value: child })),
      born,
      died,
      cache.timeSec,
    );

    // 초점이 잡히면서 궤도가 펼쳐지고, 풀리면서 접힌다.
    const spread = focus.key === nextOwner ? focus.t : 1 - focus.t;
    const parentRadius = radiusFor(entry.value.mem_mb);
    for (const satellite of tracker.entries()) {
      const pid = satellite.value.pid;
      satellites.set(pid, {
        position: satellitePosition(orbitFor(parentRadius, pid), center, cache.timeSec, spread),
        radius: radiusFor(satellite.value.mem_mb) * SATELLITE_SCALE,
        child: satellite.value,
        entry: satellite,
        account: entry.value.account,
      });
    }

    const pids = tracker.entries().map((satellite) => satellite.value.pid);
    const next = `${entry.value.account}|${pids.join(',')}`;
    if (next !== signature.current) {
      signature.current = next;
      setShown({ account: entry.value.account, pids });
    }
  }, FRAME_PRIORITY.satellites);

  return (
    <>
      {shown.pids.map((pid) => (
        <SatelliteNode key={pid} pid={pid} account={shown.account} />
      ))}
    </>
  );
}
```

- [ ] **Step 13: `web/src/scene/SatelliteNode.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { Color, type Mesh, type MeshStandardMaterial } from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { activityFor, advancePhase, colorFor, glowFor, pulseFor } from '../visual/mapping';
import { presenceVisual } from '../visual/presence';
import { useSceneContext } from './sceneContext';

const SALT_PHASE = 2;
// 위성은 부모 색 계열에서 조금 더 밝게 그린다.
const SATELLITE_LIGHTNESS = 0.68;

interface Props {
  pid: number;
  account: ProcessGroup['account'];
}

// 위성 하나. 위치와 값은 Satellites 가 프레임마다 채운 SatelliteView 에서 읽는다.
export function SatelliteNode({ pid, account }: Props) {
  const { cache, satellites, setHovered } = useSceneContext();
  const mesh = useRef<Mesh>(null);
  const material = useRef<MeshStandardMaterial>(null);
  const phase = useRef(hash01(String(pid), SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, String(pid));
    return new Color().setHSL(hsl.h / 360, hsl.s, SATELLITE_LIGHTNESS);
  }, [account, pid]);

  useFrame(() => {
    if (mesh.current === null || material.current === null) {
      return;
    }
    const view = satellites.get(pid);
    if (view === undefined) {
      mesh.current.visible = false;
      return;
    }
    mesh.current.visible = true;

    const visual = presenceVisual(view.entry);
    const activity = activityFor(view.child.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);

    mesh.current.position.set(view.position.x, view.position.y, view.position.z);
    mesh.current.scale.setScalar(
      view.radius * visual.scale * (1 + pulse.amplitude * Math.sin(phase.current)),
    );
    material.current.emissiveIntensity = glowFor(activity).emissiveIntensity;
    material.current.opacity = visual.opacity;
  });

  return (
    <mesh
      ref={mesh}
      visible={false}
      onPointerOver={(event) => {
        event.stopPropagation();
        setHovered(() => ({ kind: 'satellite', pid }));
      }}
      onPointerOut={() =>
        setHovered((current) =>
          current?.kind === 'satellite' && current.pid === pid ? null : current,
        )
      }
    >
      <sphereGeometry args={[1, 16, 16]} />
      <meshStandardMaterial
        ref={material}
        color={color}
        emissive={color}
        roughness={0.5}
        transparent
      />
    </mesh>
  );
}
```

- [ ] **Step 14: `web/src/scene/Particles.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry, type Points } from 'three';

import { POOL_CAPACITY, particleAt, type Burst, type BurstPool } from '../visual/bursts';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext, type SceneContextValue } from './sceneContext';

const PARTICLE_SIZE = 0.35;

// 버스트의 기준점. 기준 천체가 이미 사라졌으면 마지막으로 본 자리.
function centerOf(burst: Burst, context: SceneContextValue): Vec3 | null {
  const { cache, layout, satellites } = context;
  let center: Vec3 | undefined;
  if (burst.anchor.kind === 'group') {
    center =
      cache.timeSec === null ? undefined : floatingPosition(layout, burst.anchor.key, cache.timeSec);
  } else {
    center = satellites.get(Number(burst.anchor.key))?.position;
  }
  if (center !== undefined) {
    burst.lastCenter = center;
  }
  return burst.lastCenter;
}

interface Props {
  pool: BurstPool;
}

// M5 스펙 7절. 모든 버스트가 Points 하나를 나눠 쓴다. 입자 수만큼 버퍼를
// 미리 잡아 두고 매 프레임 살아 있는 입자만 앞에서부터 채운다.
export function Particles({ pool }: Props) {
  const context = useSceneContext();
  const points = useRef<Points>(null);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(new Float32Array(POOL_CAPACITY * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(POOL_CAPACITY * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  useFrame(() => {
    const { cache } = context;
    if (cache.timeSec === null) {
      return;
    }
    const positions = geometry.getAttribute('position') as BufferAttribute;
    const colors = geometry.getAttribute('color') as BufferAttribute;
    let n = 0;
    for (const burst of pool.active(cache.timeSec)) {
      const center = centerOf(burst, context);
      if (center === null) {
        continue;
      }
      for (let i = 0; i < burst.count && n < POOL_CAPACITY; i += 1) {
        const particle = particleAt(burst, i, cache.timeSec, center);
        if (particle === null) {
          continue;
        }
        positions.setXYZ(n, particle.x, particle.y, particle.z);
        // 가산 블렌딩이므로 알파 대신 색을 어둡게 해 사라지게 한다.
        colors.setXYZ(
          n,
          burst.color[0] * particle.alpha,
          burst.color[1] * particle.alpha,
          burst.color[2] * particle.alpha,
        );
        n += 1;
      }
    }
    geometry.setDrawRange(0, n);
    positions.needsUpdate = true;
    colors.needsUpdate = true;
  }, FRAME_PRIORITY.particles);

  return (
    // 입자는 호버·클릭 대상이 아니다. 경계 구가 바뀌지 않으므로 절두체 선별도 끈다.
    <points ref={points} geometry={geometry} frustumCulled={false} raycast={() => null}>
      <pointsMaterial
        size={PARTICLE_SIZE}
        sizeAttenuation
        vertexColors
        transparent
        blending={AdditiveBlending}
        depthWrite={false}
      />
    </points>
  );
}
```

- [ ] **Step 15: `web/src/scene/Tooltip.tsx` 전체 교체**

```tsx
import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import type { Group } from 'three';

import { formatMb, formatPct } from '../dashboard/format';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext, type Hovered, type SceneContextValue } from './sceneContext';

interface Label {
  position: Vec3;
  radius: number;
  title: string;
  detail: string;
}

// 숫자는 대시보드와 같은 formatMb·formatPct 로 쓴다 — 두 화면이 일치해야 한다.
function labelFor(target: NonNullable<Hovered>, context: SceneContextValue): Label | null {
  const { cache, layout, presence, satellites } = context;
  if (target.kind === 'satellite') {
    const view = satellites.get(target.pid);
    if (view === undefined) {
      return null;
    }
    return {
      position: view.position,
      radius: view.radius,
      title: `${view.child.name} · pid ${view.child.pid}`,
      detail: `${formatMb(view.child.mem_mb)} MB · CPU ${formatPct(view.child.cpu_pct)}%`,
    };
  }
  const entry = presence.get(target.key);
  const position =
    cache.timeSec === null ? undefined : floatingPosition(layout, target.key, cache.timeSec);
  // 떠나는 중인 천체의 값은 고정된 옛 값이다. 보여 주지 않는다.
  if (
    entry === undefined ||
    position === undefined ||
    entry.phase === 'fading-out' ||
    entry.phase === 'collapsing'
  ) {
    return null;
  }
  const group = entry.value;
  return {
    position,
    radius: radiusFor(group.mem_mb),
    title: group.name,
    detail: `${formatMb(group.mem_mb)} MB · CPU ${formatPct(group.cpu_pct)}%`,
  };
}

interface Props {
  target: NonNullable<Hovered>;
}

// 호버한 천체나 위성 위에 이름·메모리·CPU 를 띄운다. 값은 보간된 프레임
// 값이므로 React 상태를 거치지 않고 DOM 을 직접 바꾼다.
// anchor 를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저
// 돌아야 한 프레임 지연이나 원점 깜빡임이 없다 (framePriority 참조).
export function Tooltip({ target }: Props) {
  const context = useSceneContext();
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);
  const name = useRef<HTMLDivElement>(null);
  const detail = useRef<HTMLDivElement>(null);

  useFrame(() => {
    if (
      anchor.current === null ||
      box.current === null ||
      name.current === null ||
      detail.current === null
    ) {
      return;
    }
    const label = labelFor(target, context);
    // Html 은 DOM 이라 group.visible 로는 숨겨지지 않는다. DOM 쪽을 직접 숨긴다.
    if (label === null) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(
      label.position.x,
      label.position.y + label.radius * 1.2,
      label.position.z,
    );
    name.current.textContent = label.title;
    detail.current.textContent = label.detail;
  }, FRAME_PRIORITY.tooltip);

  return (
    <group ref={anchor}>
      <Html center style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-tooltip" style={{ display: 'none' }}>
          <div ref={name} className="universe-tooltip-name" />
          <div ref={detail} />
        </div>
      </Html>
    </group>
  );
}
```

- [ ] **Step 16: `web/src/scene/FocusPanel.tsx`**

```tsx
import { formatMb, formatPct } from '../dashboard/format';
import { useInterpolated } from '../state/useInterpolated';
import { useFocusStore } from './focusStore';

// M5 스펙 9.5. Canvas 밖 DOM 패널. 초점이 없으면 아무것도 마운트하지 않는다 —
// 10 Hz 타이머는 초점이 있는 동안에만 돈다.
export function FocusPanel() {
  const focusedKey = useFocusStore((state) => state.focusedKey);
  if (focusedKey === null) {
    return null;
  }
  return <FocusPanelBody groupKey={focusedKey} />;
}

function FocusPanelBody({ groupKey }: { groupKey: string }) {
  const frame = useInterpolated();
  const clear = useFocusStore((state) => state.clear);
  const group = frame?.groups.find((g) => g.key === groupKey);
  if (group === undefined) {
    return null;
  }
  const children = [...group.children].sort((a, b) => b.mem_mb - a.mem_mb);

  return (
    <aside className="focus-panel" aria-label="focused group">
      <header>
        <strong>{group.name}</strong>
        <button type="button" onClick={clear} aria-label="close focus">
          ×
        </button>
      </header>
      <dl>
        <dt>pid</dt>
        <dd>{group.root_pid}</dd>
        <dt>account</dt>
        <dd>{group.account}</dd>
        <dt>memory</dt>
        <dd>{formatMb(group.mem_mb)} MB</dd>
        <dt>cpu</dt>
        <dd>{formatPct(group.cpu_pct)}%</dd>
        <dt>processes</dt>
        <dd>{group.proc_count}</dd>
        <dt>threads</dt>
        <dd>{group.thread_count}</dd>
      </dl>
      <p className="focus-panel-path">{group.image_path || '-'}</p>
      {children.length === 0 ? (
        <p className="focus-panel-empty">no child processes</p>
      ) : (
        <table>
          <thead>
            <tr>
              <th>child</th>
              <th>pid</th>
              <th>MB</th>
              <th>CPU %</th>
            </tr>
          </thead>
          <tbody>
            {children.map((child) => (
              <tr key={child.pid}>
                <td>{child.name}</td>
                <td>{child.pid}</td>
                <td>{formatMb(child.mem_mb)}</td>
                <td>{formatPct(child.cpu_pct)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </aside>
  );
}
```

- [ ] **Step 17: `web/src/scene/Universe.tsx` 전체 교체**

```tsx
import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useEffect, useState } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
import { OVERVIEW_POSE } from '../visual/camera';
import { FocusPanel } from './FocusPanel';
import { useFocusStore } from './focusStore';
import { SceneRoot } from './SceneRoot';

function probeWebgl(): boolean {
  try {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl2') ?? canvas.getContext('webgl');
    if (gl === null) {
      return false;
    }
    // 탐지용 컨텍스트를 그대로 두면 Universe 가 마운트될 때마다 하나씩
    // 새어 나간다. 판정이 끝나면 바로 반납한다.
    (gl as WebGLRenderingContext).getExtension('WEBGL_lose_context')?.loseContext();
    return true;
  } catch {
    return false;
  }
}

// 모듈 로드 시 한 번만 판정한다. Universe 가 여러 번 마운트되어도(뷰 토글)
// 컨텍스트를 반복해서 만들지 않는다.
const webglAvailable = probeWebgl();

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);
  const focused = useFocusStore((state) => state.focusedKey !== null);

  // Esc 로 초점을 푼다 (M5 스펙 9.1).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        useFocusStore.getState().clear();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (!webglAvailable) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas
        dpr={[1, 2]}
        camera={{
          fov: 50,
          position: [OVERVIEW_POSE.position.x, OVERVIEW_POSE.position.y, OVERVIEW_POSE.position.z],
        }}
        // 빈 곳을 클릭하면 초점을 푼다. R3F 는 2px 넘게 끌면 이것을 부르지 않으므로
        // 궤도 조작은 초점을 풀지 않는다.
        onPointerMissed={() => useFocusStore.getState().clear()}
      >
        <color attach="background" args={['#03040a']} />
        <ambientLight intensity={0.2} />
        {/* 중심 점광원은 가운데의 큰 천체 안에 묻힌다. 방향광을 쓴다. */}
        <directionalLight position={[20, 30, 25]} intensity={1.1} />
        <Stars radius={120} depth={60} count={4000} factor={4} fade />
        <SceneRoot />
        <OrbitControls
          makeDefault
          enableDamping
          // 초점 중에는 자동 회전을 끈다. 카메라가 천체를 따라가는 중이다.
          autoRotate={autoRotate && !focused}
          autoRotateSpeed={0.3}
          onStart={() => setAutoRotate(false)}
        />
      </Canvas>
      <FocusPanel />
      {!hasSnapshot && <p className="universe-message">waiting for the first snapshot…</p>}
    </div>
  );
}
```

- [ ] **Step 18: `web/src/shell/shell.css` 전체 교체**

```css
.universe {
  position: fixed;
  inset: 0;
}

.universe-message {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0;
  color: #8f9aa8;
  pointer-events: none;
}

.universe-message a {
  pointer-events: auto;
  color: #6ee7a8;
  margin-left: 0.5ch;
}

.universe-tooltip {
  padding: 6px 9px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
  white-space: nowrap;
}

.universe-tooltip-name {
  color: #e8edf3;
}

.shell-badge {
  position: fixed;
  top: 12px;
  left: 12px;
  right: 160px;
  /* 배지에는 상호작용 요소가 없다. 캔버스 위 포인터 입력을 가로막지 않는다. */
  pointer-events: none;
}

.shell-toggle {
  position: fixed;
  top: 12px;
  right: 12px;
  padding: 6px 12px;
  font: inherit;
  color: #d7dde5;
  background: #14181d;
  border: 1px solid #2a313a;
  border-radius: 4px;
  cursor: pointer;
}

.shell-toggle:hover {
  border-color: #6ee7a8;
}

.focus-panel {
  position: fixed;
  top: 64px;
  right: 12px;
  width: 340px;
  max-height: calc(100vh - 88px);
  overflow-y: auto;
  padding: 10px 12px;
  background: rgba(20, 24, 29, 0.9);
  border: 1px solid #2a313a;
  border-radius: 4px;
}

.focus-panel header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
  color: #e8edf3;
}

.focus-panel header button {
  font: inherit;
  color: #d7dde5;
  background: none;
  border: none;
  cursor: pointer;
}

.focus-panel dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 2px 12px;
  margin: 0 0 8px;
}

.focus-panel dt {
  color: #8f9aa8;
}

.focus-panel dd {
  margin: 0;
}

.focus-panel-path {
  margin: 0 0 8px;
  color: #8f9aa8;
  word-break: break-all;
}

.focus-panel table {
  width: 100%;
  border-collapse: collapse;
}

.focus-panel th,
.focus-panel td {
  padding: 2px 4px;
  text-align: right;
}

.focus-panel th:first-child,
.focus-panel td:first-child {
  text-align: left;
}
```

- [ ] **Step 19: 통과 확인 (GREEN)**

Run: `npx vitest run --project jsdom tests/focus.test.tsx tests/nodeList.test.ts`
Expected: `Tests  12 passed (12)`. 출력에 `act(`·`Warning` 없음.

- [ ] **Step 20: `web/README.md` 갱신**

`## 레이어 구조` 코드 블록에서

```
visual/                   순수 TS: 크기·맥박·발광·색 매핑, 배치 시뮬레이션, 프레임 캐시
scene/                    R3F: 프레임당 sample() 한 번 → FrameCache → 노드가 key 로 읽는다
```

두 줄을 다음으로 바꾼다:

```
visual/                   순수 TS: 매핑, 배치, 프레임 캐시, 존재 추적, lifecycle 소비, 버스트·궤도·카메라 계산
scene/                    R3F: 프레임당 sample() 한 번 → 존재 추적기 → 노드·위성·입자가 key 로 읽는다
```

그 코드 블록 바로 아래 문단 끝에 다음 문장을 덧붙인다:

```markdown
천체를 클릭하면 카메라가 다가가고 자식 프로세스가 위성으로 펼쳐진다. `Esc` 나 빈 곳 클릭으로 돌아온다. GSAP 은 이 전환의 진행도 하나만 움직이고, 형성·페이드·붕괴는 `visual/presence.ts` 가 시각으로 계산한다.
```

- [ ] **Step 21: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  198 passed (198)`, 출력에 `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐 (새 `react(immutability)` 경고가 있으면 코드가 계획과 다른 것이다)
- build 성공, `index-*.js` 약 1.35 MB, 청크 크기 경고 없음

- [ ] **Step 22: 브라우저 확인은 하지 않는다**

컨트롤러가 실제 엔진에 붙여 확인한다. `pulse-engine` 을 띄우지 않는다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 23: 커밋**

```
git add package.json package-lock.json src/scene src/shell/shell.css tests/focus.test.tsx tests/nodeList.test.ts README.md
git commit -m "feat(web): form, fade and collapse bodies, burst particles, and focus a group with its satellites"
```

---

## Task 7: D 키 이월 항목 (M4)

**Files:**
- Modify (전체 교체): `web/src/shell/Shell.tsx`
- Test (전체 교체): `web/tests/shell.test.tsx`

**Interfaces:**
- Produces: `D` 단축키가 `event.code === 'KeyD'` 외에 `event.key` 가 `d`/`D` 인 경우도 받는다 (AZERTY·Dvorak). 장면 오류 뒤에도 `D` 키가 동작함을 테스트로 고정한다.

- [ ] **Step 1: 테스트 교체 — `web/tests/shell.test.tsx`** (기존 + 2개)

```tsx
import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Snapshot } from '../src/protocol/schema';
import { Shell } from '../src/shell/Shell';
import { toggledHash, viewFromHash } from '../src/shell/view';
import { useSnapshotStore } from '../src/state/snapshotStore';

// jsdom 에는 WebGL 이 없다. 장면 자체는 자동 테스트 대상이 아니므로 (계약서 10절)
// 여기서는 어느 화면이 선택되는지만 본다.
let universeRenderCount = 0;
let universeShouldThrow = false;

vi.mock('../src/scene/Universe', () => ({
  Universe: () => {
    universeRenderCount += 1;
    if (universeShouldThrow) {
      throw new Error('scene boom');
    }
    return <div>universe-stub</div>;
  },
}));

function setHash(hash: string) {
  act(() => {
    window.location.hash = hash;
    window.dispatchEvent(new HashChangeEvent('hashchange'));
  });
}

// tests/nodeList.test.ts 의 최소 스냅샷 모양을 그대로 쓴다.
function minimalSnapshot(seq: number): Snapshot {
  return {
    type: 'snapshot',
    v: 1,
    seq,
    t: seq,
    system: { cpu_pct: 0, mem_used_mb: 0, mem_total_mb: 0, process_total: 0, thread_total: 0 },
    cores: [],
    groups: [],
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  };
}

describe('viewFromHash', () => {
  it('shows the universe by default', () => {
    expect(viewFromHash('')).toBe('universe');
    expect(viewFromHash('#')).toBe('universe');
    expect(viewFromHash('#something-else')).toBe('universe');
  });

  it('shows the dashboard for #dashboard', () => {
    expect(viewFromHash('#dashboard')).toBe('dashboard');
  });

  it('toggles to the other view', () => {
    expect(toggledHash('universe')).toBe('#dashboard');
    expect(toggledHash('dashboard')).toBe('');
  });
});

describe('Shell', () => {
  beforeEach(() => {
    useSnapshotStore.getState().reset();
    universeRenderCount = 0;
    universeShouldThrow = false;
    setHash('');
  });

  afterEach(() => {
    setHash('');
  });

  it('renders the universe and a connection badge without a hash', () => {
    render(<Shell />);
    expect(screen.getByText('universe-stub')).toBeInTheDocument();
    expect(screen.getByText('closed')).toBeInTheDocument();
    expect(screen.queryByText(/data check/)).not.toBeInTheDocument();
  });

  it('renders only the dashboard for #dashboard', () => {
    setHash('#dashboard');
    render(<Shell />);
    expect(screen.getByText(/data check/)).toBeInTheDocument();
    expect(screen.queryByText('universe-stub')).not.toBeInTheDocument();
  });

  it('follows the hash when it changes', () => {
    render(<Shell />);
    setHash('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
    setHash('');
    expect(screen.getByText('universe-stub')).toBeInTheDocument();
  });

  it('switches views with the toggle button', async () => {
    const user = userEvent.setup();
    render(<Shell />);

    await user.click(screen.getByRole('button', { name: /dashboard/ }));
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
  });

  it('switches views with the D key', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
  });

  it('ignores D with a modifier or while typing', () => {
    render(
      <>
        <Shell />
        <input aria-label="field" />
      </>,
    );
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', ctrlKey: true });
    fireEvent.keyDown(screen.getByLabelText('field'), { key: 'd', code: 'KeyD' });
    expect(window.location.hash).toBe('');
  });

  // F3: 한글 IME 로 조합 중인 'ㅇ' 은 code 로만 잡는다. keyup 없이 계속 눌려
  // 반복 입력되는 D(자동 반복)와, 조합 중인 D 는 토글하지 않는다.
  it('toggles on a Korean IME keydown when the physical key is D', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'ㅇ', code: 'KeyD' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
  });

  it('toggles on a d key from a non-QWERTY layout', () => {
    // AZERTY·Dvorak 에서는 d 가 다른 물리 키에 있다. event.code 는 KeyD 가 아니다.
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyE' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
  });

  it('ignores an auto-repeated D keydown', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', repeat: true });
    expect(window.location.hash).toBe('');
  });

  it('ignores D while composing (IME)', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', isComposing: true });
    expect(window.location.hash).toBe('');
  });

  // F1: seq 는 1 Hz 로 바뀐다. Shell 이 그것을 구독하면 매초 <Universe/> 까지
  // 재렌더된다 (스펙 4절 규칙 3 위반). 배지만 구독해야 한다.
  it('does not re-render Universe when a new snapshot arrives', () => {
    render(<Shell />);
    const before = universeRenderCount;

    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(1), performance.now());
    });
    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(2), performance.now());
    });

    expect(universeRenderCount).toBe(before);
  });

  it('shows the connection state in the badge', () => {
    render(<Shell />);
    expect(screen.getByText('closed')).toBeInTheDocument();
  });

  it('shows the latest seq in the badge after a snapshot arrives', () => {
    render(<Shell />);
    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(7), performance.now());
    });
    expect(screen.getByText(/seq 7/)).toBeInTheDocument();
  });
});

// F2: 장면이 렌더 중 던지면 R3F 가 에러를 바깥으로 다시 던진다. 경계가 없으면
// 리액트가 루트 전체를 언마운트해 토글 버튼과 D 키까지 사라진다.
describe('Shell scene error boundary', () => {
  let consoleErrorSpy: ReturnType<typeof vi.spyOn>;

  beforeEach(() => {
    useSnapshotStore.getState().reset();
    universeRenderCount = 0;
    universeShouldThrow = true;
    setHash('');
    consoleErrorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });

  afterEach(() => {
    setHash('');
    universeShouldThrow = false;
    consoleErrorSpy.mockRestore();
  });

  it('falls back to a dashboard link and keeps the toggle button working', async () => {
    const user = userEvent.setup();
    render(<Shell />);

    expect(screen.getByText(/3D scene failed/)).toBeInTheDocument();
    expect(screen.getByRole('link', { name: '#dashboard' })).toHaveAttribute('href', '#dashboard');

    const toggle = screen.getByRole('button', { name: /dashboard/ });
    expect(toggle).toBeInTheDocument();

    await user.click(toggle);
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
  });

  it('keeps the D key working after the scene failed', () => {
    render(<Shell />);
    expect(screen.getByText(/3D scene failed/)).toBeInTheDocument();

    fireEvent.keyDown(window, { key: 'd', code: 'KeyD' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project jsdom tests/shell.test.tsx`
Expected: `toggles on a d key from a non-QWERTY layout` 1개 실패. `keeps the D key working after the scene failed` 는 통과한다 (이미 동작하는 것을 고정하는 테스트다).

- [ ] **Step 3: `web/src/shell/Shell.tsx` 전체 교체**

```tsx
import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { Universe } from '../scene/Universe';
import { SceneErrorBoundary } from './SceneErrorBoundary';
import { UniverseBadge } from './UniverseBadge';
import { toggledHash, viewFromHash } from './view';
import './shell.css';

function subscribeToHash(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

function currentHash(): string {
  return window.location.hash;
}

// 입력 중인 글자를 단축키로 가로채지 않는다.
function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))
  );
}

// 우주와 대시보드 중 하나만 마운트한다. 숨긴 쪽을 남겨 두면 대시보드의
// 100 ms 타이머와 WebGL 렌더 루프가 함께 돈다.
// Shell 은 스냅샷마다 바뀌는 값(status, seq)을 구독하지 않는다 — 구독하면
// <Universe/> 까지 매초 재렌더된다(스펙 4절 규칙 3). 배지는 UniverseBadge 가
// 따로 구독한다.
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
        return;
      }
      // 한글 IME 가 켜져 있으면 event.key 는 'ㅇ' 이나 조합 중 'Process' 로
      // 읽혀 물리 키를 알 수 없다. 그래서 물리 키(event.code)로 맞춘다.
      // 반대로 AZERTY·Dvorak 에서는 d 가 다른 물리 키에 있으므로 글자
      // (event.key)도 받는다. 조합 중(isComposing)이거나 키를 누르고 있어
      // 자동 반복(repeat)되는 입력은 무시한다 — 안 그러면 초당 30번씩 뷰가
      // 뒤집히며 WebGL 컨텍스트를 매번 새로 만든다.
      if (event.isComposing || event.repeat) {
        return;
      }
      if (event.code === 'KeyD' || event.key === 'd' || event.key === 'D') {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [toggle]);

  return (
    <>
      {view === 'dashboard' ? (
        <App />
      ) : (
        <>
          <SceneErrorBoundary>
            <Universe />
          </SceneErrorBoundary>
          <UniverseBadge />
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project jsdom tests/shell.test.tsx`
Expected: 전부 통과, `act(`·`Warning` 없음.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  200 passed (200)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 6: 커밋**

```
git add src/shell/Shell.tsx tests/shell.test.tsx
git commit -m "fix(web): accept the D shortcut by letter too, for non-QWERTY layouts"
```

---

## M5 완료 조건 (컨트롤러가 확인)

- [ ] 엔진 `pulse-tests.exe` 전부 통과(175), 빌드 경고 0. `[timing]` 10회 연속 통과.
- [ ] 웹 `npm test`(200), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `src/visual/` 이 `react`·`three`·`zustand`·`@react-three/*`·`gsap` 을 import 하지 않는다.
- [ ] 실제 엔진 72초 측정: 스냅샷 간격 중앙값 약 1000 ms, 목록 진입 간격당 0.2 이하.
- [ ] 브라우저 (dev 서버 + 엔진):
  - 300 MB 짜리 독립 프로세스(`Start-Process node -ArgumentList '-e','"const b=Buffer.alloc(300*1024*1024,1);setTimeout(()=>{},8000)"' -WindowStyle Hidden`)를 띄우면 무리 바깥에서 파티클이 모이며 천체가 형성되고, 끝나면 붕괴한다.
  - 천체 클릭 → 카메라가 다가가고, 다른 천체가 어두워지고, 위성이 펼쳐지고, 패널이 뜬다. `Esc`·빈 곳 클릭·같은 천체 재클릭으로 돌아온다. 드래그로 궤도를 돌려도 초점이 풀리지 않는다.
  - 위성 호버 툴팁이 이름·pid·MB·CPU 를 보여 주고, 패널의 값과 맞는다.
  - 콘솔에는 R3F 의 `THREE.Clock` 경고와 StrictMode 이중 연결 경고 외에 아무것도 없다.
- [ ] `npm run build` 후 `pulse-engine --serve --web-root ..\web\dist` 에서도 같다.

## 다음 단계

M6 가 CPU Core Orb 와 셰이더, `ambient` 배경 입자를 올린다. 파티클은 이 마일스톤의 `BurstPool`·`Particles` 와 같은 방식(공유 풀)을 따른다.
