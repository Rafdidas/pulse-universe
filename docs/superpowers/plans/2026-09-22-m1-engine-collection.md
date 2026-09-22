# M1 — C++ Monitoring Engine (수집 + `--dump`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `pulse-engine.exe --dump` 이 1초마다 프로세스 그룹 표를 콘솔에 출력하고, 그 숫자가 작업 관리자와 일치한다.

**Architecture:** 모든 OS 호출을 `ISystemReader` 인터페이스 뒤로 격리한다. 그 위의 계산 로직(CPU 델타, 그룹화, 필터링, 생명주기 추적, Flow 추정)은 순수 함수·순수 클래스라 가짜 표본으로 단위 테스트한다. `DataAggregator` 가 이들을 엮어 `SystemSnapshot` 을 만든다. M1에는 네트워크도 JSON도 없다 — 스냅샷을 콘솔 표로만 출력한다.

**Tech Stack:** C++20 / MSVC / CMake 3.25+ / vcpkg / Catch2 v3 / Win32 (Toolhelp32, PSAPI, PDH)

## Global Constraints

- C++20. `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`.
- MSVC 경고 수준 `/W4 /permissive- /utf-8`.
- **Boost는 M1에서 사용하지 않는다.** WebSocket은 M2 범위다.
- 모든 OS 호출은 `ISystemReader` 구현 안에만 존재한다. `src/core/` 의 어떤 파일도 `<windows.h>` 를 포함하지 않는다.
- 필드 이름은 스펙 4.4~4.5절과 **철자까지 동일**해야 한다. M2의 직렬화가 이 이름을 그대로 쓴다.
- 플랫폼 구현은 `src/platform/windows/` 에만 둔다. `src/platform/RawTypes.h` 와 `ISystemReader.h` 는 플랫폼 중립이다.
- 네임스페이스는 `pulse`.
- 스펙 원문: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`

### M1에서 의도적으로 제외한 것

- **`children[].role`** — 스펙 4.5절에 정의돼 있으나 `--type=renderer` 판별에는 프로세스 커맨드라인이 필요하고, 이는 PEB 읽기 또는 WMI를 요구한다. M1에서는 `"child"` 고정값을 넣는다. 실제 role 추출은 자식 노드를 화면에 그리는 M5에서 다룬다.
- JSON 직렬화, WebSocket, 프론트엔드 일체 (M2~M3).

## File Structure

```
engine/
  CMakeLists.txt                       빌드 정의
  CMakePresets.json                    VS 2022 생성기 + vcpkg 툴체인
  vcpkg.json                           의존성 (catch2)
  src/
    main.cpp                           CLI 진입점, --dump 루프
    platform/
      RawTypes.h                       RawProcess, RawCore, RawMemory, RawSample
      ISystemReader.h                  OS 접근 인터페이스
      windows/
        WindowsSystemReader.h/.cpp     Toolhelp32 + PSAPI + PDH 구현
    core/
      Snapshot.h                       ChildProcess, ProcessGroup, Ambient,
                                       CoreLoad, Flow, SystemTotals, SystemSnapshot
      CpuDelta.h/.cpp                  누적 CPU 시간 → 순간 사용률
      GroupBuilder.h/.cpp              프로세스 목록 → 트리 → 그룹
      ProcessFilter.h/.cpp             점수 계산 → 상위 N개 그룹 선택
      LifecycleTracker.h/.cpp          전체 집합 기준 생성/종료 감지
      FlowEstimator.h/.cpp             그룹 → 코어 흐름 추정
      DataAggregator.h/.cpp            위 전부를 엮어 SystemSnapshot 생성
    cli/
      TableFormatter.h/.cpp            SystemSnapshot → 콘솔 표 문자열
  tests/
    CMakeLists.txt
    fakes/FakeSystemReader.h           결정적 RawSample 주입
    test_smoke.cpp
    test_cpu_delta.cpp
    test_group_builder.cpp
    test_process_filter.cpp
    test_lifecycle_tracker.cpp
    test_flow_estimator.cpp
    test_aggregator.cpp
    test_table_formatter.cpp
    test_windows_reader.cpp            통합 테스트 (불변식만 검증)
```

책임 분리 원칙: `core/` 의 각 파일은 하나의 변환만 담당하고 서로를 모른다. `DataAggregator` 만이 이들을 안다. 이 구조 덕분에 나중에 ETW 수집기를 넣을 때 `FlowEstimator` 만 교체된다.

---

## Task 1: 툴체인 설치 및 빌드/테스트 골격

이 PC에는 C++ 컴파일러가 없다 (`cl`, `clang`, `g++`, `cmake`, `ninja`, Visual Studio 모두 부재 확인됨). 코드를 쓰기 전에 설치가 필요하다.

**Files:**
- Create: `engine/CMakeLists.txt`
- Create: `engine/CMakePresets.json`
- Create: `engine/vcpkg.json`
- Create: `engine/src/main.cpp`
- Create: `engine/tests/CMakeLists.txt`
- Create: `engine/tests/test_smoke.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: 없음
- Produces: 빌드 가능한 `pulse-engine` 실행 파일과 `pulse-tests` 테스트 실행 파일. 이후 모든 Task가 이 두 타깃에 소스를 추가한다.

- [ ] **Step 1: Visual Studio Build Tools 설치**

관리자 PowerShell에서 실행한다. 다운로드가 수 GB이므로 시간이 걸린다.

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"
```

- [ ] **Step 2: CMake 설치**

```powershell
winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
```

- [ ] **Step 3: vcpkg 설치 및 부트스트랩**

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
[Environment]::SetEnvironmentVariable('VCPKG_ROOT','C:\vcpkg','User')
```

- [ ] **Step 4: 설치 검증 — 새 PowerShell 창을 열고 실행**

```powershell
cmake --version; $env:VCPKG_ROOT; & "$env:VCPKG_ROOT\vcpkg.exe" version
```

Expected: `cmake version 3.2x.x` 이상, `C:\vcpkg`, `vcpkg package management program version ...`. 셋 중 하나라도 실패하면 새 창에서 다시 시도한다 (PATH 갱신은 새 세션에만 적용된다).

- [ ] **Step 5: `engine/vcpkg.json` 작성**

```json
{
  "name": "pulse-engine",
  "version": "0.1.0",
  "dependencies": ["catch2"]
}
```

- [ ] **Step 6: `engine/CMakePresets.json` 작성**

