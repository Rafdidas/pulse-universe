# M2 — 직렬화와 WebSocket 전송 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `pulse-engine.exe --serve` 가 1초마다 계약서 4.4절 형식의 JSON 스냅샷을 WebSocket 으로 내보낸다.

**Architecture:** M1 이 만든 `SystemSnapshot` 은 그대로 둔다. 그 위에 세 개의 독립 부품을 얹는다 — `Serializer`(스냅샷 → JSON 문자열, 순수 함수), `EngineLoop`(샘플링 전용 스레드, 콜백만 호출), `WebSocketServer`(문자열을 세션들에 브로드캐스트). 셋은 서로를 모르고 `main` 이 엮는다.

**Tech Stack:** C++20 / MSVC / CMake / vcpkg / Catch2 v3 / Boost.JSON / Boost.Beast / Boost.Asio

## Global Constraints

- C++20. `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`.
- MSVC 경고 수준 `/W4 /permissive- /utf-8`. 경고는 결함이다.
- `src/core/` 의 어떤 파일도 `<windows.h>` 를 포함하지 않는다. 이 제약은 M2 에서도 유지된다.
- `src/network/` 의 어떤 파일도 `<windows.h>` 를 직접 포함하지 않는다. Asio 가 내부적으로 끌어오는 것은 무방하다.
- 플랫폼 구현은 `src/platform/windows/` 에만 둔다.
- 네임스페이스는 `pulse`.
- 와이어 필드 이름은 계약서 4.3~4.5절과 **철자까지 동일**해야 한다. 프론트엔드가 이 이름을 그대로 읽는다.
- 리스닝 소켓은 `127.0.0.1` 에만 바인딩한다. `0.0.0.0` 바인딩 경로를 코드에 두지 않는다.
- 계약은 서버 → 클라이언트 **단방향**이다. 서버는 클라이언트 프레임을 읽되 해석하지 않는다.
- 허용 의존성: `catch2`, `boost-json`, `boost-beast`. 그 외 추가 금지.
- 스펙 원문: `docs/superpowers/specs/2026-09-23-m2-transport-design.md`
- 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`

### 환경

빌드 도구가 셸 PATH 에 없다. 모든 빌드/테스트 명령 앞에 붙인다:

```
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

이는 셸 환경 문제이므로 커밋되는 파일에 PATH 우회를 넣지 않는다.

### M2 에서 하지 않는 것

- 정적 파일 서빙 (스펙 D9 — M3).
- 프론트엔드 일체.
- TLS, 인증, 클라이언트 → 서버 명령.
- ETW 기반 실측 스레드-코어 매핑.

## File Structure

```
engine/
  vcpkg.json                          boost-json, boost-beast 추가
  CMakeLists.txt                      Boost 링크, 새 소스 등록
  src/
    network/
      Serializer.h/.cpp               SystemSnapshot → JSON. 소켓을 모른다
      ServerConfig.h                  포트, 허용 Origin
      WebSocketServer.h/.cpp          Beast 수락 루프, 세션, 브로드캐스트
    app/
      EngineLoop.h/.cpp               샘플링 스레드. reader → aggregator → 콜백
    platform/
      RawTypes.h                      HostInfo 추가
      ISystemReader.h                 hostInfo() 추가
      windows/WindowsSystemReader.*   hostInfo() 구현
    cli/Options.h/.cpp                Mode 도입 (--json, --serve)
    main.cpp                          세 모드 배선
  tests/
    fakes/FakeSystemReader.h          결정적 리더 + 던지는 리더
    test_serializer.cpp
    test_engine_loop.cpp
    test_websocket_server.cpp
```

`src/core/` 와 `src/platform/windows/WindowsSystemReader.cpp` 의 수집 로직은 건드리지 않는다. M1 이 검증한 부분이다.

---

## Task 1: Boost 도입과 Serializer

**Files:**
- Modify: `engine/vcpkg.json`
- Modify: `engine/CMakeLists.txt`
- Create: `engine/src/network/Serializer.h`
- Create: `engine/src/network/Serializer.cpp`
- Create: `engine/tests/test_serializer.cpp`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::SystemSnapshot`, `pulse::ProcessGroup`, `pulse::ChildProcess`, `pulse::Flow`, `pulse::LifecycleDelta`, `pulse::SpawnedProcess`, `pulse::CoreLoad`, `pulse::Ambient`, `pulse::SystemTotals`, `pulse::Account` — 전부 `core/Snapshot.h` 에 이미 있다.
- Produces:
  - `pulse::kProtocolVersion` — `inline constexpr int`, 값 1
  - `pulse::HelloInfo` — `{ unsigned interval_ms; unsigned core_count; bool elevated; std::string os; std::string thread_mapping; }`
  - `std::string pulse::serializeHello(const HelloInfo&)`
  - `std::string pulse::serializeSnapshot(const SystemSnapshot&)`

- [ ] **Step 1: `engine/vcpkg.json` 에 의존성 추가**

Beast 도 지금 함께 넣는다. vcpkg 의 Boost 설치는 최초 1회에 수 분 걸리므로 Task 4 에서 다시 기다리지 않게 한다.

```json
{
  "name": "pulse-engine",
  "version": "0.1.0",
  "dependencies": ["catch2", "boost-json", "boost-beast"]
}
```

- [ ] **Step 2: `engine/CMakeLists.txt` 에 Boost 링크 추가**

`add_library(pulse_core ...)` 블록 **위에** `find_package` 를 넣는다.

```cmake
find_package(Boost REQUIRED COMPONENTS json)
```

그리고 `add_library(pulse_core ...)` 의 소스 목록에 `src/network/Serializer.cpp` 를 추가한 뒤, `target_include_directories(pulse_core PUBLIC src)` 바로 아래의 링크 블록을 다음으로 교체한다.

```cmake
target_link_libraries(pulse_core PUBLIC Boost::json)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi)
endif()
```

- [ ] **Step 3: 의존성 설치 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --preset default
```

Expected: vcpkg 가 boost-json 과 boost-beast 및 그 의존성을 빌드한다. 최초 실행은 수 분 걸린다. 마지막 줄에 `-- Build files have been written to: C:/dev/pulse-uni/engine/build`.

실패하면 코드를 고치지 말고 오류 전문과 함께 BLOCKED 로 보고한다.

- [ ] **Step 4: 실패하는 테스트 작성 — `engine/tests/test_serializer.cpp`**

계약서 4.3~4.5절의 필드를 하나씩 확인한다. 숫자는 `to_number<int64_t>()` 로 읽는다 — `unsigned` 로 담긴 값은 `as_int64()` 가 던진다.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <boost/json.hpp>

#include <string>

#include "network/Serializer.h"

using namespace pulse;
namespace json = boost::json;

namespace {

SystemSnapshot makeSnapshot() {
    SystemSnapshot s;
    s.seq = 1423;
    s.t = 1758531600123ull;
    s.system.cpu_pct = 34.2;
    s.system.mem_used_mb = 18432.0;
    s.system.mem_total_mb = 32553.0;
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
    g.started_at = 1758520000000ull;
    g.account = Account::User;
    g.image_path = "C:/Program Files/Naver/Whale/whale.exe";

    ChildProcess c;
    c.pid = 8400;
    c.name = "whale.exe";
    c.role = "child";
    c.cpu_pct = 4.1;
    c.mem_mb = 570.5;
    c.threads = 18;
    g.children.push_back(c);

    s.groups.push_back(g);

    Flow f;
    f.group = "whale.exe:22008";
    f.core = 3;
    f.weight = 0.42;
    s.flows.push_back(f);

    SpawnedProcess sp;
    sp.pid = 20114;
    sp.ppid = 22008;
    sp.name = "whale.exe";
    sp.group = "whale.exe:22008";
    s.lifecycle.spawned.push_back(sp);
    s.lifecycle.terminated.push_back(18002);

    s.ambient.service_proc_count = 84;
    s.ambient.service_mem_mb = 1400.0;
    return s;
}

json::object parseObject(const std::string& text) {
    return json::parse(text).as_object();
}

}  // namespace