Visual Studio 생성기를 쓰는 이유: 개발자 명령 프롬프트 없이 일반 PowerShell에서 바로 빌드된다.

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "default",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ],
  "buildPresets": [
    { "name": "default", "configurePreset": "default", "configuration": "Debug" }
  ],
  "testPresets": [
    {
      "name": "default",
      "configurePreset": "default",
      "configuration": "Debug",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

- [ ] **Step 7: `engine/CMakeLists.txt` 작성**

`pulse_core` 라이브러리는 아직 소스가 없으므로 Task 2에서 만든다.

```cmake
cmake_minimum_required(VERSION 3.25)
project(pulse_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
endif()

add_executable(pulse-engine src/main.cpp)
target_include_directories(pulse-engine PRIVATE src)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 8: `engine/src/main.cpp` 작성**

```cpp
#include <cstdio>

int main() {
    std::printf("pulse-engine 0.1.0\n");
    return 0;
}
```

- [ ] **Step 9: `engine/tests/CMakeLists.txt` 작성**

```cmake
find_package(Catch2 3 CONFIG REQUIRED)

add_executable(pulse-tests
  test_smoke.cpp
)
target_include_directories(pulse-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pulse-tests PRIVATE Catch2::Catch2WithMain)

add_test(NAME pulse-tests COMMAND pulse-tests)
```

- [ ] **Step 10: `engine/tests/test_smoke.cpp` 작성 — 실패하는 테스트**

테스트 실행기 자체가 동작하는지 먼저 확인한다. 일부러 틀린 값을 넣는다.

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("test harness runs", "[smoke]") {
    REQUIRE(1 + 1 == 3);
}
```

- [ ] **Step 11: 설정 및 빌드**

```powershell
cd C:\dev\pulse-uni\engine; cmake --preset default
```

Expected: vcpkg가 catch2를 빌드한다 (최초 수 분 소요). 마지막 줄에 `-- Build files have been written to: C:/dev/pulse-uni/engine/build`.

```powershell
cmake --build --preset default
```

Expected: `pulse-engine.vcxproj` 와 `pulse-tests.vcxproj` 빌드 성공, 경고 0.

- [ ] **Step 12: 테스트 실행 — 실패 확인**

```powershell
ctest --preset default
```

Expected: FAIL. 출력에 `REQUIRE( 1 + 1 == 3 )` 와 `with expansion: 2 == 3` 이 보인다. 이 실패는 테스트 실행기가 정상 동작한다는 증거다.

- [ ] **Step 13: 테스트를 올바르게 수정**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("test harness runs", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 14: 테스트 통과 확인 및 실행 파일 확인**

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed, 0 tests failed out of 1`.

```powershell
.\build\Debug\pulse-engine.exe
```

Expected: `pulse-engine 0.1.0`

- [ ] **Step 15: `.gitignore` 에 vcpkg 산출물 경로 확인 후 커밋**

`.gitignore` 에 이미 `build/` 와 `vcpkg_installed/` 가 있다. 없으면 추가한다.

```powershell
cd C:\dev\pulse-uni; git status --short
```

Expected: `engine/` 아래 소스 파일만 나열되고 `build/` 는 보이지 않는다.

```bash
git add engine/CMakeLists.txt engine/CMakePresets.json engine/vcpkg.json engine/src/main.cpp engine/tests/CMakeLists.txt engine/tests/test_smoke.cpp .gitignore
git commit -m "build: add C++ engine skeleton with CMake, vcpkg and Catch2"
```

---

## Task 2: CpuDelta — 누적 CPU 시간을 순간 사용률로 변환

**Files:**
- Create: `engine/src/platform/RawTypes.h`
- Create: `engine/src/core/CpuDelta.h`
- Create: `engine/src/core/CpuDelta.cpp`
- Create: `engine/tests/test_cpu_delta.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `pulse::Account` — `enum class { User, System }`
  - `pulse::RawProcess`, `pulse::RawCore`, `pulse::RawMemory`, `pulse::RawSample`
  - `pulse::CpuDelta::CpuDelta(unsigned core_count)`
  - `std::optional<double> pulse::CpuDelta::update(uint32_t pid, uint64_t cpu_cumulative_ms, uint64_t timestamp_ms)`
  - `void pulse::CpuDelta::forget(uint32_t pid)`

- [ ] **Step 1: `engine/src/platform/RawTypes.h` 작성**

플랫폼 중립 원시 표본 타입. Windows 헤더를 포함하지 않는다.

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pulse {

enum class Account { User, System };

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

struct RawSample {
    std::vector<RawProcess> processes;
    std::vector<RawCore> cores;
    RawMemory memory;
    uint64_t timestamp_ms = 0;
};

}  // namespace pulse
```

- [ ] **Step 2: 실패하는 테스트 작성 — `engine/tests/test_cpu_delta.cpp`**

스펙 6.1절의 예시(124ms / 1000ms / 16코어 = 0.775%)를 그대로 테스트로 옮긴다.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/CpuDelta.h"

using namespace pulse;

TEST_CASE("first sample yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    REQUIRE_FALSE(delta.update(100, 12450, 1000).has_value());
}

TEST_CASE("second sample matches spec worked example", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 12450, 1000);

    const auto pct = delta.update(100, 12574, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(0.775, 0.0001));
}

TEST_CASE("full saturation of every core reads as 100 percent", "[cpu]") {
    CpuDelta delta(4);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 4000, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(100.0, 0.0001));
}

TEST_CASE("result is clamped when cpu time exceeds wall time times cores", "[cpu]") {
    CpuDelta delta(2);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 9999, 2000);

    REQUIRE(pct.has_value());
    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(100.0, 0.0001));
}

TEST_CASE("a backwards counter yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 5000, 1000);

    REQUIRE_FALSE(delta.update(100, 4000, 2000).has_value());
}

TEST_CASE("a non advancing clock yields no percentage", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 5000, 1000);

    REQUIRE_FALSE(delta.update(100, 6000, 1000).has_value());
}

TEST_CASE("pids are tracked independently", "[cpu]") {
    CpuDelta delta(10);
    delta.update(1, 0, 1000);
    delta.update(2, 0, 1000);

    const auto a = delta.update(1, 1000, 2000);
    const auto b = delta.update(2, 500, 2000);

    REQUIRE_THAT(*a, Catch::Matchers::WithinAbs(10.0, 0.0001));
    REQUIRE_THAT(*b, Catch::Matchers::WithinAbs(5.0, 0.0001));
}

TEST_CASE("forget drops the stored sample", "[cpu]") {
    CpuDelta delta(16);
    delta.update(100, 12450, 1000);
    delta.forget(100);

    REQUIRE_FALSE(delta.update(100, 12574, 2000).has_value());
}

TEST_CASE("zero core count is treated as one core", "[cpu]") {
    CpuDelta delta(0);
    delta.update(100, 0, 1000);

    const auto pct = delta.update(100, 500, 2000);

    REQUIRE_THAT(*pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}
```

- [ ] **Step 3: 테스트 파일을 빌드에 등록**

`engine/tests/CMakeLists.txt` 의 `add_executable` 블록을 다음으로 교체한다.

```cmake
add_executable(pulse-tests
  test_smoke.cpp
  test_cpu_delta.cpp
)
target_include_directories(pulse-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pulse-tests PRIVATE Catch2::Catch2WithMain pulse_core)
```

`engine/CMakeLists.txt` 에서 `add_executable(pulse-engine ...)` 바로 위에 `pulse_core` 라이브러리를 추가한다.

```cmake
add_library(pulse_core
  src/core/CpuDelta.cpp
)
target_include_directories(pulse_core PUBLIC src)

add_executable(pulse-engine src/main.cpp)
target_link_libraries(pulse-engine PRIVATE pulse_core)
```

`add_executable(pulse-engine ...)` 아래의 기존 `target_include_directories(pulse-engine PRIVATE src)` 줄은 삭제한다. `pulse_core` 가 `PUBLIC` 으로 전파한다.

- [ ] **Step 4: 테스트 실패 확인**

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/CpuDelta.h'` 와 `LNK1181: cannot open input file` 계열 오류.

- [ ] **Step 5: `engine/src/core/CpuDelta.h` 작성**

```cpp
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
```

- [ ] **Step 6: `engine/src/core/CpuDelta.cpp` 작성**

```cpp
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
```

- [ ] **Step 7: 테스트 통과 확인**

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`. Catch2 출력에 `All tests passed (10 assertions in 9 test cases)` 계열 문구.

- [ ] **Step 8: 커밋**

```bash
git add engine/src/platform/RawTypes.h engine/src/core/CpuDelta.h engine/src/core/CpuDelta.cpp engine/tests/test_cpu_delta.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): derive instantaneous cpu usage from cumulative samples"
```

---

## Task 3: GroupBuilder — 프로세스 목록을 트리 루트로 묶기

**Files:**
- Create: `engine/src/core/Snapshot.h`
- Create: `engine/src/core/GroupBuilder.h`
- Create: `engine/src/core/GroupBuilder.cpp`
- Create: `engine/tests/test_group_builder.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::RawProcess`, `pulse::Account` (Task 2)
- Produces:
  - `pulse::ChildProcess`, `pulse::ProcessGroup`, `pulse::Ambient` (in `core/Snapshot.h`)
  - `pulse::GroupingResult` — `{ std::vector<ProcessGroup> groups; Ambient ambient; }`
  - `pulse::GroupBuilder::build(const std::vector<RawProcess>&) const -> GroupingResult`

- [ ] **Step 1: 실패하는 테스트 작성 — `engine/tests/test_group_builder.cpp`**

스펙 5.1~5.2절의 규칙을 그대로 테스트로 옮긴다. 실측 데이터(whale.exe 26개, PPID 22008)를 근거로 한 형태를 사용한다.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#include "core/GroupBuilder.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid,
                       uint32_t ppid,
                       std::string name,
                       uint64_t mem_bytes = 1024ull * 1024ull,
                       uint64_t start_time_ms = 1000,
                       Account account = Account::User) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.mem_bytes = mem_bytes;
    p.thread_count = 1;
    p.start_time_ms = start_time_ms;
    p.account = account;
    p.image_path = "C:/app/" + p.name;
    return p;
}

const ProcessGroup* findGroup(const GroupingResult& result, const std::string& key) {
    const auto it = std::find_if(result.groups.begin(), result.groups.end(),
                                 [&](const ProcessGroup& g) { return g.key == key; });
    return it == result.groups.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("a lone process becomes its own group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({makeProcess(100, 4, "explorer.exe")});

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].key == "explorer.exe:100");
    REQUIRE(result.groups[0].root_pid == 100);
    REQUIRE(result.groups[0].proc_count == 1);
    REQUIRE(result.groups[0].children.empty());
}

TEST_CASE("children sharing the parent name collapse into one group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(22008, 21876, "whale.exe", 400ull * 1024 * 1024, 1000),
        makeProcess(8400, 22008, "whale.exe", 570ull * 1024 * 1024, 2000),
        makeProcess(22264, 22008, "whale.exe", 238ull * 1024 * 1024, 2000),
    });

    REQUIRE(result.groups.size() == 1);
    const ProcessGroup& g = result.groups[0];
    REQUIRE(g.key == "whale.exe:22008");
    REQUIRE(g.root_pid == 22008);
    REQUIRE(g.proc_count == 3);
    REQUIRE(g.children.size() == 2);
    REQUIRE_THAT(g.mem_mb, Catch::Matchers::WithinAbs(1208.0, 0.01));
}

TEST_CASE("a differently named child starts its own group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(500, 4, "Code.exe"),
        makeProcess(600, 500, "node.exe"),
    });

    REQUIRE(result.groups.size() == 2);
    REQUIRE(findGroup(result, "Code.exe:500") != nullptr);
    REQUIRE(findGroup(result, "node.exe:600") != nullptr);
}

TEST_CASE("grandchildren of the same name reach the true root", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(10, 4, "app.exe", 1024 * 1024, 1000),
        makeProcess(11, 10, "app.exe", 1024 * 1024, 2000),
        makeProcess(12, 11, "app.exe", 1024 * 1024, 3000),
    });

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].root_pid == 10);
    REQUIRE(result.groups[0].proc_count == 3);
}

TEST_CASE("a parent started after its child is not treated as the parent", "[group]") {
    // pid 재사용 방어. 부모의 시작 시각이 자식보다 늦으면 실제 부모가 아니다.
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(300, 4, "app.exe", 1024 * 1024, 9000),
        makeProcess(301, 300, "app.exe", 1024 * 1024, 1000),
    });

    REQUIRE(result.groups.size() == 2);
}

TEST_CASE("a process whose parent is gone becomes a root", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({makeProcess(700, 99999, "orphan.exe")});

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].root_pid == 700);
}

TEST_CASE("a parent cycle does not hang the builder", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(1, 2, "loop.exe", 1024 * 1024, 1000),
        makeProcess(2, 1, "loop.exe", 1024 * 1024, 1000),
    });

    REQUIRE_FALSE(result.groups.empty());
}

TEST_CASE("svchost is excluded from groups and counted as ambient", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(800, 4, "svchost.exe", 100ull * 1024 * 1024, 1000, Account::System),
        makeProcess(801, 4, "SvcHost.exe", 100ull * 1024 * 1024, 1000, Account::System),
        makeProcess(900, 4, "explorer.exe", 50ull * 1024 * 1024, 1000),
    });

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].name == "explorer.exe");
    REQUIRE(result.ambient.service_proc_count == 2);
    REQUIRE_THAT(result.ambient.service_mem_mb, Catch::Matchers::WithinAbs(200.0, 0.01));
}

TEST_CASE("group metadata is taken from the root process", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(50, 4, "app.exe", 1024 * 1024, 5000, Account::User),
        makeProcess(51, 50, "app.exe", 1024 * 1024, 6000, Account::System),
    });

    REQUIRE(result.groups[0].started_at == 5000);
    REQUIRE(result.groups[0].account == Account::User);
    REQUIRE(result.groups[0].image_path == "C:/app/app.exe");
}

TEST_CASE("thread counts are summed across the group", "[group]") {
    const GroupBuilder builder;
    auto root = makeProcess(60, 4, "app.exe");
    root.thread_count = 12;
    auto child = makeProcess(61, 60, "app.exe", 1024 * 1024, 2000);
    child.thread_count = 30;

    const auto result = builder.build({root, child});

    REQUIRE(result.groups[0].thread_count == 42);
}

TEST_CASE("children carry the placeholder role in M1", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(70, 4, "app.exe", 1024 * 1024, 1000),
        makeProcess(71, 70, "app.exe", 1024 * 1024, 2000),
    });

    REQUIRE(result.groups[0].children[0].role == "child");
}
```

- [ ] **Step 2: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_group_builder.cpp` 를 추가한다.

```cmake
add_executable(pulse-tests
  test_smoke.cpp
  test_cpu_delta.cpp
  test_group_builder.cpp
)
```

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/GroupBuilder.h'`.

- [ ] **Step 3: `engine/src/core/Snapshot.h` 작성**

스펙 4.4~4.5절의 필드 이름과 철자를 그대로 따른다.

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platform/RawTypes.h"

namespace pulse {

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

struct Ambient {
    uint32_t service_proc_count = 0;
    double service_mem_mb = 0.0;
};

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/core/GroupBuilder.h` 작성**

```cpp
#pragma once

#include <vector>

#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

struct GroupingResult {
    std::vector<ProcessGroup> groups;
    Ambient ambient;
};

// 프로세스 목록을 트리 루트 기준으로 묶는다.
// 부모와 자식의 이미지 이름이 같으면 부모 쪽으로 계속 올라가고,
// 이름이 달라지는 지점에서 멈춘다. 특정 프로그램 이름을 하드코딩하지 않는다.
class GroupBuilder {
public:
    GroupingResult build(const std::vector<RawProcess>& processes) const;
};

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/core/GroupBuilder.cpp` 작성**

```cpp
#include "core/GroupBuilder.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace pulse {
namespace {

constexpr int kMaxParentHops = 32;
constexpr double kBytesPerMb = 1024.0 * 1024.0;

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

bool isAmbientService(const RawProcess& p) {
    return equalsIgnoreCase(p.name, "svchost.exe");
}

double toMb(uint64_t bytes) {
    return static_cast<double>(bytes) / kBytesPerMb;
}

}  // namespace

GroupingResult GroupBuilder::build(const std::vector<RawProcess>& processes) const {
    GroupingResult result;

    std::unordered_map<uint32_t, const RawProcess*> by_pid;
    by_pid.reserve(processes.size());
    for (const RawProcess& p : processes) {
        if (isAmbientService(p)) {
            result.ambient.service_proc_count += 1;
            result.ambient.service_mem_mb += toMb(p.mem_bytes);
            continue;
        }
        by_pid.emplace(p.pid, &p);
    }

    // pid -> 그 프로세스가 속한 트리의 루트 pid
    const auto resolveRoot = [&](const RawProcess& start) -> uint32_t {
        const RawProcess* current = &start;
        std::unordered_set<uint32_t> visited;
        for (int hop = 0; hop < kMaxParentHops; ++hop) {
            if (!visited.insert(current->pid).second) {
                break;  // 순환. 현재 지점을 루트로 삼는다.
            }
            const auto parent_it = by_pid.find(current->ppid);
            if (parent_it == by_pid.end()) {
                break;  // 부모가 없거나 제외된 프로세스다.
            }
            const RawProcess* parent = parent_it->second;
            if (!equalsIgnoreCase(parent->name, current->name)) {
                break;  // 이름이 달라지는 지점이 그룹 경계다.
            }
            if (parent->start_time_ms > current->start_time_ms) {
                break;  // pid 재사용. 실제 부모가 아니다.
            }
            current = parent;
        }
        return current->pid;
    };

    // 루트별로 구성원을 모은다. 입력 순서를 보존해 결과를 결정적으로 유지한다.
    std::unordered_map<uint32_t, size_t> index_by_root;
    std::vector<std::vector<const RawProcess*>> members;
    std::vector<uint32_t> root_order;

    for (const RawProcess& p : processes) {
        if (isAmbientService(p)) {
            continue;
        }
        const uint32_t root = resolveRoot(p);
        const auto it = index_by_root.find(root);
        if (it == index_by_root.end()) {
            index_by_root.emplace(root, members.size());
            members.push_back({&p});
            root_order.push_back(root);
        } else {
            members[it->second].push_back(&p);
        }
    }

    result.groups.reserve(root_order.size());
    for (size_t i = 0; i < root_order.size(); ++i) {
        const uint32_t root_pid = root_order[i];
        const auto root_it = by_pid.find(root_pid);
        if (root_it == by_pid.end()) {
            continue;
        }
        const RawProcess& root = *root_it->second;

        ProcessGroup group;
        group.key = root.name + ":" + std::to_string(root.pid);
        group.name = root.name;
        group.root_pid = root.pid;
        group.started_at = root.start_time_ms;
        group.account = root.account;
        group.image_path = root.image_path;

        for (const RawProcess* member : members[i]) {
            group.mem_mb += toMb(member->mem_bytes);
            group.thread_count += member->thread_count;
            group.proc_count += 1;
            if (member->pid == root.pid) {
                continue;
            }
            ChildProcess child;
            child.pid = member->pid;
            child.name = member->name;
            child.mem_mb = toMb(member->mem_bytes);
            child.threads = member->thread_count;
            group.children.push_back(std::move(child));
        }

        result.groups.push_back(std::move(group));
    }

    return result;
}

}  // namespace pulse
```

- [ ] **Step 6: 소스를 빌드에 등록하고 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `add_library(pulse_core ...)` 목록에 `src/core/GroupBuilder.cpp` 를 추가한다.

```cmake
add_library(pulse_core
  src/core/CpuDelta.cpp
  src/core/GroupBuilder.cpp
)
```

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 7: 커밋**

```bash
git add engine/src/core/Snapshot.h engine/src/core/GroupBuilder.h engine/src/core/GroupBuilder.cpp engine/tests/test_group_builder.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): group processes by tree root with pid-reuse guard"
```

---

## Task 4: ProcessFilter — 상위 N개 그룹 선택

**Files:**
- Create: `engine/src/core/ProcessFilter.h`
- Create: `engine/src/core/ProcessFilter.cpp`
- Create: `engine/tests/test_process_filter.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::ProcessGroup`, `pulse::Account` (Task 3)
- Produces:
  - `pulse::FilterConfig` — `{ size_t max_groups = 40; double cpu_weight = 1.0; double mem_weight = 1.0; double user_account_bonus = 1.5; }`
  - `pulse::ProcessFilter::ProcessFilter(FilterConfig cfg = {})`
  - `std::vector<ProcessGroup> pulse::ProcessFilter::select(std::vector<ProcessGroup> groups, double total_mem_mb) const`

- [ ] **Step 1: 실패하는 테스트 작성 — `engine/tests/test_process_filter.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

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
```

- [ ] **Step 2: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_process_filter.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/ProcessFilter.h'`.

- [ ] **Step 3: `engine/src/core/ProcessFilter.h` 작성**

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct FilterConfig {
    size_t max_groups = 40;
    double cpu_weight = 1.0;
    double mem_weight = 1.0;
    // 사용자가 직접 띄운 프로그램이 배경 서비스보다 우선 노출되게 한다.
    double user_account_bonus = 1.5;
};

// 그룹 점수를 계산해 상위 N개만 남긴다.
class ProcessFilter {
public:
    explicit ProcessFilter(FilterConfig cfg = {});

    std::vector<ProcessGroup> select(std::vector<ProcessGroup> groups,
                                     double total_mem_mb) const;

private:
    FilterConfig cfg_;
};

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/core/ProcessFilter.cpp` 작성**

```cpp
#include "core/ProcessFilter.h"

#include <algorithm>

namespace pulse {

ProcessFilter::ProcessFilter(FilterConfig cfg) : cfg_(cfg) {}

std::vector<ProcessGroup> ProcessFilter::select(std::vector<ProcessGroup> groups,
                                                double total_mem_mb) const {
    const auto score = [&](const ProcessGroup& g) {
        const double cpu = g.cpu_pct.value_or(0.0);
        const double mem_norm =
            total_mem_mb > 0.0 ? (g.mem_mb / total_mem_mb) * 100.0 : 0.0;
        const double base = cpu * cfg_.cpu_weight + mem_norm * cfg_.mem_weight;
        const double bonus = g.account == Account::User ? cfg_.user_account_bonus : 1.0;
        return base * bonus;
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

- [ ] **Step 5: 소스 등록 및 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/core/ProcessFilter.cpp` 를 추가한 뒤:

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 6: 커밋**

```bash
git add engine/src/core/ProcessFilter.h engine/src/core/ProcessFilter.cpp engine/tests/test_process_filter.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): score and select the top process groups"
```

---

## Task 5: LifecycleTracker — 전체 집합 기준 생성/종료 감지

스펙 4.6절: 필터된 목록에서 사라진 것과 실제 종료된 것을 구분해야 한다. 이 추적기는 **필터 전 전체 프로세스**를 입력받는다.

**Files:**
- Create: `engine/src/core/LifecycleTracker.h`
- Create: `engine/src/core/LifecycleTracker.cpp`
- Create: `engine/tests/test_lifecycle_tracker.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::RawProcess` (Task 2)
- Produces:
  - `pulse::SpawnedProcess` — `{ uint32_t pid; uint32_t ppid; std::string name; }`
  - `pulse::LifecycleDelta` — `{ std::vector<SpawnedProcess> spawned; std::vector<uint32_t> terminated; }`
  - `pulse::LifecycleTracker::update(const std::vector<RawProcess>&) -> LifecycleDelta`

- [ ] **Step 1: 실패하는 테스트 작성 — `engine/tests/test_lifecycle_tracker.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/LifecycleTracker.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid, uint32_t ppid, std::string name,
                       uint64_t start_time_ms = 1000) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.start_time_ms = start_time_ms;
    return p;
}

bool containsPid(const std::vector<uint32_t>& pids, uint32_t pid) {
    return std::find(pids.begin(), pids.end(), pid) != pids.end();
}

}  // namespace

TEST_CASE("the first update reports nothing", "[lifecycle]") {
    // 기동 시 이미 떠 있던 수백 개가 전부 생성으로 잡히면
    // 프론트엔드가 생성 애니메이션을 수백 번 재생하게 된다.
    LifecycleTracker tracker;

    const auto delta = tracker.update({
        makeProcess(1, 0, "a.exe"),
        makeProcess(2, 0, "b.exe"),
    });

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("a new process is reported as spawned", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe")});

    const auto delta = tracker.update({
        makeProcess(1, 0, "a.exe"),
        makeProcess(2, 1, "child.exe"),
    });

    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].pid == 2);
    REQUIRE(delta.spawned[0].ppid == 1);
    REQUIRE(delta.spawned[0].name == "child.exe");
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("a vanished process is reported as terminated", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe"), makeProcess(2, 1, "b.exe")});

    const auto delta = tracker.update({makeProcess(1, 0, "a.exe")});

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.size() == 1);
    REQUIRE(delta.terminated[0] == 2);
}

TEST_CASE("a reused pid counts as both a termination and a spawn", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(5, 0, "old.exe", 1000)});

    const auto delta = tracker.update({makeProcess(5, 0, "new.exe", 9000)});

    REQUIRE(containsPid(delta.terminated, 5));
    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].pid == 5);
    REQUIRE(delta.spawned[0].name == "new.exe");
}