TEST_CASE("hello carries the envelope, capabilities and host", "[serialize]") {
    HelloInfo info;
    info.interval_ms = 1000;
    info.core_count = 28;
    info.elevated = true;
    info.os = "Windows 11";

    const json::object o = parseObject(serializeHello(info));

    REQUIRE(o.at("type").as_string() == "hello");
    REQUIRE(o.at("v").to_number<int64_t>() == 1);
    REQUIRE(o.at("interval_ms").to_number<int64_t>() == 1000);
    REQUIRE(o.at("core_count").to_number<int64_t>() == 28);
    REQUIRE(o.at("capabilities").as_object().at("thread_mapping").as_string() == "estimated");
    REQUIRE(o.at("host").as_object().at("os").as_string() == "Windows 11");
    REQUIRE(o.at("host").as_object().at("elevated").as_bool());
}

TEST_CASE("snapshot carries the protocol envelope", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));

    REQUIRE(o.at("type").as_string() == "snapshot");
    REQUIRE(o.at("v").to_number<int64_t>() == 1);
    REQUIRE(o.at("seq").to_number<int64_t>() == 1423);
    REQUIRE(o.at("t").to_number<int64_t>() == 1758531600123ll);
}

TEST_CASE("system totals are serialized", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& sys = o.at("system").as_object();

    REQUIRE_THAT(sys.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(34.2, 0.0001));
    REQUIRE_THAT(sys.at("mem_used_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(18432.0, 0.0001));
    REQUIRE_THAT(sys.at("mem_total_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(32553.0, 0.0001));
    REQUIRE(sys.at("process_total").to_number<int64_t>() == 382);
    REQUIRE(sys.at("thread_total").to_number<int64_t>() == 4187);
}

TEST_CASE("an absent cpu reading is serialized as null, not zero", "[serialize]") {
    // 계약서 6.1: 첫 주기에는 CPU 값이 없다. 0.0 과 구분되어야 한다.
    SystemSnapshot s = makeSnapshot();
    s.system.cpu_pct.reset();
    s.groups[0].cpu_pct.reset();
    s.groups[0].children[0].cpu_pct.reset();

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("system").as_object().at("cpu_pct").is_null());
    const json::object& g = o.at("groups").as_array().at(0).as_object();
    REQUIRE(g.at("cpu_pct").is_null());
    REQUIRE(g.at("children").as_array().at(0).as_object().at("cpu_pct").is_null());
}

TEST_CASE("cores are serialized in order", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::array& cores = o.at("cores").as_array();

    REQUIRE(cores.size() == 2);
    REQUIRE(cores.at(0).as_object().at("id").to_number<int64_t>() == 0);
    REQUIRE_THAT(cores.at(0).as_object().at("pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(82.4, 0.0001));
    REQUIRE(cores.at(1).as_object().at("id").to_number<int64_t>() == 1);
}

TEST_CASE("a group carries every contract field", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& g = o.at("groups").as_array().at(0).as_object();

    REQUIRE(g.at("key").as_string() == "whale.exe:22008");
    REQUIRE(g.at("name").as_string() == "whale.exe");
    REQUIRE(g.at("root_pid").to_number<int64_t>() == 22008);
    REQUIRE_THAT(g.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(12.4, 0.0001));
    REQUIRE_THAT(g.at("mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(3626.0, 0.0001));
    REQUIRE(g.at("proc_count").to_number<int64_t>() == 26);
    REQUIRE(g.at("thread_count").to_number<int64_t>() == 412);
    REQUIRE(g.at("started_at").to_number<int64_t>() == 1758520000000ll);
    REQUIRE(g.at("account").as_string() == "user");
    REQUIRE(g.at("image_path").as_string() == "C:/Program Files/Naver/Whale/whale.exe");
    REQUIRE(g.at("children").as_array().size() == 1);
}

TEST_CASE("a system account is serialized as the string system", "[serialize]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].account = Account::System;

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("groups").as_array().at(0).as_object().at("account").as_string() == "system");
}

TEST_CASE("a child carries every contract field", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& c =
        o.at("groups").as_array().at(0).as_object().at("children").as_array().at(0).as_object();

    REQUIRE(c.at("pid").to_number<int64_t>() == 8400);
    REQUIRE(c.at("name").as_string() == "whale.exe");
    REQUIRE(c.at("role").as_string() == "child");
    REQUIRE_THAT(c.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(4.1, 0.0001));
    REQUIRE_THAT(c.at("mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(570.5, 0.0001));
    REQUIRE(c.at("threads").to_number<int64_t>() == 18);
}

TEST_CASE("flows carry the estimated source marker", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& f = o.at("flows").as_array().at(0).as_object();

    REQUIRE(f.at("group").as_string() == "whale.exe:22008");
    REQUIRE(f.at("core").to_number<int64_t>() == 3);
    REQUIRE_THAT(f.at("weight").to_number<double>(),
                 Catch::Matchers::WithinAbs(0.42, 0.0001));
    REQUIRE(f.at("source").as_string() == "estimated");
}

TEST_CASE("lifecycle carries spawned entries with their group and terminated pids",
          "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& life = o.at("lifecycle").as_object();
    const json::object& sp = life.at("spawned").as_array().at(0).as_object();

    REQUIRE(sp.at("pid").to_number<int64_t>() == 20114);
    REQUIRE(sp.at("ppid").to_number<int64_t>() == 22008);
    REQUIRE(sp.at("name").as_string() == "whale.exe");
    REQUIRE(sp.at("group").as_string() == "whale.exe:22008");

    REQUIRE(life.at("terminated").as_array().size() == 1);
    REQUIRE(life.at("terminated").as_array().at(0).to_number<int64_t>() == 18002);
}

TEST_CASE("ambient service totals are serialized", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& a = o.at("ambient").as_object();

    REQUIRE(a.at("service_proc_count").to_number<int64_t>() == 84);
    REQUIRE_THAT(a.at("service_mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(1400.0, 0.0001));
}

TEST_CASE("empty collections are serialized as arrays, not null", "[serialize]") {
    SystemSnapshot s;
    s.seq = 1;

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("cores").is_array());
    REQUIRE(o.at("cores").as_array().empty());
    REQUIRE(o.at("groups").as_array().empty());
    REQUIRE(o.at("flows").as_array().empty());
    REQUIRE(o.at("lifecycle").as_object().at("spawned").as_array().empty());
    REQUIRE(o.at("lifecycle").as_object().at("terminated").as_array().empty());
}

TEST_CASE("names with quotes, backslashes and non-ascii survive a round trip",
          "[serialize]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].name = "he said \"hi\"\\x";
    s.groups[0].image_path = "C:\\Program Files\\한글 폴더\\app.exe";

    const json::object o = parseObject(serializeSnapshot(s));
    const json::object& g = o.at("groups").as_array().at(0).as_object();

    REQUIRE(g.at("name").as_string() == "he said \"hi\"\\x");
    REQUIRE(g.at("image_path").as_string() == "C:\\Program Files\\한글 폴더\\app.exe");
}
```

- [ ] **Step 5: 테스트 파일 등록**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_serializer.cpp` 를 추가한다.

- [ ] **Step 6: 빌드해서 실패 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'network/Serializer.h'`.

- [ ] **Step 7: `engine/src/network/Serializer.h` 작성**

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
    // M2 는 항상 추정이다. ETW 수집기가 들어오면 "measured" 가 된다.
    std::string thread_mapping = "estimated";
};

// 계약서 4.3 절 형식.
std::string serializeHello(const HelloInfo& info);

// 계약서 4.4 절 형식. type 과 v 는 SystemSnapshot 에 없고 여기서 붙인다 —
// 구조체는 도메인 값이고 프로토콜 봉투는 전송 계층의 관심사다.
std::string serializeSnapshot(const SystemSnapshot& snapshot);

}  // namespace pulse
```

- [ ] **Step 8: `engine/src/network/Serializer.cpp` 작성**

```cpp
#include "network/Serializer.h"

#include <boost/json.hpp>

namespace pulse {
namespace {

namespace json = boost::json;

// std::optional<double> 은 값이 없으면 JSON null 이 된다.
// 0.0 과 구분되어야 한다 — 계약서 6.1 절.
json::value optionalNumber(const std::optional<double>& value) {
    if (!value.has_value()) {
        return nullptr;
    }
    return *value;
}

json::string accountName(Account account) {
    return account == Account::User ? "user" : "system";
}

json::array serializeChildren(const std::vector<ChildProcess>& children) {
    json::array out;
    out.reserve(children.size());
    for (const ChildProcess& c : children) {
        out.push_back(json::object{
            {"pid", c.pid},
            {"name", c.name},
            {"role", c.role},
            {"cpu_pct", optionalNumber(c.cpu_pct)},
            {"mem_mb", c.mem_mb},
            {"threads", c.threads},
        });
    }
    return out;
}

json::array serializeCores(const std::vector<CoreLoad>& cores) {
    json::array out;
    out.reserve(cores.size());
    for (const CoreLoad& c : cores) {
        out.push_back(json::object{{"id", c.id}, {"pct", c.pct}});
    }
    return out;
}

json::array serializeGroups(const std::vector<ProcessGroup>& groups) {
    json::array out;
    out.reserve(groups.size());
    for (const ProcessGroup& g : groups) {
        out.push_back(json::object{
            {"key", g.key},
            {"name", g.name},
            {"root_pid", g.root_pid},
            {"cpu_pct", optionalNumber(g.cpu_pct)},
            {"mem_mb", g.mem_mb},
            {"proc_count", g.proc_count},
            {"thread_count", g.thread_count},
            {"started_at", g.started_at},
            {"account", accountName(g.account)},
            {"image_path", g.image_path},
            {"children", serializeChildren(g.children)},
        });
    }
    return out;
}

json::array serializeFlows(const std::vector<Flow>& flows) {
    json::array out;
    out.reserve(flows.size());
    for (const Flow& f : flows) {
        out.push_back(json::object{
            {"group", f.group},
            {"core", f.core},
            {"weight", f.weight},
            {"source", f.source},
        });
    }
    return out;
}

json::object serializeLifecycle(const LifecycleDelta& lifecycle) {
    json::array spawned;
    spawned.reserve(lifecycle.spawned.size());
    for (const SpawnedProcess& p : lifecycle.spawned) {
        spawned.push_back(json::object{
            {"pid", p.pid},
            {"ppid", p.ppid},
            {"name", p.name},
            {"group", p.group},
        });
    }

    json::array terminated;
    terminated.reserve(lifecycle.terminated.size());
    for (const uint32_t pid : lifecycle.terminated) {
        terminated.push_back(json::value(pid));
    }

    return json::object{{"spawned", spawned}, {"terminated", terminated}};
}

}  // namespace

std::string serializeHello(const HelloInfo& info) {
    const json::object message{
        {"type", "hello"},
        {"v", kProtocolVersion},
        {"interval_ms", info.interval_ms},
        {"core_count", info.core_count},
        {"capabilities", json::object{{"thread_mapping", info.thread_mapping}}},
        {"host", json::object{{"os", info.os}, {"elevated", info.elevated}}},
    };
    return json::serialize(message);
}

std::string serializeSnapshot(const SystemSnapshot& snapshot) {
    const json::object message{
        {"type", "snapshot"},
        {"v", kProtocolVersion},
        {"seq", snapshot.seq},
        {"t", snapshot.t},
        {"system",
         json::object{
             {"cpu_pct", optionalNumber(snapshot.system.cpu_pct)},
             {"mem_used_mb", snapshot.system.mem_used_mb},
             {"mem_total_mb", snapshot.system.mem_total_mb},
             {"process_total", snapshot.system.process_total},
             {"thread_total", snapshot.system.thread_total},
         }},
        {"cores", serializeCores(snapshot.cores)},
        {"groups", serializeGroups(snapshot.groups)},
        {"flows", serializeFlows(snapshot.flows)},
        {"lifecycle", serializeLifecycle(snapshot.lifecycle)},
        {"ambient",
         json::object{
             {"service_proc_count", snapshot.ambient.service_proc_count},
             {"service_mem_mb", snapshot.ambient.service_mem_mb},
         }},
    };
    return json::serialize(message);
}

}  // namespace pulse
```

- [ ] **Step 9: 테스트 통과 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`. 경고 0.

- [ ] **Step 10: 커밋**

```bash
git add engine/vcpkg.json engine/CMakeLists.txt engine/src/network/Serializer.h engine/src/network/Serializer.cpp engine/tests/test_serializer.cpp engine/tests/CMakeLists.txt
git commit -m "feat(engine): serialize system snapshots to the contract json shape"
```

---

## Task 2: CLI 모드 도입과 `--json`

**Files:**
- Modify: `engine/src/cli/Options.h`
- Modify: `engine/src/cli/Options.cpp`
- Modify: `engine/src/main.cpp`
- Modify: `engine/tests/test_options.cpp`

**Interfaces:**
- Consumes: `pulse::serializeSnapshot` (Task 1)
- Produces:
  - `pulse::Mode` — `enum class { None, Dump, Json }`
  - `pulse::Options` — `{ Mode mode; unsigned interval_ms; unsigned iterations; size_t max_groups; }`
  - `parseOptions` / `usageText` 시그니처는 그대로

`Options` 에서 `bool dump` 가 사라지고 `Mode mode` 로 바뀐다. `--serve` 와 `port` 는 Task 5 에서 추가한다.

- [ ] **Step 1: 실패하는 테스트로 `engine/tests/test_options.cpp` 갱신**

기존 파일의 `REQUIRE(options.dump)` 를 쓰는 테스트들을 `Mode` 기준으로 바꾸고, 모드 배타성 테스트를 추가한다. 아래로 파일 전체를 교체한다.

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "cli/Options.h"

using namespace pulse;

namespace {

ParseResult parse(std::vector<const char*> args, Options& out, std::string& error) {
    args.insert(args.begin(), "pulse-engine.exe");
    return parseOptions(static_cast<int>(args.size()), args.data(), out, error);
}

}  // namespace

TEST_CASE("no arguments shows usage", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({}, options, error) == ParseResult::ShowUsage);
}

TEST_CASE("dump alone uses the documented defaults", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Dump);
    REQUIRE(options.interval_ms == 1000);
    REQUIRE(options.iterations == 0);
    REQUIRE(options.max_groups == 40);
}

TEST_CASE("json selects its own mode", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--json"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Json);
}

TEST_CASE("two modes at once are rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--json"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("a value is read for each flag", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "500", "--iterations", "3", "--max-groups", "12"},
                  options, error) == ParseResult::Ok);
    REQUIRE(options.interval_ms == 500);
    REQUIRE(options.iterations == 3);
    REQUIRE(options.max_groups == 12);
}

TEST_CASE("a negative number is rejected rather than wrapping around", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "-5"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--max-groups", "-1"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "-1"}, options, error) == ParseResult::Error);
}

TEST_CASE("a non numeric value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--iterations", "abc"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "3x"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "+3"}, options, error) == ParseResult::Error);
}

TEST_CASE("a zero interval is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "0"}, options, error) == ParseResult::Error);
}

TEST_CASE("a flag missing its value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("an unknown flag is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--colour"}, options, error) == ParseResult::Error);
}

TEST_CASE("options are left untouched when parsing fails", "[options]") {
    Options options;
    options.interval_ms = 7;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "-5"}, options, error) == ParseResult::Error);
    REQUIRE(options.interval_ms == 7);
}

TEST_CASE("usage mentions every mode", "[options]") {
    const std::string usage = usageText();

    REQUIRE(usage.find("--dump") != std::string::npos);
    REQUIRE(usage.find("--json") != std::string::npos);
}
```

- [ ] **Step 2: 빌드해서 실패 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `'Mode': undeclared identifier` 및 `'struct pulse::Options' has no member named 'mode'`.

- [ ] **Step 3: `engine/src/cli/Options.h` 갱신**

```cpp
#pragma once

#include <cstddef>
#include <string>

namespace pulse {

enum class Mode { None, Dump, Json };

struct Options {
    Mode mode = Mode::None;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
};

enum class ParseResult { Ok, ShowUsage, Error };

// argv 를 파싱한다. Ok 일 때만 out 을 덮어쓴다.
// 실패하면 error 에 사람이 읽을 이유를 담는다.
ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error);

std::string usageText();

}  // namespace pulse
```

- [ ] **Step 4: `engine/src/cli/Options.cpp` 의 `usageText` 와 `parseOptions` 갱신**

`usageText` 전체를 교체한다.

```cpp
std::string usageText() {
    return
        "pulse-engine 0.2.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "  pulse-engine --json [--interval-ms N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --json            Print one snapshot as contract-shaped JSON and exit.\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000, minimum 1).\n"
        "  --iterations N    Stop after N snapshots (default: run until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n";
}
```

`parseOptions` 안의 `--dump` 분기를 다음으로 교체하고, 그 아래에 `--json` 분기를 더한다. 모드 설정을 한 곳으로 모아 배타성을 한 번만 검사한다.

```cpp
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
        } else if (std::strcmp(arg, "--interval-ms") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.interval_ms) ||
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
        } else {
            error = std::string("unknown argument: ") + arg;
            return ParseResult::Error;
        }
    }

    if (parsed.mode == Mode::None) {
        return ParseResult::ShowUsage;
    }

    out = parsed;
    return ParseResult::Ok;
}
```

기존 파일에 이미 `--interval-ms 0` 거부가 있다면 그대로 두고 위 형태와 같은지 확인한다.

- [ ] **Step 5: `engine/src/main.cpp` 에 `--json` 배선**

기존 루프를 모드 분기로 감싼다. `--json` 은 두 번 표본을 뜨고 두 번째를 찍는다 — 첫 스냅샷은 계약서 6.1절대로 CPU 가 전부 `null` 이라 스키마의 숫자 필드를 확인할 수 없다.

```cpp
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "core/DataAggregator.h"
#include "network/Serializer.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

int runDump(pulse::WindowsSystemReader& reader, const pulse::Options& options) {
    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    for (unsigned n = 0; options.iterations == 0 || n < options.iterations; ++n) {
        const pulse::SystemSnapshot snapshot = aggregator.aggregate(reader.read());

        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);

        const bool is_last = options.iterations != 0 && n + 1 == options.iterations;
        if (!is_last) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
    }
    return 0;
}

int runJson(pulse::WindowsSystemReader& reader, const pulse::Options& options) {
    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    // 첫 표본은 CPU 델타가 없어 모든 cpu_pct 가 null 이다. 버린다.
    aggregator.aggregate(reader.read());
    std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));

    const pulse::SystemSnapshot snapshot = aggregator.aggregate(reader.read());
    std::printf("%s\n", pulse::serializeSnapshot(snapshot).c_str());
    return 0;
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

    pulse::WindowsSystemReader reader;

    switch (options.mode) {
        case pulse::Mode::Dump:
            return runDump(reader, options);
        case pulse::Mode::Json:
            return runJson(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
```

- [ ] **Step 6: 테스트 통과 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`, 경고 0.

- [ ] **Step 7: `--json` 을 실제로 확인**

```powershell
.\build\Debug\pulse-engine.exe --json --max-groups 3
```

Expected: 한 줄짜리 JSON. `"type":"snapshot"`, `"v":1`, `"seq":2` 로 시작하고, `"cpu_pct"` 가 `null` 이 아닌 숫자다. `--dump` 도 여전히 동작하는지 확인한다.

```powershell
.\build\Debug\pulse-engine.exe --dump --iterations 1 --max-groups 3
.\build\Debug\pulse-engine.exe --dump --json
```

Expected: 첫 명령은 표 하나, 두 번째는 `only one mode may be given` 오류와 종료 코드 2.

- [ ] **Step 8: 커밋**

```bash
git add engine/src/cli/Options.h engine/src/cli/Options.cpp engine/src/main.cpp engine/tests/test_options.cpp
git commit -m "feat(engine): add --json mode printing one contract-shaped snapshot"
```

---

## Task 3: EngineLoop 추출

`--dump` 와 `--serve` 가 같은 샘플링 루프를 쓰게 만든다. 지금 분리하지 않으면 두 경로가 갈라진다.

**Files:**
- Modify: `engine/src/platform/RawTypes.h`
- Modify: `engine/src/platform/ISystemReader.h`
- Modify: `engine/src/platform/windows/WindowsSystemReader.h`
- Modify: `engine/src/platform/windows/WindowsSystemReader.cpp`
- Create: `engine/src/app/EngineLoop.h`
- Create: `engine/src/app/EngineLoop.cpp`
- Create: `engine/tests/fakes/FakeSystemReader.h`
- Create: `engine/tests/test_engine_loop.cpp`
- Modify: `engine/src/main.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pulse::ISystemReader`, `pulse::DataAggregator`, `pulse::AggregatorConfig`, `pulse::SystemSnapshot`
- Produces:
  - `pulse::HostInfo` — `{ std::string os; bool elevated; }` (in `platform/RawTypes.h`)
  - `pulse::ISystemReader::hostInfo() const -> HostInfo` (순수 가상)
  - `pulse::EngineLoopConfig` — `{ unsigned interval_ms; unsigned iterations; AggregatorConfig aggregator; }`
  - `pulse::EngineLoop::EngineLoop(ISystemReader&, EngineLoopConfig, SnapshotHandler)`
  - `pulse::EngineLoop::SnapshotHandler` — `std::function<void(const SystemSnapshot&)>`
  - `void pulse::EngineLoop::run()`, `void pulse::EngineLoop::stop()`, `const std::string& pulse::EngineLoop::error() const`
  - `pulse::FakeSystemReader`, `pulse::ThrowingSystemReader` (테스트 전용)

- [ ] **Step 1: `engine/src/platform/RawTypes.h` 에 `HostInfo` 추가**

`struct RawSample` 정의 바로 앞에 삽입한다.

```cpp
struct HostInfo {
    std::string os;
    bool elevated = false;
};
```

- [ ] **Step 2: `engine/src/platform/ISystemReader.h` 에 `hostInfo()` 추가**

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
    // hello 메시지의 host 블록을 채우는 데 쓴다 — 계약서 4.3 절.
    virtual HostInfo hostInfo() const = 0;
};

}  // namespace pulse
```

- [ ] **Step 3: 실패하는 테스트 작성 — `engine/tests/fakes/FakeSystemReader.h`**

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "platform/ISystemReader.h"

namespace pulse {

// 정해진 표본을 순서대로 돌려준다. 목록이 끝나면 마지막 것을 반복한다.
class FakeSystemReader final : public ISystemReader {
public:
    FakeSystemReader(std::vector<RawSample> samples, unsigned core_count)
        : samples_(std::move(samples)), core_count_(core_count == 0 ? 1u : core_count) {}

    RawSample read() override {
        ++read_count_;
        if (samples_.empty()) {
            return RawSample{};
        }
        const std::size_t index = std::min(next_++, samples_.size() - 1);
        return samples_[index];
    }

    unsigned coreCount() const override { return core_count_; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

    std::size_t readCount() const { return read_count_; }

private:
    std::vector<RawSample> samples_;
    unsigned core_count_ = 1;
    std::size_t next_ = 0;
    std::size_t read_count_ = 0;
};

// 지정한 횟수만큼 정상 반환한 뒤 던진다.
class ThrowingSystemReader final : public ISystemReader {
public:
    explicit ThrowingSystemReader(std::size_t reads_before_throw)
        : reads_before_throw_(reads_before_throw) {}

    RawSample read() override {
        if (read_count_++ >= reads_before_throw_) {
            throw std::runtime_error("reader exploded");
        }
        return RawSample{};
    }

    unsigned coreCount() const override { return 4; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

private:
    std::size_t reads_before_throw_ = 0;
    std::size_t read_count_ = 0;
};

}  // namespace pulse
```

- [ ] **Step 4: 실패하는 테스트 작성 — `engine/tests/test_engine_loop.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

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
```

- [ ] **Step 5: 테스트 등록 후 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_engine_loop.cpp` 를 추가하고, `target_include_directories` 를 하나 더한다 (`fakes/` 를 `fakes/FakeSystemReader.h` 로 포함하기 위해 테스트 디렉터리가 포함 경로에 있어야 한다).

```cmake
target_include_directories(pulse-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'app/EngineLoop.h'`.

- [ ] **Step 6: `engine/src/app/EngineLoop.h` 작성**

```cpp
#pragma once

#include <atomic>
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
    const std::string& error() const;

private:
    // 정지 요청에 빨리 반응하도록 주기를 잘게 쪼개 잔다.
    void sleepInterval();

    ISystemReader& reader_;
    EngineLoopConfig cfg_;
    SnapshotHandler handler_;
    DataAggregator aggregator_;
    std::atomic<bool> stop_requested_{false};
    std::string error_;
};

}  // namespace pulse
```

- [ ] **Step 7: `engine/src/app/EngineLoop.cpp` 작성**

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
    try {
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
            sleepInterval();
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

void EngineLoop::sleepInterval() {
    unsigned remaining = cfg_.interval_ms;
    while (remaining > 0 && !stop_requested_.load()) {
        const unsigned slice = std::min(remaining, kSleepSliceMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
}

}  // namespace pulse
```

- [ ] **Step 8: `WindowsSystemReader` 에 `hostInfo()` 구현**

`engine/src/platform/windows/WindowsSystemReader.h` 의 `unsigned coreCount() const override;` 아래에 선언을 더한다.

```cpp
    HostInfo hostInfo() const override;
```

`engine/src/platform/windows/WindowsSystemReader.cpp` 의 익명 네임스페이스에 헬퍼를 더한다. `enableDebugPrivilege` 정의 바로 아래가 적당하다.

```cpp
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
```

그리고 `coreCount()` 정의 아래에 멤버 함수를 더한다.

```cpp
HostInfo WindowsSystemReader::hostInfo() const {
    HostInfo info;
    info.os = "Windows";
    info.elevated = isProcessElevated();
    return info;
}
```

- [ ] **Step 9: 소스를 빌드에 등록하고 테스트 통과 확인**

`engine/CMakeLists.txt` 의 `pulse_core` 소스 목록에 `src/app/EngineLoop.cpp` 를 추가한 뒤:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`, 경고 0.

- [ ] **Step 10: `main.cpp` 의 두 모드를 `EngineLoop` 위로 옮긴다**

`runDump` 와 `runJson` 을 교체한다. 나머지(`main` 본문)는 그대로 둔다.

```cpp
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
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }

    std::printf("%s\n", pulse::serializeSnapshot(latest).c_str());
    return 0;
}
```

`main.cpp` 상단의 include 에서 `"core/DataAggregator.h"` 를 `"app/EngineLoop.h"` 로 바꾸고, `<chrono>` 와 `<thread>` 는 더 이상 쓰지 않으므로 제거한다.

- [ ] **Step 11: 동작 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
.\build\Debug\pulse-engine.exe --dump --iterations 2 --max-groups 3
.\build\Debug\pulse-engine.exe --json --max-groups 3
```

Expected: 테스트 통과. `--dump` 는 표 두 개(첫 표의 `CPU%` 는 `-`), `--json` 은 `"seq":2` 인 JSON 한 줄.

- [ ] **Step 12: 커밋**

```bash
git add engine/src/app engine/src/platform engine/src/main.cpp engine/tests/fakes engine/tests/test_engine_loop.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "refactor(engine): extract the sampling loop so dump and serve share it"
```

---

## Task 4: WebSocketServer

**Files:**
- Create: `engine/src/network/ServerConfig.h`
- Create: `engine/src/network/WebSocketServer.h`
- Create: `engine/src/network/WebSocketServer.cpp`
- Create: `engine/tests/test_websocket_server.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: 없음 (`SystemSnapshot` 을 모른다 — 완성된 문자열만 받는다)
- Produces:
  - `pulse::ServerConfig` — `{ unsigned short port; std::vector<std::string> allowed_origins; }`
  - `pulse::WebSocketServer::WebSocketServer(boost::asio::io_context&, ServerConfig)`
  - `unsigned short pulse::WebSocketServer::port() const`
  - `void pulse::WebSocketServer::setHello(std::shared_ptr<const std::string>)`
  - `void pulse::WebSocketServer::broadcast(std::shared_ptr<const std::string>)`
  - `void pulse::WebSocketServer::stop()`

`setHello` 와 `broadcast` 와 `stop` 은 **어느 스레드에서 불러도 안전하다.** 내부에서 `io_context` 로 post 한다. 그 외의 모든 상태는 io_context 스레드에서만 만진다.

- [ ] **Step 1: `engine/src/network/ServerConfig.h` 작성**

```cpp
#pragma once

#include <string>
#include <vector>

namespace pulse {

struct ServerConfig {
    // 0 이면 OS 가 임시 포트를 고른다. 테스트에서 쓴다.
    unsigned short port = 9000;

    // 브라우저가 보내는 Origin 헤더의 허용 목록.
    // Origin 이 아예 없는 연결(네이티브 클라이언트, 테스트)은 허용한다 —
    // Origin 검사는 브라우저발 교차 출처 접근을 막는 장치이고,
    // 네이티브 접근은 127.0.0.1 바인딩으로 이미 통제된다.
    std::vector<std::string> allowed_origins = {
        "http://localhost:5173",
        "http://127.0.0.1:5173",
    };
};

}  // namespace pulse
```

- [ ] **Step 2: 실패하는 테스트 작성 — `engine/tests/test_websocket_server.cpp`**

임시 포트(0)에 바인딩해 같은 프로세스에서 Beast 클라이언트로 접속한다.

```cpp
#include <catch2/catch_test_macros.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "network/WebSocketServer.h"

using namespace pulse;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = net::ip::tcp;

namespace {

std::shared_ptr<const std::string> msg(std::string text) {
    return std::make_shared<const std::string>(std::move(text));
}

// 서버를 자기 스레드에서 돌리고 소멸 시 정리한다.
class ServerFixture {
public:
    explicit ServerFixture(ServerConfig cfg = ServerConfig{}) {
        cfg.port = 0;  // 임시 포트
        server_ = std::make_unique<WebSocketServer>(ioc_, cfg);
        port_ = server_->port();
        thread_ = std::thread([this] { ioc_.run(); });
    }

    ~ServerFixture() {
        server_->stop();
        thread_.join();
    }

    WebSocketServer& server() { return *server_; }
    unsigned short port() const { return port_; }

private:
    net::io_context ioc_;
    std::unique_ptr<WebSocketServer> server_;
    unsigned short port_ = 0;
    std::thread thread_;
};

// 접속해서 메시지를 읽는 동기 클라이언트.
class TestClient {
public:
    TestClient(unsigned short port, const std::string& origin = {}) : ws_(ioc_) {
        tcp::resolver resolver(ioc_);
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
        net::connect(ws_.next_layer(), endpoints);

        if (!origin.empty()) {
            ws_.set_option(websocket::stream_base::decorator(
                [origin](websocket::request_type& req) {
                    req.set(boost::beast::http::field::origin, origin);
                }));
        }
        ws_.handshake("127.0.0.1", "/");
    }

    std::string read() {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

    void close() { ws_.close(websocket::close_code::normal); }

private:
    net::io_context ioc_;
    websocket::stream<tcp::socket> ws_;
};

}  // namespace

TEST_CASE("the server binds an ephemeral port on request", "[ws]") {
    ServerFixture fixture;

    REQUIRE(fixture.port() != 0);
}

TEST_CASE("a new client receives the hello message first", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());

    REQUIRE(client.read() == R"({"type":"hello"})");
    client.close();
}

TEST_CASE("a new client receives the latest snapshot right after hello", "[ws]") {
    // 접속 시점이 샘플링 주기와 무관하므로, 보관본이 없으면 화면이
    // 최대 한 주기 동안 비어 있다 — 스펙 6.3 절.
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));
    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":7})"));

    TestClient client(fixture.port());

    REQUIRE(client.read() == R"({"type":"hello"})");
    REQUIRE(client.read() == R"({"type":"snapshot","seq":7})");
    client.close();
}

TEST_CASE("a broadcast reaches a connected client", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());
    REQUIRE(client.read() == R"({"type":"hello"})");

    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":1})"));

    REQUIRE(client.read() == R"({"type":"snapshot","seq":1})");
    client.close();
}

TEST_CASE("a broadcast reaches every connected client", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient a(fixture.port());
    TestClient b(fixture.port());
    REQUIRE(a.read() == R"({"type":"hello"})");
    REQUIRE(b.read() == R"({"type":"hello"})");

    fixture.server().broadcast(msg(R"({"type":"snapshot","seq":1})"));

    REQUIRE(a.read() == R"({"type":"snapshot","seq":1})");
    REQUIRE(b.read() == R"({"type":"snapshot","seq":1})");
    a.close();
    b.close();
}

TEST_CASE("an allowed origin is accepted", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port(), "http://localhost:5173");

    REQUIRE(client.read() == R"({"type":"hello"})");
    client.close();
}

TEST_CASE("a disallowed origin is rejected", "[ws]") {
    ServerFixture fixture;

    REQUIRE_THROWS([&] { TestClient client(fixture.port(), "http://evil.example"); }());
}

TEST_CASE("the server survives a client disconnecting", "[ws]") {
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    {
        TestClient first(fixture.port());
        REQUIRE(first.read() == R"({"type":"hello"})");
        first.close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TestClient second(fixture.port());
    REQUIRE(second.read() == R"({"type":"hello"})");
    second.close();
}

TEST_CASE("a client that stops reading does not grow an unbounded queue", "[ws]") {
    // 세션당 대기 스냅샷은 1개. 밀린 것은 새 것으로 덮어쓴다 — 스펙 7 절.
    ServerFixture fixture;
    fixture.server().setHello(msg(R"({"type":"hello"})"));

    TestClient client(fixture.port());
    REQUIRE(client.read() == R"({"type":"hello"})");

    for (int i = 1; i <= 50; ++i) {
        fixture.server().broadcast(msg(R"({"type":"snapshot","seq":)" + std::to_string(i) + "}"));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 큐가 자라지 않았다면 밀린 50개가 전부 오지는 않는다.
    // 적어도 하나는 오고, 마지막 값이 결국 도달한다.
    bool saw_last = false;
    for (int reads = 0; reads < 5 && !saw_last; ++reads) {
        if (client.read() == R"({"type":"snapshot","seq":50})") {
            saw_last = true;
        }
    }
    REQUIRE(saw_last);
    client.close();
}
```

- [ ] **Step 3: 테스트 등록 후 실패 확인**

`engine/tests/CMakeLists.txt` 의 소스 목록에 `test_websocket_server.cpp` 를 추가한 뒤:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `Cannot open include file: 'network/WebSocketServer.h'`.

- [ ] **Step 4: `engine/src/network/WebSocketServer.h` 작성**

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "network/ServerConfig.h"

namespace pulse {

// 127.0.0.1 전용 WebSocket 브로드캐스트 서버.
// SystemSnapshot 을 모른다 — 완성된 문자열만 받아 연결된 세션들에 보낸다.
//
// setHello / broadcast / stop 은 어느 스레드에서 불러도 안전하다.
// 내부에서 io_context 로 post 하므로, 세션 목록은 io_context 스레드에서만
// 만져진다. 그래서 뮤텍스가 없다.
class WebSocketServer {
public:
    WebSocketServer(boost::asio::io_context& ioc, ServerConfig cfg);
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // 실제로 바인딩된 포트. cfg.port 가 0 이었다면 OS 가 고른 값이다.
    unsigned short port() const;

    // 새로 접속한 세션이 가장 먼저 받을 메시지.
    void setHello(std::shared_ptr<const std::string> hello);

    // 연결된 모든 세션에 보낸다. 보관되어 새 세션에도 hello 직후 전달된다.
    void broadcast(std::shared_ptr<const std::string> message);

    // 수락을 멈추고 세션을 모두 닫는다. io_context.run() 이 돌아오게 된다.
    void stop();

private:
    class Session;

    void doAccept();
    void onAccept(boost::system::error_code ec, boost::asio::ip::tcp::socket socket);
    void removeSession(const std::shared_ptr<Session>& session);

    boost::asio::io_context& ioc_;
    ServerConfig cfg_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<Session>> sessions_;
    std::shared_ptr<const std::string> hello_;
    std::shared_ptr<const std::string> latest_;
    bool stopped_ = false;
};

}  // namespace pulse
```

- [ ] **Step 5: `engine/src/network/WebSocketServer.cpp` 작성**

```cpp
#include "network/WebSocketServer.h"

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <utility>

namespace pulse {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = net::ip::tcp;

}  // namespace

// ---------------------------------------------------------------- Session

// 하나의 WebSocket 연결. 쓰기는 한 번에 하나만 진행할 수 있으므로,
// 진행 중에 새 메시지가 오면 대기 슬롯 하나에 덮어쓴다.
class WebSocketServer::Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, WebSocketServer& server)
        : ws_(std::move(socket)), server_(server) {}

    void run(std::shared_ptr<const std::string> hello,
             std::shared_ptr<const std::string> latest) {
        pending_ = std::move(latest);
        first_ = std::move(hello);

        // 업그레이드 요청을 직접 읽어 Origin 을 검사한 뒤 수락한다.
        // Beast 의 async_accept 에는 거절 훅이 없다.
        http::async_read(ws_.next_layer(), buffer_, request_,
                         [self = shared_from_this()](beast::error_code ec, std::size_t) {
                             self->onRequest(ec);
                         });
    }

    void send(std::shared_ptr<const std::string> message) {
        if (!open_) {
            return;
        }
        if (writing_) {
            // 큐를 키우지 않는다. 밀린 스냅샷은 보낼 가치가 없다.
            pending_ = std::move(message);
            return;
        }
        sending_ = std::move(message);
        doWrite();
    }

    void close() {
        open_ = false;
        beast::error_code ignored;
        ws_.next_layer().close(ignored);
    }

private:
    void onRequest(beast::error_code ec) {
        if (ec) {
            server_.removeSession(shared_from_this());
            return;
        }

        if (!originAllowed()) {
            auto response = std::make_shared<http::response<http::string_body>>(
                http::status::forbidden, request_.version());
            response->set(http::field::content_type, "text/plain");
            response->body() = "origin not allowed";
            response->prepare_payload();
            http::async_write(ws_.next_layer(), *response,
                              [self = shared_from_this(), response](beast::error_code,
                                                                    std::size_t) {
                                  self->server_.removeSession(self);
                              });
            return;
        }

        ws_.async_accept(request_, [self = shared_from_this()](beast::error_code accept_ec) {
            self->onAccept(accept_ec);
        });
    }

    bool originAllowed() const {
        const auto it = request_.find(http::field::origin);
        if (it == request_.end()) {
            // Origin 이 없는 연결은 브라우저발이 아니다. 127.0.0.1 바인딩이 통제한다.
            return true;
        }
        const std::string origin(it->value());
        const auto& allowed = server_.cfg_.allowed_origins;
        return std::find(allowed.begin(), allowed.end(), origin) != allowed.end();
    }

    void onAccept(beast::error_code ec) {
        if (ec) {
            server_.removeSession(shared_from_this());
            return;
        }
        open_ = true;
        ws_.text(true);
        doRead();

        if (first_) {
            sending_ = std::move(first_);
            doWrite();
        } else if (pending_) {
            sending_ = std::move(pending_);
            doWrite();
        }
    }

    // 계약은 단방향이다. 읽기는 close 프레임과 연결 종료를 감지하기 위해서만 한다.
    void doRead() {
        read_buffer_.clear();
        ws_.async_read(read_buffer_,
                       [self = shared_from_this()](beast::error_code ec, std::size_t) {
                           if (ec) {
                               self->open_ = false;
                               self->server_.removeSession(self);
                               return;
                           }
                           self->doRead();
                       });
    }

    void doWrite() {
        writing_ = true;
        ws_.async_write(net::buffer(*sending_),
                        [self = shared_from_this()](beast::error_code ec, std::size_t) {
                            self->onWrite(ec);
                        });
    }

    void onWrite(beast::error_code ec) {
        writing_ = false;
        sending_.reset();

        if (ec) {
            open_ = false;
            server_.removeSession(shared_from_this());
            return;
        }
        if (pending_) {
            sending_ = std::move(pending_);
            doWrite();
        }
    }

    websocket::stream<tcp::socket> ws_;
    WebSocketServer& server_;
    beast::flat_buffer buffer_;
    beast::flat_buffer read_buffer_;
    http::request<http::string_body> request_;
    std::shared_ptr<const std::string> first_;
    std::shared_ptr<const std::string> sending_;
    std::shared_ptr<const std::string> pending_;
    bool writing_ = false;
    bool open_ = false;
};

// ---------------------------------------------------------- WebSocketServer

WebSocketServer::WebSocketServer(net::io_context& ioc, ServerConfig cfg)
    : ioc_(ioc), cfg_(std::move(cfg)), acceptor_(ioc) {
    // 루프백 전용. 0.0.0.0 바인딩 경로를 두지 않는다 — 계약서 4.2 절.
    const tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), cfg_.port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);

    doAccept();
}

WebSocketServer::~WebSocketServer() = default;

unsigned short WebSocketServer::port() const {
    return acceptor_.local_endpoint().port();
}

void WebSocketServer::setHello(std::shared_ptr<const std::string> hello) {
    net::post(ioc_, [this, hello = std::move(hello)]() mutable {
        hello_ = std::move(hello);
    });
}

void WebSocketServer::broadcast(std::shared_ptr<const std::string> message) {
    net::post(ioc_, [this, message = std::move(message)]() mutable {
        latest_ = message;
        for (const auto& session : sessions_) {
            session->send(message);
        }
    });
}

void WebSocketServer::stop() {
    net::post(ioc_, [this] {
        if (stopped_) {
            return;
        }
        stopped_ = true;

        beast::error_code ignored;
        acceptor_.close(ignored);
        for (const auto& session : sessions_) {
            session->close();
        }
        sessions_.clear();
    });
}

void WebSocketServer::doAccept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        onAccept(ec, std::move(socket));
    });
}

void WebSocketServer::onAccept(boost::system::error_code ec, tcp::socket socket) {
    if (stopped_) {
        return;
    }
    if (!ec) {
        auto session = std::make_shared<Session>(std::move(socket), *this);
        sessions_.push_back(session);
        session->run(hello_, latest_);
    }
    doAccept();
}

void WebSocketServer::removeSession(const std::shared_ptr<Session>& session) {
    const auto it = std::find(sessions_.begin(), sessions_.end(), session);
    if (it != sessions_.end()) {
        sessions_.erase(it);
    }
}

}  // namespace pulse
```

- [ ] **Step 6: 빌드에 등록**

`engine/CMakeLists.txt` 의 `pulse_core` 소스 목록에 `src/network/WebSocketServer.cpp` 를 추가하고, Boost 컴포넌트에 beast 를 더한다. `find_package` 줄을 교체한다.

```cmake
find_package(Boost REQUIRED COMPONENTS json beast)
```

Beast 는 헤더 온리라 vcpkg 의 Boost 버전에 따라 `COMPONENTS beast` 를 인식하지 못할 수 있다. `Could NOT find Boost (missing: beast)` 가 나오면 다음으로 바꾼다 — 같은 `Boost::beast` 타깃을 얻는 다른 경로다.

```cmake
find_package(Boost REQUIRED COMPONENTS json)
find_package(boost_beast CONFIG REQUIRED)
```

둘 다 실패하면 코드를 고치지 말고 오류 전문과 함께 BLOCKED 로 보고한다. `Boost::beast` 는 `Boost::asio` 를 전이적으로 끌어오므로 asio 를 따로 링크할 필요는 없다.

그리고 링크 줄을 교체한다.

```cmake
target_link_libraries(pulse_core PUBLIC Boost::json Boost::beast)
if(WIN32)
  target_link_libraries(pulse_core PUBLIC pdh psapi ws2_32 mswsock)
endif()
```

- [ ] **Step 7: 테스트 통과 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`, 경고 0. WebSocket 테스트가 실제 소켓을 쓰므로 실행 시간이 1초 남짓 늘어난다.

- [ ] **Step 8: 커밋**

```bash
git add engine/src/network/ServerConfig.h engine/src/network/WebSocketServer.h engine/src/network/WebSocketServer.cpp engine/tests/test_websocket_server.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): add a loopback websocket broadcast server"
```

---

## Task 5: `--serve` 배선

M2 의 산출물. 여기가 끝나면 브라우저 콘솔에서 1초마다 스냅샷이 흐른다.

**Files:**
- Modify: `engine/src/cli/Options.h`
- Modify: `engine/src/cli/Options.cpp`
- Modify: `engine/src/main.cpp`
- Modify: `engine/tests/test_options.cpp`

**Interfaces:**
- Consumes: `pulse::EngineLoop` (Task 3), `pulse::WebSocketServer` (Task 4), `pulse::serializeHello` / `pulse::serializeSnapshot` (Task 1)
- Produces: `pulse::Mode::Serve`, `pulse::Options::port`

- [ ] **Step 1: 실패하는 테스트를 `engine/tests/test_options.cpp` 에 추가**

기존 테스트는 그대로 두고 아래를 덧붙인다.

```cpp
TEST_CASE("serve selects its own mode with a default port", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.port == 9000);
}

TEST_CASE("a port can be given", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--port", "9100"}, options, error) == ParseResult::Ok);
    REQUIRE(options.port == 9100);
}

TEST_CASE("a port outside the valid range is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--port", "0"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--serve", "--port", "70000"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--serve", "--port", "-1"}, options, error) == ParseResult::Error);
}

TEST_CASE("serve cannot be combined with another mode", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--dump"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--json", "--serve"}, options, error) == ParseResult::Error);
}
```

그리고 기존의 `usage mentions every mode` 테스트에 한 줄 더한다.

```cpp
    REQUIRE(usage.find("--serve") != std::string::npos);
```

- [ ] **Step 2: 빌드해서 실패 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default
```

Expected: FAIL. `'Serve': is not a member of 'pulse::Mode'` 및 `'port': is not a member of 'pulse::Options'`.

- [ ] **Step 3: `engine/src/cli/Options.h` 갱신**

```cpp
enum class Mode { None, Dump, Json, Serve };

struct Options {
    Mode mode = Mode::None;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
    unsigned port = 9000;
};
```

- [ ] **Step 4: `engine/src/cli/Options.cpp` 갱신**

`usageText` 를 교체한다.

```cpp
std::string usageText() {
    return
        "pulse-engine 0.2.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump  [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "  pulse-engine --json  [--interval-ms N] [--max-groups N]\n"
        "  pulse-engine --serve [--port N] [--interval-ms N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --json            Print one snapshot as contract-shaped JSON and exit.\n"
        "  --serve           Stream snapshots over WebSocket on 127.0.0.1.\n"
        "  --port N          Listen port for --serve (default 9000).\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000, minimum 1).\n"
        "  --iterations N    Stop after N snapshots (default: run until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n";
}
```

`parseOptions` 의 `--json` 분기 바로 아래에 두 분기를 더한다.

```cpp
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
```

- [ ] **Step 5: `engine/src/main.cpp` 에 `runServe` 추가**

include 를 더한다.

```cpp
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <memory>
#include <thread>

#include "network/Serializer.h"
#include "network/WebSocketServer.h"
```

`runJson` 아래에 함수를 더한다.

```cpp
int runServe(pulse::ISystemReader& reader, const pulse::Options& options) {
    namespace net = boost::asio;

    net::io_context ioc;

    pulse::ServerConfig server_cfg;
    server_cfg.port = static_cast<unsigned short>(options.port);

    std::unique_ptr<pulse::WebSocketServer> server;
    try {
        server = std::make_unique<pulse::WebSocketServer>(ioc, server_cfg);
    } catch (const std::exception& e) {
        std::printf("cannot listen on 127.0.0.1:%u — %s\n", options.port, e.what());
        return 2;
    }

    pulse::HelloInfo hello;
    hello.interval_ms = options.interval_ms;
    hello.core_count = reader.coreCount();
    const pulse::HostInfo host = reader.hostInfo();
    hello.os = host.os;
    hello.elevated = host.elevated;
    server->setHello(
        std::make_shared<const std::string>(pulse::serializeHello(hello)));

    pulse::EngineLoopConfig loop_cfg;
    loop_cfg.interval_ms = options.interval_ms;
    loop_cfg.iterations = options.iterations;
    loop_cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::EngineLoop loop(reader, loop_cfg, [&](const pulse::SystemSnapshot& snapshot) {
        server->broadcast(
            std::make_shared<const std::string>(pulse::serializeSnapshot(snapshot)));
    });

    // Ctrl+C 로 종료한다. 신호는 io_context 에서 받고 샘플링 루프에 정지를 알린다.
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        loop.stop();
        server->stop();
    });

    std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                static_cast<unsigned>(server->port()));
    std::fflush(stdout);

    std::thread sampler([&] {
        loop.run();
        // 반복 횟수를 채웠거나 예외로 끝났으면 서버도 접는다.
        server->stop();
        ioc.stop();
    });

    ioc.run();
    loop.stop();
    sampler.join();

    if (!loop.error().empty()) {
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }
    return 0;
}
```

`main` 의 모드 분기에 한 줄 더한다.

```cpp
        case pulse::Mode::Serve:
            return runServe(reader, options);
```

- [ ] **Step 6: 테스트 통과 확인**

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"; $env:VCPKG_ROOT = "C:\vcpkg"; cd C:\dev\pulse-uni\engine; cmake --build --preset default; ctest --preset default
```

Expected: `100% tests passed`, 경고 0.

- [ ] **Step 7: 실제로 서빙되는지 확인 — 종료되는 실행**

`--iterations` 로 스스로 끝나게 해서 Ctrl+C 없이 확인한다.

```powershell
.\build\Debug\pulse-engine.exe --serve --iterations 3 --interval-ms 500 --max-groups 3
```

Expected: `pulse-engine listening on ws://127.0.0.1:9000` 출력 후 약 1.5초 뒤 종료 코드 0.

- [ ] **Step 8: 실제 클라이언트로 확인**

한 셸에서 서버를 띄운다.

```powershell
.\build\Debug\pulse-engine.exe --serve --interval-ms 1000 --max-groups 5
```

다른 셸에서 Node 로 접속한다 (이 PC 에 Node v24 가 있다).

```powershell
node -e "const ws=new WebSocket('ws://127.0.0.1:9000');let n=0;ws.onmessage=e=>{const m=JSON.parse(e.data);console.log(m.type, m.seq ?? '', (e.data.length/1024).toFixed(1)+' KB');if(++n>=4)process.exit(0)};ws.onerror=e=>{console.error('error',e.message);process.exit(1)}"
```

Expected:

```
hello
snapshot 1 <크기> KB
snapshot 2 <크기> KB
snapshot 3 <크기> KB
```

첫 줄이 `hello` 여야 하고, `seq` 가 1 씩 증가해야 하며, 크기가 스펙 2절의 측정치(그룹 수에 비례, 40개 기준 약 20 KB)와 같은 자릿수여야 한다. 서버 셸에서 Ctrl+C 로 종료하고 종료 코드가 0 인지 확인한다.

- [ ] **Step 9: 포트 충돌 동작 확인**

한 셸에서 서버를 띄운 채, 다른 셸에서 같은 포트로 또 띄운다.

```powershell
.\build\Debug\pulse-engine.exe --serve
```

Expected: `cannot listen on 127.0.0.1:9000 — ...` 메시지와 종료 코드 2.

- [ ] **Step 10: 다른 모드가 여전히 동작하는지 확인**

```powershell
.\build\Debug\pulse-engine.exe --dump --iterations 2 --max-groups 3
.\build\Debug\pulse-engine.exe --json --max-groups 3
.\build\Debug\pulse-engine.exe --serve --dump
```

Expected: 앞의 둘은 M1/Task 2 와 동일하게 동작하고, 셋째는 `only one mode may be given` 오류와 종료 코드 2.

- [ ] **Step 11: 커밋**

```bash
git add engine/src/cli/Options.h engine/src/cli/Options.cpp engine/src/main.cpp engine/tests/test_options.cpp
git commit -m "feat(engine): stream snapshots over websocket with --serve"
```

---

## M2 완료 조건

- [ ] `ctest --preset default` 가 전부 통과하고 경고가 없다.
- [ ] `--json` 이 계약서 4.4절 형식의 JSON 을 찍고, `cpu_pct` 가 `null` 이 아닌 숫자다.
- [ ] `--serve` 로 띄운 서버에 접속하면 `hello` 가 먼저 오고 이어서 `snapshot` 이 1초마다 온다.
- [ ] 접속 즉시 보관된 최신 스냅샷이 전달되어, 첫 화면이 한 주기 동안 비어 있지 않다.
- [ ] 허용되지 않은 Origin 이 거절된다.
- [ ] 포트가 사용 중이면 어떤 포트인지 밝히고 종료 코드 2 로 끝난다.
- [ ] `--dump` 가 M1 과 동일하게 동작한다.
- [ ] `src/core/` 의 어떤 파일도 `<windows.h>` 를 포함하지 않는다.

확인 명령:

```powershell
cd C:\dev\pulse-uni\engine; Select-String -Path src\core\*.h,src\core\*.cpp -Pattern 'windows.h'
```

Expected: 출력 없음.

## 다음 단계

M3 은 React + TypeScript 프론트엔드의 데이터 레이어와 검증용 숫자 대시보드를 만든다. 계약서 7절이 그 설계이고, 스펙 D9 이 미뤄둔 정적 파일 서빙도 그때 함께 붙인다. `--dump` 와 `--json` 은 그대로 남긴다 — 화면이 이상할 때 데이터를 먼저 의심할 수 있게 해주는 도구다.