TEST_CASE("a steady process set produces no events", "[lifecycle]") {
    LifecycleTracker tracker;
    const std::vector<RawProcess> set = {makeProcess(1, 0, "a.exe"),
                                         makeProcess(2, 0, "b.exe")};
    tracker.update(set);

    const auto delta = tracker.update(set);

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("everything disappearing reports every pid as terminated", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe"), makeProcess(2, 0, "b.exe")});

    const auto delta = tracker.update({});

    REQUIRE(delta.terminated.size() == 2);
    REQUIRE(containsPid(delta.terminated, 1));
    REQUIRE(containsPid(delta.terminated, 2));
}
```

- [ ] **Step 2: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_lifecycle_tracker.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/LifecycleTracker.h'`.

- [ ] **Step 3: `engine/src/core/LifecycleTracker.h` 작성**

```cpp
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
```

- [ ] **Step 4: `engine/src/core/LifecycleTracker.cpp` 작성**

```cpp
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
```

- [ ] **Step 5: 소스 등록 및 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/core/LifecycleTracker.cpp` 를 추가한 뒤:

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 6: 커밋**

```bash
git add engine/src/core/LifecycleTracker.h engine/src/core/LifecycleTracker.cpp engine/tests/test_lifecycle_tracker.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): detect real spawns and exits across the full process set"
```

---

## Task 6: FlowEstimator — 그룹에서 코어로의 흐름 추정

스펙 6.2절. 실측이 아니므로 전부 `source: "estimated"` 로 표시한다.

**Files:**
- Create: `engine/src/core/FlowEstimator.h`
- Create: `engine/src/core/FlowEstimator.cpp`
- Create: `engine/tests/test_flow_estimator.cpp`
- Modify: `engine/src/core/Snapshot.h`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::ProcessGroup` (Task 3)
- Produces:
  - `pulse::CoreLoad` — `{ uint32_t id = 0; double pct = 0.0; }` (in `core/Snapshot.h`)
  - `pulse::Flow` — `{ std::string group; uint32_t core = 0; double weight = 0.0; std::string source = "estimated"; }` (in `core/Snapshot.h`)
  - `pulse::FlowConfig` — `{ double min_weight = 0.05; size_t max_flows_per_group = 4; }`
  - `pulse::FlowEstimator::FlowEstimator(FlowConfig cfg = {})`
  - `std::vector<Flow> pulse::FlowEstimator::estimate(const std::vector<ProcessGroup>&, const std::vector<CoreLoad>&) const`

- [ ] **Step 1: `engine/src/core/Snapshot.h` 에 `CoreLoad` 와 `Flow` 추가**

`struct Ambient` 정의 바로 앞에 다음을 삽입한다.

```cpp
struct CoreLoad {
    uint32_t id = 0;
    double pct = 0.0;
};

struct Flow {
    std::string group;  // ProcessGroup::key
    uint32_t core = 0;
    double weight = 0.0;
    // M1 은 추정만 한다. ETW 수집기가 들어오면 "measured" 가 된다.
    std::string source = "estimated";
};
```

- [ ] **Step 2: 실패하는 테스트 작성 — `engine/tests/test_flow_estimator.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

#include "core/FlowEstimator.h"

using namespace pulse;

namespace {

ProcessGroup makeGroup(std::string name, uint32_t pid, std::optional<double> cpu_pct) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = pid;
    g.key = name + ":" + std::to_string(pid);
    g.cpu_pct = cpu_pct;
    return g;
}

const Flow* find(const std::vector<Flow>& flows, const std::string& key, uint32_t core) {
    const auto it = std::find_if(flows.begin(), flows.end(), [&](const Flow& f) {
        return f.group == key && f.core == core;
    });
    return it == flows.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("every flow is marked as estimated in M1", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 100.0}});

    REQUIRE_FALSE(flows.empty());
    REQUIRE(flows[0].source == "estimated");
}

TEST_CASE("a single busy core takes the whole weight of the busiest group", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 80.0}});

    const Flow* f = find(flows, "a.exe:1", 0);
    REQUIRE(f != nullptr);
    REQUIRE_THAT(f->weight, Catch::Matchers::WithinAbs(1.0, 0.0001));
}

TEST_CASE("weight splits in proportion to core load", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("a.exe", 1, 50.0)},
        {CoreLoad{0, 75.0}, CoreLoad{1, 25.0}});

    REQUIRE_THAT(find(flows, "a.exe:1", 0)->weight,
                 Catch::Matchers::WithinAbs(0.75, 0.0001));
    REQUIRE_THAT(find(flows, "a.exe:1", 1)->weight,
                 Catch::Matchers::WithinAbs(0.25, 0.0001));
}

TEST_CASE("a quieter group produces proportionally weaker flows", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("busy.exe", 1, 80.0), makeGroup("quiet.exe", 2, 20.0)},
        {CoreLoad{0, 100.0}});

    REQUIRE_THAT(find(flows, "busy.exe:1", 0)->weight,
                 Catch::Matchers::WithinAbs(1.0, 0.0001));
    REQUIRE_THAT(find(flows, "quiet.exe:2", 0)->weight,
                 Catch::Matchers::WithinAbs(0.25, 0.0001));
}

TEST_CASE("flows below the minimum weight are dropped", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.5;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("busy.exe", 1, 100.0), makeGroup("quiet.exe", 2, 1.0)},
        {CoreLoad{0, 100.0}});

    REQUIRE(find(flows, "busy.exe:1", 0) != nullptr);
    REQUIRE(find(flows, "quiet.exe:2", 0) == nullptr);
}

TEST_CASE("only the strongest cores per group are kept", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    cfg.max_flows_per_group = 2;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("a.exe", 1, 100.0)},
        {CoreLoad{0, 10.0}, CoreLoad{1, 40.0}, CoreLoad{2, 30.0}, CoreLoad{3, 20.0}});

    REQUIRE(flows.size() == 2);
    REQUIRE(find(flows, "a.exe:1", 1) != nullptr);
    REQUIRE(find(flows, "a.exe:1", 2) != nullptr);
}

TEST_CASE("a group without a cpu reading produces no flows", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("warming.exe", 1, std::nullopt)},
                                          {CoreLoad{0, 100.0}});

    REQUIRE(flows.empty());
}

TEST_CASE("fully idle cores produce no flows", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 0.0}, CoreLoad{1, 0.0}});

    REQUIRE(flows.empty());
}

TEST_CASE("no groups yields no flows", "[flow]") {
    const FlowEstimator estimator;

    REQUIRE(estimator.estimate({}, {CoreLoad{0, 50.0}}).empty());
}
```

- [ ] **Step 3: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_flow_estimator.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/FlowEstimator.h'`.

- [ ] **Step 4: `engine/src/core/FlowEstimator.h` 작성**

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct FlowConfig {
    // 이보다 약한 흐름은 화면을 어지럽히기만 하므로 버린다.
    double min_weight = 0.05;
    size_t max_flows_per_group = 4;
};

// 그룹의 CPU 점유율을 코어별 부하에 비례 배분해 흐름 세기를 만든다.
// 실측이 아니므로 결과는 전부 source == "estimated" 다.
class FlowEstimator {
public:
    explicit FlowEstimator(FlowConfig cfg = {});

    std::vector<Flow> estimate(const std::vector<ProcessGroup>& groups,
                               const std::vector<CoreLoad>& cores) const;

private:
    FlowConfig cfg_;
};

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/core/FlowEstimator.cpp` 작성**

```cpp
#include "core/FlowEstimator.h"

#include <algorithm>

namespace pulse {

FlowEstimator::FlowEstimator(FlowConfig cfg) : cfg_(cfg) {}

std::vector<Flow> FlowEstimator::estimate(const std::vector<ProcessGroup>& groups,
                                          const std::vector<CoreLoad>& cores) const {
    std::vector<Flow> flows;

    double total_core_pct = 0.0;
    for (const CoreLoad& c : cores) {
        total_core_pct += c.pct;
    }
    if (total_core_pct <= 0.0) {
        return flows;
    }

    double max_group_cpu = 0.0;
    for (const ProcessGroup& g : groups) {
        max_group_cpu = std::max(max_group_cpu, g.cpu_pct.value_or(0.0));
    }
    if (max_group_cpu <= 0.0) {
        return flows;
    }

    std::vector<Flow> per_group;
    for (const ProcessGroup& g : groups) {
        if (!g.cpu_pct.has_value() || *g.cpu_pct <= 0.0) {
            continue;
        }
        const double activity = *g.cpu_pct / max_group_cpu;

        per_group.clear();
        for (const CoreLoad& c : cores) {
            const double weight = activity * (c.pct / total_core_pct);
            if (weight < cfg_.min_weight) {
                continue;
            }
            Flow f;
            f.group = g.key;
            f.core = c.id;
            f.weight = weight;
            per_group.push_back(std::move(f));
        }

        std::stable_sort(per_group.begin(), per_group.end(),
                         [](const Flow& a, const Flow& b) { return a.weight > b.weight; });
        if (per_group.size() > cfg_.max_flows_per_group) {
            per_group.resize(cfg_.max_flows_per_group);
        }
        flows.insert(flows.end(), per_group.begin(), per_group.end());
    }

    return flows;
}

}  // namespace pulse
```

- [ ] **Step 6: 소스 등록 및 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/core/FlowEstimator.cpp` 를 추가한 뒤:

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 7: 커밋**

```bash
git add engine/src/core/Snapshot.h engine/src/core/FlowEstimator.h engine/src/core/FlowEstimator.cpp engine/tests/test_flow_estimator.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): estimate group-to-core flows from load distribution"
```

---

## Task 7: DataAggregator — 전부를 엮어 SystemSnapshot 만들기

**Files:**
- Create: `engine/src/core/DataAggregator.h`
- Create: `engine/src/core/DataAggregator.cpp`
- Create: `engine/tests/test_aggregator.cpp`
- Modify: `engine/src/core/Snapshot.h`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CpuDelta` (Task 2), `GroupBuilder` (Task 3), `ProcessFilter` (Task 4), `LifecycleTracker` (Task 5), `FlowEstimator` (Task 6)
- Produces:
  - `pulse::SystemTotals` — `{ std::optional<double> cpu_pct; double mem_used_mb = 0.0; double mem_total_mb = 0.0; uint32_t process_total = 0; uint32_t thread_total = 0; }`
  - `pulse::SystemSnapshot` — `{ uint64_t seq = 0; uint64_t t = 0; SystemTotals system; std::vector<CoreLoad> cores; std::vector<ProcessGroup> groups; std::vector<Flow> flows; LifecycleDelta lifecycle; Ambient ambient; }`
  - `pulse::AggregatorConfig` — `{ FilterConfig filter; FlowConfig flow; }`
  - `pulse::DataAggregator::DataAggregator(unsigned core_count, AggregatorConfig cfg = {})`
  - `SystemSnapshot pulse::DataAggregator::aggregate(const RawSample&)`

- [ ] **Step 1: `engine/src/core/Snapshot.h` 에 `SystemTotals` 와 `SystemSnapshot` 추가**

파일 상단의 include 목록에 `#include "core/LifecycleTracker.h"` 를 추가하고, 파일 끝 `}  // namespace pulse` 직전에 다음을 삽입한다.

```cpp
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
```

- [ ] **Step 2: 실패하는 테스트 작성 — `engine/tests/test_aggregator.cpp`**

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
```

- [ ] **Step 3: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_aggregator.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'core/DataAggregator.h'`.

- [ ] **Step 4: `engine/src/core/DataAggregator.h` 작성**

```cpp
#pragma once

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
    uint64_t seq_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/core/DataAggregator.cpp` 작성**

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
    if (!sample.cores.empty() && seq_ > 1) {
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

        if (any) {
            g.cpu_pct = sum;
        }
    }

    // 6. 상위 N개 선택. 합계와 생명주기는 이미 전체 기준으로 계산됐다.
    snapshot.groups = filter_.select(std::move(grouped.groups),
                                     snapshot.system.mem_total_mb);

    // 7. 화면에 남은 그룹에 대해서만 흐름을 추정한다.
    snapshot.flows = flow_estimator_.estimate(snapshot.groups, snapshot.cores);

    return snapshot;
}

}  // namespace pulse
```

- [ ] **Step 6: 소스 등록 및 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/core/DataAggregator.cpp` 를 추가한 뒤:

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 7: 커밋**

```bash
git add engine/src/core/Snapshot.h engine/src/core/DataAggregator.h engine/src/core/DataAggregator.cpp engine/tests/test_aggregator.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): assemble raw samples into system snapshots"
```

---

## Task 8: WindowsSystemReader — 실제 OS 데이터 읽기

여기가 M1에서 유일하게 Win32를 호출하는 곳이다. 다른 어떤 파일도 `<windows.h>` 를 포함하지 않는다.

**Files:**
- Create: `engine/src/platform/ISystemReader.h`
- Create: `engine/src/platform/windows/WindowsSystemReader.h`
- Create: `engine/src/platform/windows/WindowsSystemReader.cpp`
- Create: `engine/tests/test_windows_reader.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::RawSample` (Task 2)
- Produces:
  - `pulse::ISystemReader` — 순수 가상 `virtual RawSample read() = 0;`
  - `pulse::WindowsSystemReader::WindowsSystemReader()` / `~WindowsSystemReader()`
  - `RawSample pulse::WindowsSystemReader::read() override`
  - `unsigned pulse::WindowsSystemReader::coreCount() const`

- [ ] **Step 1: `engine/src/platform/ISystemReader.h` 작성**

```cpp
#pragma once

#include "platform/RawTypes.h"

namespace pulse {

// OS 접근의 유일한 경계. core/ 의 모든 로직은 이 뒤의 구현을 모른다.
class ISystemReader {
public:
    virtual ~ISystemReader() = default;
    virtual RawSample read() = 0;
    virtual unsigned coreCount() const = 0;
};

}  // namespace pulse
```

- [ ] **Step 2: 통합 테스트 작성 — `engine/tests/test_windows_reader.cpp`**

실제 시스템 값은 매번 달라지므로 정확한 값이 아니라 **불변식**을 검증한다. 테스트가 자기 자신의 프로세스를 찾는 것으로 정확성을 확인한다.

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "platform/windows/WindowsSystemReader.h"

#include <windows.h>

using namespace pulse;

TEST_CASE("the reader returns a plausible process list", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(sample.processes.size() > 10);
    REQUIRE(sample.timestamp_ms > 0);

    for (const RawProcess& p : sample.processes) {
        REQUIRE(p.pid != 0);
        REQUIRE_FALSE(p.name.empty());
    }
}

TEST_CASE("the reader finds this very test process", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();
    const auto self = static_cast<uint32_t>(::GetCurrentProcessId());

    const auto it = std::find_if(sample.processes.begin(), sample.processes.end(),
                                 [&](const RawProcess& p) { return p.pid == self; });

    REQUIRE(it != sample.processes.end());
    REQUIRE(it->name == "pulse-tests.exe");
    REQUIRE(it->mem_bytes > 0);
    REQUIRE(it->thread_count > 0);
    REQUIRE(it->start_time_ms > 0);
    REQUIRE(it->account == Account::User);
    REQUIRE_FALSE(it->image_path.empty());
}

TEST_CASE("the reader reports one entry per logical core", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(reader.coreCount() > 0);
    REQUIRE(sample.cores.size() == reader.coreCount());
    for (const RawCore& c : sample.cores) {
        REQUIRE(c.pct >= 0.0);
        REQUIRE(c.pct <= 100.0);
    }
}

TEST_CASE("the reader reports plausible memory totals", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(sample.memory.total_bytes > 1024ull * 1024ull * 1024ull);
    REQUIRE(sample.memory.used_bytes > 0);
    REQUIRE(sample.memory.used_bytes < sample.memory.total_bytes);
}

TEST_CASE("cpu time never goes backwards between reads", "[reader][integration]") {
    WindowsSystemReader reader;
    const auto self = static_cast<uint32_t>(::GetCurrentProcessId());

    const auto findSelf = [&](const RawSample& s) {
        return std::find_if(s.processes.begin(), s.processes.end(),
                            [&](const RawProcess& p) { return p.pid == self; });
    };

    const RawSample first = reader.read();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const RawSample second = reader.read();

    const auto a = findSelf(first);
    const auto b = findSelf(second);
    REQUIRE(a != first.processes.end());
    REQUIRE(b != second.processes.end());
    REQUIRE(b->cpu_cumulative_ms >= a->cpu_cumulative_ms);
    REQUIRE(second.timestamp_ms > first.timestamp_ms);
}
```

- [ ] **Step 3: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_windows_reader.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'platform/windows/WindowsSystemReader.h'`.

- [ ] **Step 4: `engine/src/platform/windows/WindowsSystemReader.h` 작성**

PDH 핸들을 `void*` 로 보관해 헤더가 `<windows.h>` 를 노출하지 않게 한다.

```cpp
#pragma once

#include "platform/ISystemReader.h"

namespace pulse {

// Toolhelp32 로 프로세스를 열거하고, PSAPI 로 메모리를, PDH 로 코어별 부하를 읽는다.
// PDH 카운터는 두 번째 수집부터 값이 나오므로 생성자에서 한 번 수집해 둔다.
class WindowsSystemReader final : public ISystemReader {
public:
    WindowsSystemReader();
    ~WindowsSystemReader() override;

    WindowsSystemReader(const WindowsSystemReader&) = delete;
    WindowsSystemReader& operator=(const WindowsSystemReader&) = delete;

    RawSample read() override;
    unsigned coreCount() const override;

private:
    void* query_ = nullptr;    // PDH_HQUERY
    void* counter_ = nullptr;  // PDH_HCOUNTER
    unsigned core_count_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/platform/windows/WindowsSystemReader.cpp` 작성**

```cpp
#include "platform/windows/WindowsSystemReader.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

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

// 프로세스 핸들을 열어 얻을 수 있는 것만 채운다.
// 권한이 부족하거나 보호된 프로세스면 조용히 건너뛴다.
void enrichFromHandle(RawProcess& process) {
    const HANDLE handle = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, process.pid);
    if (handle == nullptr) {
        return;
    }

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (::GetProcessTimes(handle, &creation, &exit, &kernel, &user) != 0) {
        process.start_time_ms = fileTimeToUnixMs(creation);
        const uint64_t busy_100ns = fileTimeTo100ns(kernel) + fileTimeTo100ns(user);
        process.cpu_cumulative_ms = busy_100ns / 10000ull;
    }

    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(handle, &counters, sizeof(counters)) != 0) {
        process.mem_bytes = counters.WorkingSetSize;
    }

    wchar_t path[MAX_PATH] = {};
    DWORD path_len = MAX_PATH;
    if (::QueryFullProcessImageNameW(handle, 0, path, &path_len) != 0) {
        process.image_path = toUtf8(path);
    }

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

WindowsSystemReader::WindowsSystemReader() : core_count_(logicalCoreCount()) {
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

RawSample WindowsSystemReader::read() {
    RawSample sample;
    sample.timestamp_ms = nowUnixMs();

    // --- 프로세스 열거 ---
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry) != 0) {
            do {
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
                        // "_Total" 은 합계 항목이라 코어가 아니다.
                        // 논리 프로세서가 64개를 넘는 장비에서는 "0,3" 같은
                        // 프로세서 그룹 표기가 섞여 들어오므로 숫자만인 것만 받는다.
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

- [ ] **Step 6: 소스 등록 및 라이브러리 링크**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/platform/windows/WindowsSystemReader.cpp` 를 추가하고, `target_include_directories(pulse_core ...)` 바로 아래에 링크 설정을 넣는다.

```cmake
add_library(pulse_core
  src/core/CpuDelta.cpp
  src/core/GroupBuilder.cpp
  src/core/ProcessFilter.cpp
  src/core/LifecycleTracker.cpp
  src/core/FlowEstimator.cpp
  src/core/DataAggregator.cpp
  src/platform/windows/WindowsSystemReader.cpp
)
target_include_directories(pulse_core PUBLIC src)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi)
endif()
```

- [ ] **Step 7: 테스트 통과 확인**

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`. `cpu time never goes backwards` 테스트는 약 200ms 걸린다.

일부 프로세스의 `mem_bytes` 가 0으로 남는 것은 정상이다 (보호된 시스템 프로세스). 관리자 권한으로 실행하면 그 수가 줄어든다.

- [ ] **Step 8: 커밋**

```bash
git add engine/src/platform/ISystemReader.h engine/src/platform/windows/WindowsSystemReader.h engine/src/platform/windows/WindowsSystemReader.cpp engine/tests/test_windows_reader.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): read live process, core and memory data from Windows"
```

---

## Task 9: TableFormatter 와 `--dump` CLI

M1의 최종 산출물. 이 단계가 끝나면 작업 관리자와 숫자를 대조할 수 있다.

**Files:**
- Create: `engine/src/cli/TableFormatter.h`
- Create: `engine/src/cli/TableFormatter.cpp`
- Create: `engine/tests/test_table_formatter.cpp`
- Modify: `engine/src/main.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::SystemSnapshot` (Task 7), `pulse::WindowsSystemReader` (Task 8)
- Produces:
  - `std::string pulse::formatSnapshotTable(const SystemSnapshot&)`

- [ ] **Step 1: 실패하는 테스트 작성 — `engine/tests/test_table_formatter.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "cli/TableFormatter.h"

using namespace pulse;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

SystemSnapshot makeSnapshot() {
    SystemSnapshot s;
    s.seq = 7;
    s.t = 1758531600123;
    s.system.cpu_pct = 34.2;
    s.system.mem_used_mb = 18432.0;
    s.system.mem_total_mb = 32768.0;
    s.system.process_total = 382;
    s.system.thread_total = 4187;
    s.cores = {CoreLoad{0, 82.4}, CoreLoad{1, 12.1}};

    ProcessGroup g;
    g.key = "whale.exe:22008";
    g.name = "whale.exe";
    g.root_pid = 22008;
    g.cpu_pct = 12.4;
    g.mem_mb = 3626.0;
    g.proc_count = 26;
    g.thread_count = 412;
    g.account = Account::User;
    s.groups.push_back(g);

    s.ambient.service_proc_count = 84;
    s.ambient.service_mem_mb = 1400.0;
    return s;
}

}  // namespace

TEST_CASE("the table shows the sequence number and system totals", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "seq 7"));
    REQUIRE(contains(out, "382"));
    REQUIRE(contains(out, "4187"));
}

TEST_CASE("the table lists each group with its figures", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "whale.exe"));
    REQUIRE(contains(out, "22008"));
    REQUIRE(contains(out, "26"));
    REQUIRE(contains(out, "3626"));
    REQUIRE(contains(out, "12.4"));
}

TEST_CASE("a group without a cpu reading shows a dash", "[format]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].cpu_pct.reset();
    s.system.cpu_pct.reset();

    const std::string out = formatSnapshotTable(s);

    REQUIRE(contains(out, "-"));
    REQUIRE_FALSE(contains(out, "12.4"));
}

TEST_CASE("the table shows per core load", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "82.4"));
    REQUIRE(contains(out, "12.1"));
}

TEST_CASE("the table shows the ambient service summary", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "84"));
    REQUIRE(contains(out, "1400"));
}

TEST_CASE("the account is shown per group", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "user"));
}

TEST_CASE("an empty snapshot still renders a header", "[format]") {
    SystemSnapshot s;

    const std::string out = formatSnapshotTable(s);

    REQUIRE(contains(out, "GROUP"));
}
```

- [ ] **Step 2: 테스트 파일 등록 및 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_table_formatter.cpp` 를 추가한 뒤:

```powershell
cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'cli/TableFormatter.h'`.

- [ ] **Step 3: `engine/src/cli/TableFormatter.h` 작성**

```cpp
#pragma once

#include <string>

#include "core/Snapshot.h"

namespace pulse {

// 스냅샷을 작업 관리자와 대조하기 좋은 콘솔 표로 만든다.
std::string formatSnapshotTable(const SystemSnapshot& snapshot);

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/cli/TableFormatter.cpp` 작성**

```cpp
#include "cli/TableFormatter.h"

#include <iomanip>
#include <sstream>

namespace pulse {
namespace {

std::string formatOptionalPct(const std::optional<double>& value) {
    if (!value.has_value()) {
        return "-";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << *value;
    return out.str();
}

}  // namespace

std::string formatSnapshotTable(const SystemSnapshot& snapshot) {
    std::ostringstream out;
    out << std::fixed;

    out << "seq " << snapshot.seq << "   t " << snapshot.t << "\n";
    out << "cpu " << formatOptionalPct(snapshot.system.cpu_pct) << " %"
        << "   mem " << std::setprecision(0) << snapshot.system.mem_used_mb << " / "
        << snapshot.system.mem_total_mb << " MB"
        << "   processes " << snapshot.system.process_total << "   threads "
        << snapshot.system.thread_total << "\n";

    out << "cores ";
    for (const CoreLoad& core : snapshot.cores) {
        out << "[" << core.id << "] " << std::setprecision(1) << core.pct << "  ";
    }
    out << "\n\n";

    out << std::left << std::setw(28) << "GROUP" << std::right << std::setw(8) << "PID"
        << std::setw(8) << "PROCS" << std::setw(10) << "CPU%" << std::setw(12) << "MEM MB"
        << std::setw(9) << "THREADS" << "  ACCOUNT" << "\n";
    out << std::string(84, '-') << "\n";

    for (const ProcessGroup& group : snapshot.groups) {
        std::string name = group.name;
        if (name.size() > 27) {
            name = name.substr(0, 27);
        }
        out << std::left << std::setw(28) << name << std::right << std::setw(8)
            << group.root_pid << std::setw(8) << group.proc_count << std::setw(10)
            << formatOptionalPct(group.cpu_pct) << std::setw(12) << std::setprecision(1)
            << group.mem_mb << std::setw(9) << group.thread_count << "  "
            << (group.account == Account::User ? "user" : "system") << "\n";
    }

    out << "\nambient services  " << snapshot.ambient.service_proc_count
        << " procs   " << std::setprecision(0) << snapshot.ambient.service_mem_mb
        << " MB\n";

    if (!snapshot.lifecycle.spawned.empty() || !snapshot.lifecycle.terminated.empty()) {
        out << "lifecycle  spawned " << snapshot.lifecycle.spawned.size()
            << "   terminated " << snapshot.lifecycle.terminated.size() << "\n";
    }

    out << "flows " << snapshot.flows.size() << " (estimated)\n";

    return out.str();
}

}  // namespace pulse
```

- [ ] **Step 5: 소스 등록 및 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 목록에 `src/cli/TableFormatter.cpp` 를 추가한 뒤:

```powershell
cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`.

- [ ] **Step 6: `engine/src/main.cpp` 를 `--dump` 루프로 교체**

```cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "cli/TableFormatter.h"
#include "core/DataAggregator.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

struct Options {
    bool dump = false;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
};

void printUsage() {
    std::printf(
        "pulse-engine 0.1.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000).\n"
        "  --iterations N    Stop after N snapshots (default: run until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n");
}

bool parseUnsigned(const char* text, unsigned& out) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    out = static_cast<unsigned>(value);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            options.dump = true;
        } else if (std::strcmp(arg, "--interval-ms") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.interval_ms)) {
                std::printf("invalid --interval-ms\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--iterations") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.iterations)) {
                std::printf("invalid --iterations\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--max-groups") == 0 && i + 1 < argc) {
            unsigned value = 0;
            if (!parseUnsigned(argv[++i], value)) {
                std::printf("invalid --max-groups\n");
                return 2;
            }
            options.max_groups = value;
        } else {
            printUsage();
            return 2;
        }
    }

    if (!options.dump) {
        printUsage();
        return 0;
    }

    pulse::WindowsSystemReader reader;

    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    for (unsigned n = 0; options.iterations == 0 || n < options.iterations; ++n) {
        const pulse::RawSample sample = reader.read();
        const pulse::SystemSnapshot snapshot = aggregator.aggregate(sample);

        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
    }

    return 0;
}
```

- [ ] **Step 7: 빌드 후 실제 실행 — 첫 스냅샷의 CPU가 비어 있는지 확인**

```powershell
cmake --build --preset default; .\build\Debug\pulse-engine.exe --dump --iterations 1
```

Expected: 표가 한 번 출력되고, `cpu` 와 모든 그룹의 `CPU%` 열이 `-` 다. 스펙 6.1절이 말한 첫 주기 동작이다.

- [ ] **Step 8: 두 번째 스냅샷부터 CPU 값이 나오는지 확인**

```powershell
.\build\Debug\pulse-engine.exe --dump --iterations 3
```

Expected: 첫 표는 `CPU%` 가 `-`, 두 번째·세 번째 표에는 숫자가 들어 있다. `GROUP` 열에 `Code.exe`, `whale.exe`, `explorer.exe` 같은 실제 이름이 보인다.

- [ ] **Step 9: 작업 관리자와 대조 — M1의 인수 조건**

관리자 권한 PowerShell에서 실행한다.

```powershell
.\build\Debug\pulse-engine.exe --dump --iterations 5 --max-groups 12
```

작업 관리자(Ctrl+Shift+Esc)를 열고 **세부 정보** 탭이 아니라 **프로세스** 탭을 본다. 작업 관리자도 앱 단위로 자식 프로세스를 합산하므로 비교 대상이 같다.

확인할 것:

1. `MEM MB` 상위 항목이 작업 관리자 메모리 상위 항목과 같은 프로그램들인가
2. 큰 그룹의 메모리 값이 작업 관리자 표시값의 ±10% 안인가 (작업 관리자는 공유 메모리를 다르게 집계하므로 정확히 같지는 않다)
3. `PROCS` 값이 작업 관리자에서 해당 앱을 펼쳤을 때의 자식 수와 비슷한가
4. 아무것도 하지 않을 때 `cpu` 합계가 한 자릿수인가
5. 브라우저에서 무거운 페이지를 열면 그 그룹의 `CPU%` 가 눈에 띄게 오르는가

값이 크게 어긋나면 진행하지 말고 원인을 찾는다. 여기서 검증되지 않은 숫자는 M4 이후에 셰이더 문제로 위장한다.

- [ ] **Step 10: 생성/종료 감지 확인**

```powershell
.\build\Debug\pulse-engine.exe --dump --interval-ms 2000 --max-groups 5
```

실행 중인 상태에서 다른 창으로 메모장을 켰다 끈다.

Expected: 메모장을 켠 직후 표에 `lifecycle  spawned 1   terminated 0`, 끈 직후 `lifecycle  spawned 0   terminated 1` 이 나타난다. 첫 표에는 `lifecycle` 줄이 없어야 한다 (기준선을 세우는 주기이므로).

Ctrl+C로 중단한다.

- [ ] **Step 11: 커밋**

```bash
git add engine/src/cli/TableFormatter.h engine/src/cli/TableFormatter.cpp engine/src/main.cpp engine/tests/test_table_formatter.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): add --dump mode printing the process group table"
```

- [ ] **Step 12: 푸시**

```bash
git push origin main
```

---

## M1 완료 조건

- [ ] `ctest --preset default` 가 전부 통과한다.
- [ ] `pulse-engine.exe --dump` 가 실제 프로세스 그룹 표를 출력한다.
- [ ] 첫 스냅샷의 CPU 열이 `-` 이고, 두 번째부터 숫자가 나온다.
- [ ] 메모리 상위 그룹이 작업 관리자와 일치한다.
- [ ] 프로그램을 켜고 끄면 `lifecycle` 에 `spawned` / `terminated` 가 각각 잡힌다.
- [ ] `src/core/` 의 어떤 파일도 `<windows.h>` 를 포함하지 않는다.

확인 명령:

```powershell
cd C:\dev\pulse-uni\engine; Select-String -Path src\core\*.h,src\core\*.cpp -Pattern 'windows.h'
```

Expected: 출력 없음.

## 다음 단계

M2는 이 `SystemSnapshot` 을 JSON으로 직렬화하고 Boost.Beast WebSocket으로 내보낸다. `--dump` 는 그대로 남겨둔다. 데이터가 의심스러울 때마다 돌아올 검증 도구다.
