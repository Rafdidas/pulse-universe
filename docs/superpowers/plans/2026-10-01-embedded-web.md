# 웹 내장 exe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `web/dist` 를 `web.pak` 으로 묶어 `pulse-engine.exe` 에 내장하고, 인자 없는 실행이 exe 옆 `web/` 이 없으면 내장본을 서빙한다. 릴리스에는 exe 단독 파일과 기존 zip 을 함께 올린다.

**Architecture:** `scripts/make-pak.ps1` 이 pak 을 만들고, CMake 옵션 `PULSE_WEB_PAK` 이 그것을 리소스(RCDATA `WEBPAK`)로 `pulse-engine` 에만 링크한다. 엔진은 `AssetPack` 으로 pak 을 읽고(`EmbeddedAssets` 가 리소스를 가져온다), `ServerConfig::assets` 가 설정되면 `WebSocketServer` 가 폴더 대신 pak 에서 응답한다. 서빙 우선순위는 `--web-root` → exe 옆 `web/` → 내장본 → 사용법 출력이다.

**Tech Stack:** C++20 (MSVC `/W4`, 경고 0), CMake, Boost.Beast, Catch2, PowerShell 5.1, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-30-embedded-web-design.md` (시제품 결과는 그 11절).

## Global Constraints

- 빌드 환경: `export PATH="/c/Program Files/CMake/bin:$PATH" VCPKG_ROOT="C:/vcpkg"`. 개발 빌드 `cd engine && cmake --build --preset default`, 테스트 `engine/build/tests/Debug/pulse-tests.exe`. 컴파일 경고는 0 이어야 한다.
- 개발 빌드(`PULSE_WEB_PAK` 없음)는 프런트엔드 없이 빌드되고 리소스를 넣지 않는다 (D78).
- `AssetPack::parse` 는 magic·개수·범위(`offset + size` 오버플로 없이)·경로 엄격 오름차순(중복 없음)을 모두 검증하고, 하나라도 어긋나면 `nullopt` 다.
- 서빙 우선순위는 `--web-root DIR` → (인자 없는 실행) exe 옆 `web/` → 내장본 → 사용법 출력 (D77). 명시적인 `--serve` 에 `--web-root`·`--embedded-web` 이 모두 없으면 기존처럼 WebSocket 전용이다.
- `--embedded-web` 은 `--serve` 와만 쓰고 `--web-root` 와 같이 쓰면 오류다. 내장본이 없는 빌드에서 쓰면 `no embedded web assets in this build` 를 stderr 에 내고 종료 코드 2.
- pak 모드의 요청 경로: 쿼리·프래그먼트를 떼고, 선행 `/` 가 없거나 역슬래시가 있거나 `..` 세그먼트가 있으면 403. `:` 를 포함하면 없는 파일처럼 `/index.html` 로 폴백. `/` 는 `/index.html`. 없는 경로는 `/index.html` 로 폴백(SPA), index 도 없으면 404. `index.html` 응답에만 `Cache-Control: no-cache`, 모든 응답에 `X-Content-Type-Options: nosniff` (기존 `sendSimple`).
- 릴리스 Assets 는 4 개: `pulse-engine.exe`, `pulse-engine.exe.sha256`, `pulse-universe-vX.Y.Z-win-x64.zip`, `pulse-universe-vX.Y.Z-win-x64.zip.sha256`. zip 안에는 `web/` 폴더가 없다.
- `.ps1`, `.bat`, `README.txt` 는 ASCII 만 쓴다 (PowerShell 5.1 이 BOM 없는 UTF-8 을 ANSI 로 읽는다). 기존 `.ps1` 줄바꿈 관례를 따른다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다. 브랜치는 `feature/embedded-web`. `main` 병합·푸시·태그는 하지 않는다.
- 코드를 바꿀 때는 `C:\dev\pulse-uni` 의 기존 파일을 읽고 그 주석 밀도·이름·관례를 따른다. 파일에 코드를 쓸 때 백슬래시 이스케이프(`\n`, `\b`, `\0`, `C:\...`)가 제어 문자로 바뀌지 않았는지 다시 읽어 확인한다.

---

### Task 1: pak 포맷, 읽기, 내장 (시제품 그대로)

**Files:**
- Create: `scripts/make-pak.ps1`
- Create: `engine/src/network/AssetPack.h`, `engine/src/network/AssetPack.cpp`
- Create: `engine/src/platform/windows/EmbeddedAssets.h`, `engine/src/platform/windows/EmbeddedAssets.cpp`
- Create: `engine/resources/web.rc.in`
- Create: `engine/tests/pak_builder.h`, `engine/tests/test_asset_pack.cpp`
- Modify: `engine/CMakeLists.txt`, `engine/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `pulse::AssetPack::parse(std::string_view) -> std::optional<AssetPack>`, `AssetPack::find(std::string_view) const -> std::optional<std::string_view>`, `AssetPack::size() const`; `const pulse::AssetPack* pulse::embeddedAssetPack()`; `pulse_test::makePak(const std::vector<std::pair<std::string,std::string>>&) -> std::string` (tests/pak_builder.h); CMake cache var `PULSE_WEB_PAK`; `scripts/make-pak.ps1 -WebDist <dir> -Out <file>`.

- [ ] **Step 1: 파일을 그대로 만든다**

아래 각 블록을 그 경로에 **글자 그대로** 쓴다 (시제품과 바이트 단위로 같아야 한다).

`scripts/make-pak.ps1`:

```powershell
<#
.SYNOPSIS
  Packs a built web folder (web/dist) into one web.pak file that the engine embeds.
.PARAMETER WebDist
  The folder to pack (every file under it, recursively).
.PARAMETER Out
  The pak file to write.
#>
param(
    [Parameter(Mandatory = $true)][string]$WebDist,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $WebDist).Path.TrimEnd('\')
$byPath = @{}
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
    $byPath['/' + $relative] = $file.FullName
}
if ($byPath.Count -eq 0) { throw "no files under $root" }

# Ascending ordinal order: the engine binary-searches the table and rejects anything else.
[string[]]$paths = @($byPath.Keys)
[Array]::Sort($paths, [System.StringComparer]::Ordinal)

$utf8 = New-Object System.Text.UTF8Encoding($false)
$pathBytes = @{}
$tableSize = 0
foreach ($path in $paths) {
    $bytes = $utf8.GetBytes($path)
    if ($bytes.Length -gt 65535) { throw "path too long: $path" }
    $pathBytes[$path] = $bytes
    $tableSize += 2 + $bytes.Length + 8 + 8
}

$stream = [System.IO.File]::Create($Out)
try {
    $writer = New-Object System.IO.BinaryWriter($stream)
    $writer.Write([byte[]][System.Text.Encoding]::ASCII.GetBytes("PLSPAK1`0"))
    $writer.Write([uint32]$paths.Count)

    $offset = [uint64](8 + 4 + $tableSize)
    foreach ($path in $paths) {
        $size = [uint64](New-Object System.IO.FileInfo($byPath[$path])).Length
        $writer.Write([uint16]$pathBytes[$path].Length)
        $writer.Write([byte[]]$pathBytes[$path])
        $writer.Write([uint64]$offset)
        $writer.Write([uint64]$size)
        $offset += $size
    }
    foreach ($path in $paths) {
        $writer.Write([byte[]][System.IO.File]::ReadAllBytes($byPath[$path]))
    }
    $writer.Flush()
} finally {
    $stream.Dispose()
}
Write-Host "==> $Out ($($paths.Count) files, $((Get-Item -LiteralPath $Out).Length) bytes)"
```

`engine/src/network/AssetPack.h`:

```cpp
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace pulse {

// web.pak 를 읽는다. 포맷은 docs/superpowers/specs/2026-09-30-embedded-web-design.md 3절.
// 바이트를 복사하지 않는다 — parse 에 넘긴 버퍼가 AssetPack 보다 오래 살아야 한다.
class AssetPack {
public:
    // magic·개수·범위·경로 정렬(오름차순, 중복 없음)을 모두 검증한다. 하나라도 어긋나면
    // nullopt — 잘린 pak 이 일부만 서빙되는 일이 없다.
    static std::optional<AssetPack> parse(std::string_view bytes);

    // "/index.html" 처럼 선행 '/' 가 있는 경로로 찾는다. 없으면 nullopt.
    std::optional<std::string_view> find(std::string_view path) const;

    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string_view path;
        std::string_view data;
    };
    std::vector<Entry> entries_;
};

}  // namespace pulse
```

`engine/src/network/AssetPack.cpp`:

```cpp
#include "network/AssetPack.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace pulse {
namespace {

constexpr std::string_view kMagic("PLSPAK1\0", 8);

// 리틀 엔디언 정수를 범위 검사와 함께 읽는다. 범위를 넘으면 false.
template <typename T>
bool readLe(std::string_view bytes, std::size_t& cursor, T& out) {
    if (bytes.size() - cursor < sizeof(T) || cursor > bytes.size()) {
        return false;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<unsigned char>(bytes[cursor + i])) << (8 * i);
    }
    cursor += sizeof(T);
    out = value;
    return true;
}

}  // namespace

std::optional<AssetPack> AssetPack::parse(std::string_view bytes) {
    if (bytes.size() < kMagic.size() + 4 || bytes.substr(0, kMagic.size()) != kMagic) {
        return std::nullopt;
    }
    std::size_t cursor = kMagic.size();
    std::uint32_t count = 0;
    if (!readLe(bytes, cursor, count)) {
        return std::nullopt;
    }

    AssetPack pack;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t path_len = 0;
        if (!readLe(bytes, cursor, path_len) || bytes.size() - cursor < path_len) {
            return std::nullopt;
        }
        const std::string_view path = bytes.substr(cursor, path_len);
        cursor += path_len;

        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        if (!readLe(bytes, cursor, offset) || !readLe(bytes, cursor, size)) {
            return std::nullopt;
        }
        // offset + size 가 넘치지 않게 뺄셈으로 비교한다.
        if (offset > bytes.size() || size > bytes.size() - offset) {
            return std::nullopt;
        }
        // find 가 이분 탐색을 하므로 경로는 엄격한 오름차순이어야 한다 (중복도 거부).
        if (!pack.entries_.empty() && !(pack.entries_.back().path < path)) {
            return std::nullopt;
        }
        pack.entries_.push_back(
            Entry{path, bytes.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(size))});
    }
    return pack;
}

std::optional<std::string_view> AssetPack::find(std::string_view path) const {
    const auto it = std::lower_bound(
        entries_.begin(), entries_.end(), path,
        [](const Entry& entry, std::string_view key) { return entry.path < key; });
    if (it == entries_.end() || it->path != path) {
        return std::nullopt;
    }
    return it->data;
}

}  // namespace pulse
```

`engine/src/platform/windows/EmbeddedAssets.h`:

```cpp
#pragma once

#include "network/AssetPack.h"

namespace pulse {

// exe 에 링크된 WEBPAK 리소스(RCDATA)를 읽은 AssetPack. 리소스가 없거나 pak 이 손상됐으면
// nullptr. 결과는 프로세스 수명 동안 유지되며 exe 이미지를 가리킨다.
const AssetPack* embeddedAssetPack();

}  // namespace pulse
```

`engine/src/platform/windows/EmbeddedAssets.cpp`:

```cpp
#include "platform/windows/EmbeddedAssets.h"

#include <windows.h>

#include <optional>
#include <string_view>

namespace pulse {
namespace {

std::optional<AssetPack> loadEmbedded() {
    const HRSRC resource = ::FindResourceW(nullptr, L"WEBPAK", reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (resource == nullptr) {
        return std::nullopt;
    }
    const DWORD size = ::SizeofResource(nullptr, resource);
    const HGLOBAL loaded = ::LoadResource(nullptr, resource);
    if (size == 0 || loaded == nullptr) {
        return std::nullopt;
    }
    const void* data = ::LockResource(loaded);
    if (data == nullptr) {
        return std::nullopt;
    }
    return AssetPack::parse(std::string_view(static_cast<const char*>(data), size));
}

}  // namespace

const AssetPack* embeddedAssetPack() {
    static const std::optional<AssetPack> pack = loadEmbedded();
    return pack ? &*pack : nullptr;
}

}  // namespace pulse
```

`engine/resources/web.rc.in`:

```text
// Generated by CMake from resources/web.rc.in. Embeds the packed frontend into the exe.
WEBPAK RCDATA "web.pak"
```

`engine/tests/pak_builder.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 테스트용 web.pak 생성기. 스펙 3절 포맷을 그대로 만든다.
namespace pulse_test {

inline void putLe(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

// 경로는 호출자가 오름차순으로 넘긴다 (정렬을 어기는 pak 을 만드는 테스트도 있다).
inline std::string makePak(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string table;
    std::size_t table_size = 0;
    for (const auto& file : files) {
        table_size += 2 + file.first.size() + 8 + 8;
    }
    std::uint64_t offset = 8 + 4 + table_size;
    for (const auto& file : files) {
        putLe(table, file.first.size(), 2);
        table += file.first;
        putLe(table, offset, 8);
        putLe(table, file.second.size(), 8);
        offset += file.second.size();
    }
    std::string pak(std::string("PLSPAK1\0", 8));
    putLe(pak, files.size(), 4);
    pak += table;
    for (const auto& file : files) {
        pak += file.second;
    }
    return pak;
}

}  // namespace pulse_test
```

`engine/tests/test_asset_pack.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "network/AssetPack.h"
#include "pak_builder.h"

using pulse_test::makePak;

TEST_CASE("AssetPack finds every file by its path", "[assetpack]") {
    const std::string pak = makePak({{"/assets/a.js", "console.log(1)"},
                                     {"/index.html", "<html></html>"},
                                     {"/z.css", ""}});
    const auto pack = pulse::AssetPack::parse(pak);
    REQUIRE(pack.has_value());
    CHECK(pack->size() == 3);
    CHECK(pack->find("/index.html").value() == "<html></html>");
    CHECK(pack->find("/assets/a.js").value() == "console.log(1)");
    CHECK(pack->find("/z.css").value().empty());
    CHECK_FALSE(pack->find("/missing").has_value());
    CHECK_FALSE(pack->find("index.html").has_value());
}

TEST_CASE("AssetPack handles an empty pack and UTF-8 paths", "[assetpack]") {
    const auto empty = pulse::AssetPack::parse(makePak({}));
    REQUIRE(empty.has_value());
    CHECK(empty->size() == 0);
    CHECK_FALSE(empty->find("/index.html").has_value());

    const std::string pak = makePak({{"/\xed\x95\x9c\xea\xb8\x80.txt", "x"}});
    const auto pack = pulse::AssetPack::parse(pak);
    REQUIRE(pack.has_value());
    CHECK(pack->find("/\xed\x95\x9c\xea\xb8\x80.txt").value() == "x");
}

TEST_CASE("AssetPack rejects malformed input", "[assetpack]") {
    const std::string good = makePak({{"/a", "hello"}, {"/b", "world"}});
    REQUIRE(pulse::AssetPack::parse(good).has_value());

    SECTION("empty and too short") {
        CHECK_FALSE(pulse::AssetPack::parse("").has_value());
        CHECK_FALSE(pulse::AssetPack::parse(good.substr(0, 11)).has_value());
    }
    SECTION("wrong magic") {
        std::string bad = good;
        bad[0] = 'X';
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("truncated anywhere before the last file ends") {
        for (std::size_t n = 0; n < good.size(); ++n) {
            CHECK_FALSE(pulse::AssetPack::parse(good.substr(0, n)).has_value());
        }
    }
    SECTION("count larger than the table") {
        std::string bad = good;
        bad[8] = 5;
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("offset plus size past the end, including overflow") {
        std::string bad = good;
        // 첫 항목: path_len(2) "/a"(2) offset(8) size(8) 이므로 size 는 12 + 2 + 2 + 8 = 24 바이트 뒤.
        for (int i = 0; i < 8; ++i) {
            bad[8 + 4 + 2 + 2 + 8 + i] = static_cast<char>(0xff);
        }
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("paths out of order or duplicated") {
        CHECK_FALSE(pulse::AssetPack::parse(makePak({{"/b", "1"}, {"/a", "2"}})).has_value());
        CHECK_FALSE(pulse::AssetPack::parse(makePak({{"/a", "1"}, {"/a", "2"}})).has_value());
    }
}
```

- [ ] **Step 2: `engine/CMakeLists.txt` 수정**

`pulse_core` 소스 목록의 `src/network/StaticFiles.cpp` 다음 줄에 두 줄을 더한다:

```cmake
  src/network/AssetPack.cpp
  src/platform/windows/EmbeddedAssets.cpp
```

그리고 `add_executable(pulse-engine src/main.cpp)` 한 줄을 아래 블록으로 바꾼다 (`target_link_libraries(pulse-engine PRIVATE pulse_core)` 는 그대로 그 아래에 둔다):

```cmake
# 릴리스 빌드는 scripts/make-pak.ps1 이 만든 web.pak 을 넘겨 프런트엔드를 exe 에 넣는다.
# 값이 없으면 (개발 빌드·테스트) 리소스 없이 지금처럼 동작한다.
set(PULSE_WEB_PAK "" CACHE FILEPATH "Packed frontend (web.pak) to embed into pulse-engine")
set(PULSE_ENGINE_SOURCES src/main.cpp)
if(PULSE_WEB_PAK)
  if(NOT EXISTS "${PULSE_WEB_PAK}")
    message(FATAL_ERROR "PULSE_WEB_PAK does not exist: ${PULSE_WEB_PAK}")
  endif()
  # rc 는 경로의 비ASCII 문자와 역슬래시에 약하다. 빌드 폴더로 복사해 상대 이름으로 참조한다.
  configure_file("${PULSE_WEB_PAK}" "${CMAKE_CURRENT_BINARY_DIR}/web.pak" COPYONLY)
  configure_file(resources/web.rc.in "${CMAKE_CURRENT_BINARY_DIR}/web.rc" COPYONLY)
  list(APPEND PULSE_ENGINE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/web.rc")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/web.rc"
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/web.pak")
endif()
add_executable(pulse-engine ${PULSE_ENGINE_SOURCES})
if(PULSE_WEB_PAK)
  target_include_directories(pulse-engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
```

- [ ] **Step 3: `engine/tests/CMakeLists.txt` 수정**

`pulse-tests` 소스 목록의 `test_static_files.cpp` 다음 줄에 `  test_asset_pack.cpp` 를 더한다.

- [ ] **Step 4: 개발 빌드와 테스트**

```bash
cd engine
cmake --preset default
cmake --build --preset default
./build/tests/Debug/pulse-tests.exe "[assetpack]"
./build/tests/Debug/pulse-tests.exe
```

Expected: 경고 0, `[assetpack]` 는 "All tests passed (87 assertions in 3 test cases)", 전체는 204 케이스 중 201 통과 + 3 건너뜀 (ETW).

- [ ] **Step 5: 릴리스 빌드로 pak 내장 확인**

PowerShell 스크립트로 pak 을 만들고 정적 릴리스 빌드에 넘긴다. 여기서는 빌드가 성공하고 exe 크기가 커졌는지만 본다 (내장본이 실제로 서빙되는지는 Task 3 의 패키징 검증이 한다).

```bash
mkdir -p dist-release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-pak.ps1 -WebDist web/dist -Out "C:\dev\pulse-uni\dist-release\web.pak"
cd engine
cmake -S . -B build-release -DPULSE_WEB_PAK="C:/dev/pulse-uni/dist-release/web.pak"
cmake --build build-release --config Release
ls -l build-release/Release/pulse-engine.exe
```

Expected: `web.pak` 은 4 파일, `pulse-engine.exe` 는 약 2.7 MB(pak 이 없으면 약 1.3 MB). `web/dist` 가 없으면 먼저 `cd web && npm ci && npm run build`. `dist-release/`, `engine/build-release/` 는 `.gitignore` 에 있어 커밋되지 않는다.

- [ ] **Step 6: 커밋**

```bash
git add scripts/make-pak.ps1 engine/src/network/AssetPack.h engine/src/network/AssetPack.cpp engine/src/platform/windows/EmbeddedAssets.h engine/src/platform/windows/EmbeddedAssets.cpp engine/resources/web.rc.in engine/tests/pak_builder.h engine/tests/test_asset_pack.cpp engine/CMakeLists.txt engine/tests/CMakeLists.txt
git commit -m "feat(engine): pack the built frontend into web.pak and embed it into the exe as a resource"
```

---

### Task 2: pak 서빙, `--embedded-web`, 인자 없는 실행 연결

**Files:**
- Modify: `engine/src/network/StaticFiles.h`, `engine/src/network/StaticFiles.cpp`
- Modify: `engine/src/network/ServerConfig.h`, `engine/src/network/WebSocketServer.cpp`
- Modify: `engine/src/cli/Options.h`, `engine/src/cli/Options.cpp`
- Modify: `engine/src/main.cpp`
- Test: `engine/tests/test_static_files.cpp`, `engine/tests/test_websocket_server.cpp`, `engine/tests/test_options.cpp`

**Interfaces:**
- Consumes: Task 1 의 `AssetPack`, `embeddedAssetPack()`, `pulse_test::makePak`.
- Produces: `PackLookupStatus`, `PackAsset`, `lookupPackAsset(const AssetPack&, const std::string& target) -> PackAsset` (StaticFiles.h); `ServerConfig::assets` (`const AssetPack*`, 기본 nullptr); `Options::use_embedded_web`; `applyDefaultLaunch(Options&, const std::string& web_root, bool web_root_exists, bool has_embedded)`.

- [ ] **Step 1: 실패하는 테스트를 먼저 쓴다**

`engine/tests/test_static_files.cpp` — 맨 위 include 에 `#include "network/AssetPack.h"` 와 `#include "pak_builder.h"` 를 더하고, 파일 끝에 덧붙인다:

```cpp
namespace {

// 스펙 3절 pak. 경로는 오름차순.
std::string samplePak() {
    return pulse_test::makePak({{"/assets/app.js", "console.log(1);"},
                                {"/index.html", "<html>index</html>"}});
}

}  // namespace

TEST_CASE("a pack lookup finds an asset by its path", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    const PackAsset asset = lookupPackAsset(*pack, "/assets/app.js");

    REQUIRE(asset.status == PackLookupStatus::Found);
    REQUIRE(asset.path == "/assets/app.js");
    REQUIRE(asset.data == "console.log(1);");
}

TEST_CASE("a pack lookup maps the root to index.html and strips the query", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    REQUIRE(lookupPackAsset(*pack, "/").path == "/index.html");
    REQUIRE(lookupPackAsset(*pack, "/assets/app.js?v=3#x").path == "/assets/app.js");
}

TEST_CASE("a pack lookup falls back to index.html for unknown and colon paths", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    for (const char* target : {"/universe", "/app.js:stream", "/assets/"}) {
        const PackAsset asset = lookupPackAsset(*pack, target);
        REQUIRE(asset.status == PackLookupStatus::Found);
        REQUIRE(asset.path == "/index.html");
        REQUIRE(asset.data == "<html>index</html>");
    }
}

TEST_CASE("a pack lookup refuses malformed and escaping targets", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    for (const char* target : {"", "index.html", "/..", "/../x", "/a/../index.html",
                               "/assets\\app.js"}) {
        REQUIRE(lookupPackAsset(*pack, target).status == PackLookupStatus::Forbidden);
    }
}

TEST_CASE("a pack without index.html answers 404 for unknown paths", "[static][pack]") {
    const std::string bytes = pulse_test::makePak({{"/only.txt", "x"}});
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    REQUIRE(lookupPackAsset(*pack, "/only.txt").status == PackLookupStatus::Found);
    REQUIRE(lookupPackAsset(*pack, "/missing").status == PackLookupStatus::NotFound);
}
```

`engine/tests/test_websocket_server.cpp` — include 묶음에 `#include "network/AssetPack.h"` 와 `#include "pak_builder.h"` 를 더하고, 파일 끝에 덧붙인다 (`httpGet`·`httpRequest` 는 같은 파일의 앞쪽 익명 네임스페이스에 이미 있다):

```cpp
namespace {

// pak 모드 서버를 띄우고 요청 하나를 보낸 뒤 응답을 돌려준다.
http::response<http::string_body> packRequest(const std::string& pak_bytes,
                                              const std::string& target,
                                              http::verb method = http::verb::get) {
    const auto pack = AssetPack::parse(pak_bytes);
    REQUIRE(pack.has_value());

    ServerConfig cfg;
    cfg.port = 0;
    cfg.assets = &*pack;

    net::io_context ioc;
    WebSocketServer server(ioc, cfg);
    const unsigned short port = server.port();
    std::thread io([&] { ioc.run(); });

    const auto response = httpRequest(port, target, method);

    server.stop();
    io.join();
    return response;
}

const std::string kPak = pulse_test::makePak({{"/assets/app.js", "console.log(1);"},
                                              {"/index.html", "<html>index</html>"}});

}  // namespace

TEST_CASE("an embedded asset is served with its content type", "[ws][pack]") {
    const auto response = packRequest(kPak, "/assets/app.js");

    REQUIRE(response.result() == http::status::ok);
    REQUIRE(response.body() == "console.log(1);");
    REQUIRE(response[http::field::content_type] == "text/javascript");
    REQUIRE(response[http::field::cache_control].empty());
}

TEST_CASE("the embedded index carries no-cache and nosniff, and unknown paths fall back to it",
          "[ws][pack]") {
    const auto root = packRequest(kPak, "/");
    REQUIRE(root.result() == http::status::ok);
    REQUIRE(root.body() == "<html>index</html>");
    REQUIRE(root[http::field::cache_control] == "no-cache");
    REQUIRE(root[http::field::x_content_type_options] == "nosniff");

    const auto fallback = packRequest(kPak, "/universe");
    REQUIRE(fallback.result() == http::status::ok);
    REQUIRE(fallback.body() == "<html>index</html>");
}

TEST_CASE("an embedded path escaping the root is refused", "[ws][pack]") {
    REQUIRE(packRequest(kPak, "/../secret").result() == http::status::forbidden);
}

TEST_CASE("a non-GET request to embedded assets is rejected with 405", "[ws][pack]") {
    const auto response = packRequest(kPak, "/", http::verb::post);

    REQUIRE(response.result() == http::status::method_not_allowed);
    REQUIRE(response[http::field::allow] == "GET");
}
```

`engine/tests/test_options.cpp` — 기존 세 `applyDefaultLaunch` 테스트(`the default launch serves the web folder ...`, `the default launch leaves the options alone ...`)를 아래 테스트들로 **바꾼다**. `usage mentions the no-argument launch` 테스트는 그대로 두고 그 아래에 새 테스트들을 더한다:

```cpp
TEST_CASE("the default launch serves the web folder with the usual defaults", "[options]") {
    Options options;

    REQUIRE(applyDefaultLaunch(options, "C:\\app\\web", true, false));

    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.web_root == "C:\\app\\web");
    REQUIRE_FALSE(options.use_embedded_web);
    REQUIRE(options.port == 9000);
    REQUIRE(options.interval_ms == 1000);
    REQUIRE(options.mapping == Mapping::Auto);
}

TEST_CASE("the web folder beats the embedded assets", "[options]") {
    Options options;

    REQUIRE(applyDefaultLaunch(options, "C:\\app\\web", true, true));

    REQUIRE(options.web_root == "C:\\app\\web");
    REQUIRE_FALSE(options.use_embedded_web);
}

TEST_CASE("the default launch falls back to the embedded assets without a web folder",
          "[options]") {
    Options missing;
    REQUIRE(applyDefaultLaunch(missing, "C:\\app\\web", false, true));
    REQUIRE(missing.mode == Mode::Serve);
    REQUIRE(missing.use_embedded_web);
    REQUIRE(missing.web_root.empty());

    Options empty_path;
    REQUIRE(applyDefaultLaunch(empty_path, "", true, true));
    REQUIRE(empty_path.use_embedded_web);
    REQUIRE(empty_path.web_root.empty());
}

TEST_CASE("the default launch leaves the options alone with neither folder nor embedded assets",
          "[options]") {
    Options options;
    options.port = 1234;

    REQUIRE_FALSE(applyDefaultLaunch(options, "C:\\app\\web", false, false));
    REQUIRE_FALSE(applyDefaultLaunch(options, "", true, false));

    REQUIRE(options.mode == Mode::None);
    REQUIRE(options.port == 1234);
}

TEST_CASE("usage mentions the embedded web assets", "[options]") {
    REQUIRE(usageText().find("--embedded-web") != std::string::npos);
}

TEST_CASE("--embedded-web selects the embedded assets for --serve", "[options]") {
    Options options;
    std::string error;
    const char* argv[] = {"pulse-engine", "--serve", "--embedded-web"};

    REQUIRE(parseOptions(3, argv, options, error) == ParseResult::Ok);

    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.use_embedded_web);
    REQUIRE(options.web_root.empty());
}

TEST_CASE("--embedded-web is refused with --web-root and without --serve", "[options]") {
    Options options;
    std::string error;

    const char* both[] = {"pulse-engine", "--serve", "--embedded-web", "--web-root", "web"};
    REQUIRE(parseOptions(5, both, options, error) == ParseResult::Error);
    REQUIRE(error == "--embedded-web cannot be combined with --web-root");

    const char* dump[] = {"pulse-engine", "--dump", "--embedded-web"};
    REQUIRE(parseOptions(3, dump, options, error) == ParseResult::Error);
    REQUIRE(error == "--embedded-web needs --serve");
}
```

- [ ] **Step 2: 실패를 확인한다**

```bash
cd engine && cmake --build --preset default 2>&1 | grep -E "error" | head
```

Expected: `lookupPackAsset`, `PackLookupStatus`, `ServerConfig::assets`, `use_embedded_web`, 4 인자 `applyDefaultLaunch` 가 없다는 컴파일 오류.

- [ ] **Step 3: `StaticFiles` 에 pak 조회를 더한다**

`engine/src/network/StaticFiles.h` — `#include <string>` 아래에 `#include <string_view>` 를 더하고, `namespace pulse {` 바로 안쪽 첫 줄에 `class AssetPack;` 를 더한다. 파일 끝의 `mimeTypeFor` 선언 위에 아래를 넣는다:

```cpp
// 내장 pak 에서 찾은 결과 (스펙 4절).
//  - Found:     path 는 실제로 찾은 키 ("/index.html" 등), data 는 pak 버퍼를 가리킨다.
//  - Forbidden: 요청 대상의 모양이 틀렸거나 ".." 세그먼트가 있다. 403 으로 답한다.
//  - NotFound:  없는 경로이고 index.html 도 없다. 404 로 답한다.
enum class PackLookupStatus {
    Found,
    Forbidden,
    NotFound,
};

struct PackAsset {
    PackLookupStatus status = PackLookupStatus::NotFound;
    std::string path;       // Found 일 때만 의미가 있다
    std::string_view data;  // Found 일 때만 의미가 있다
};

// HTTP 요청 대상을 pak 안의 자산으로 바꾼다. 쿼리·프래그먼트는 뗀다. "/" 는 "/index.html".
// 없는 경로(와 ':' 가 든 경로)는 SPA 폴백으로 "/index.html" 을 돌려준다.
PackAsset lookupPackAsset(const AssetPack& pack, const std::string& target);

```

`engine/src/network/StaticFiles.cpp` — include 묶음에 `#include <string_view>` 와 `#include "network/AssetPack.h"` 를 더하고, `mimeTypeFor` 정의 바로 위에 넣는다:

```cpp
PackAsset lookupPackAsset(const AssetPack& pack, const std::string& target) {
    std::string path = target.substr(0, target.find_first_of("?#"));

    if (path.empty() || path.front() != '/' || path.find('\\') != std::string::npos) {
        return PackAsset{PackLookupStatus::Forbidden, {}, {}};
    }
    // pak 에서 ".." 는 뜻이 없다. 탈출 시도로 보고 거절한다.
    std::size_t start = 1;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view segment =
            std::string_view(path).substr(start, end == std::string::npos ? end : end - start);
        if (segment == "..") {
            return PackAsset{PackLookupStatus::Forbidden, {}, {}};
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (path == "/") {
        path = "/index.html";
    }
    // ':' 는 대체 데이터 스트림 구문이다. 폴더 모드처럼 없는 파일로 보고 앱을 돌려준다.
    if (path.find(':') == std::string::npos) {
        if (const auto data = pack.find(path)) {
            return PackAsset{PackLookupStatus::Found, path, *data};
        }
    }
    if (const auto index = pack.find("/index.html")) {
        return PackAsset{PackLookupStatus::Found, "/index.html", *index};
    }
    return PackAsset{PackLookupStatus::NotFound, {}, {}};
}

```

- [ ] **Step 4: 서버에 pak 모드를 더한다**

`engine/src/network/ServerConfig.h` — `namespace pulse {` 안쪽 첫 줄에 `class AssetPack;` 를 더하고, `web_root` 필드 아래에 넣는다:

```cpp

    // 내장 프런트엔드 (web.pak). 설정되면 web_root 대신 이것을 서빙한다. 소유하지 않는다.
    const AssetPack* assets = nullptr;
```

`engine/src/network/WebSocketServer.cpp`:
1. include 묶음의 `#include "network/StaticFiles.h"` 앞에 `#include "network/AssetPack.h"` 를 더한다.
2. `onRequest` 의 `if (server_.cfg_.web_root.empty()) {` 를 `if (server_.cfg_.web_root.empty() && server_.cfg_.assets == nullptr) {` 로 바꾼다 (426 은 폴더도 내장본도 없을 때만).
3. `serveStatic` 위(주석 `// web_root 아래의 파일로 응답한다.` 바로 앞)에 새 메서드를 넣는다:

```cpp
    // 내장 pak 에서 응답한다. 없으면 index.html 로 대체한다 (SPA 라우팅).
    void servePack(const AssetPack& pack) {
        const std::string target = std::string(request_.target());
        const PackAsset asset = lookupPackAsset(pack, target);
        if (asset.status == PackLookupStatus::Forbidden) {
            std::fprintf(stderr, "refused path outside the web root: %s\n", target.c_str());
            sendSimple(http::status::forbidden, "forbidden", "text/plain");
            return;
        }
        if (asset.status == PackLookupStatus::NotFound) {
            sendSimple(http::status::not_found, "not found", "text/plain");
            return;
        }
        sendSimple(http::status::ok, std::string(asset.data), mimeTypeFor(asset.path),
                   /*allow_header=*/{}, asset.path == "/index.html");
    }

```

4. `serveStatic()` 본문의 첫 줄(`const std::string& root = server_.cfg_.web_root;`) 앞에 넣는다:

```cpp
        if (server_.cfg_.assets != nullptr) {
            servePack(*server_.cfg_.assets);
            return;
        }
```

- [ ] **Step 5: 옵션을 더한다**

`engine/src/cli/Options.h` — `Options` 의 `std::string web_root;` 아래에 `bool use_embedded_web = false;  // --embedded-web. web_root 와 함께 쓰지 않는다.` 를 더하고, `applyDefaultLaunch` 선언과 그 앞 주석을 아래로 바꾼다:

```cpp
// 릴리스 zip 설계 D70, 웹 내장 설계 D77. 인자 없이 exe 를 실행했을 때(더블클릭)의 설정이다.
// exe 옆에 프런트엔드 폴더가 있으면 그것을, 없고 내장본이 있으면 내장본을 서빙하는 --serve 로
// 동작한다. 둘 다 없으면 out 을 건드리지 않고 false 를 돌려주며, 호출자는 지금처럼
// 사용법을 출력한다.
bool applyDefaultLaunch(Options& out, const std::string& web_root, bool web_root_exists,
                        bool has_embedded);
```

`engine/src/cli/Options.cpp`:
- `applyDefaultLaunch` 정의 전체를 바꾼다:

```cpp
bool applyDefaultLaunch(Options& out, const std::string& web_root, bool web_root_exists,
                        bool has_embedded) {
    const bool folder = web_root_exists && !web_root.empty();
    if (!folder && !has_embedded) {
        return false;
    }
    Options launch;
    launch.mode = Mode::Serve;
    if (folder) {
        launch.web_root = web_root;
    } else {
        launch.use_embedded_web = true;
    }
    out = launch;
    return true;
}
```

- `usageText()`: `"                       [--interval-ms N] [--max-groups N] [--allow-origin URL]\n"` 줄 바로 아래에 `"                       [--embedded-web]\n"` 를 더한다. `"  --web-root DIR    Serve the built frontend from DIR (default: websocket only).\n"` 줄 아래에 `"  --embedded-web    Serve the frontend built into this exe (release builds only).\n"` 를 더한다. 그리고 `"With no arguments ..."` 문단(지금 두 줄)을 아래로 바꾼다:

```cpp
        "  With no arguments, pulse-engine serves the web/ folder next to the exe, or the\n"
        "  frontend built into the exe when there is no such folder, and opens the browser.\n"
```

  (기존 테스트 `usage mentions the no-argument launch` 가 찾는 `With no arguments` 는 그대로 있어야 한다.)
- 인자 파싱: `--web-root` 분기 다음에 넣는다:

```cpp
        } else if (std::strcmp(arg, "--embedded-web") == 0) {
            parsed.use_embedded_web = true;
```

- 검증: `parsed.mode == Mode::Json && parsed.iterations != 0` 검사 바로 아래(`out = parsed;` 앞)에 넣는다:

```cpp
    if (parsed.use_embedded_web) {
        if (parsed.mode != Mode::Serve) {
            error = "--embedded-web needs --serve";
            return ParseResult::Error;
        }
        if (!parsed.web_root.empty()) {
            error = "--embedded-web cannot be combined with --web-root";
            return ParseResult::Error;
        }
    }
```

- [ ] **Step 6: `main.cpp` 연결**

`engine/src/main.cpp`:
1. include 에 `#include "platform/windows/EmbeddedAssets.h"` 를 `EtwSchedulerCollector.h` 앞에 더한다.
2. `runServe` 에서, 기존 `if (!options.web_root.empty()) { ... cfg.server.web_root = options.web_root; }` 블록 바로 아래에 넣는다:

```cpp
    if (options.use_embedded_web) {
        const pulse::AssetPack* assets = pulse::embeddedAssetPack();
        if (assets == nullptr) {
            std::fprintf(stderr, "no embedded web assets in this build\n");
            return 2;
        }
        cfg.server.assets = assets;
    }
```

3. `const bool has_web_root = !cfg.server.web_root.empty();` 를 `const bool has_web_root = !cfg.server.web_root.empty() || cfg.server.assets != nullptr;` 로 바꾼다.
4. `main` 의 인자 없는 실행 블록(`bool launched_by_default = false;` 부터 그 `if (argc == 1) { ... }` 끝까지)을 바꾼다:

```cpp
    bool launched_by_default = false;
    if (argc == 1) {
        const std::filesystem::path web = webFolderNextToExe();
        std::string web_string;
        bool exists = false;
        // path::string() 은 현재 코드 페이지로 바꿀 수 없는 경로에서 예외를 던진다. 그 경우
        // 폴더는 없는 것으로 보고 내장본으로 물러난다.
        try {
            std::error_code ec;
            exists = !web.empty() && std::filesystem::is_directory(web, ec) && !ec;
            web_string = web.string();
        } catch (const std::exception&) {
            exists = false;
            web_string.clear();
        }
        launched_by_default = pulse::applyDefaultLaunch(options, web_string, exists,
                                                        pulse::embeddedAssetPack() != nullptr);
    }
```

   그리고 그 위 주석(`// 릴리스 zip 설계 D70. ...` 두 줄)을 다음 두 줄로 고친다:

```cpp
    // 릴리스 zip 설계 D70, 웹 내장 설계 D77. 인자 없이 실행(더블클릭)하면 exe 옆의 web/ 을,
    // 없으면 내장본을 서빙하고 브라우저를 연다. 둘 다 없으면 지금처럼 사용법을 출력한다.
```

- [ ] **Step 7: 빌드와 테스트**

```bash
cd engine
cmake --build --preset default
./build/tests/Debug/pulse-tests.exe
```

Expected: 경고 0, 실패 0 (`test cases: N | N-3 passed | 3 skipped`). 새 테스트: pack lookup 5, ws pack 4, options 는 기존 3 개가 새 7 개(폴더 1, 폴더 우선 1, 내장 폴백 1, 둘 다 없음 1, usage 1, 파싱 2)로 바뀐다.

- [ ] **Step 8: 커밋**

```bash
git add engine/src engine/tests
git commit -m "feat(engine): serve the embedded frontend; --embedded-web and the default launch fall back to it"
```

---

### Task 3: 패키징, CI, 문서

**Files:**
- Modify: `scripts/package.ps1`, `.github/workflows/release.yml`
- Modify: `README.md`, `scripts/release/README.txt`
- Modify: `docs/superpowers/specs/2026-09-30-embedded-web-design.md` (상태)

**Interfaces:**
- Consumes: Task 1 의 `scripts/make-pak.ps1`, CMake 옵션 `PULSE_WEB_PAK`; Task 2 의 `--serve --embedded-web`.
- Produces: `dist-release/` 에 `pulse-engine.exe`, `pulse-engine.exe.sha256`, `pulse-universe-v<ver>-win-x64.zip`, `…zip.sha256`.

- [ ] **Step 1: `scripts/package.ps1` 수정**

현재 파일을 먼저 읽는다. 아래 세 군데를 바꾼다 (나머지는 그대로).

(a) `# --- 2. engine` 절의 시작(`$engineDir = ...` 바로 앞)에 pak 생성을 넣고, `cmake configure` 호출에 `-DPULSE_WEB_PAK` 을 더한다:

```powershell
# --- 1b. pack the frontend so the engine can embed it -------------------------------------
New-Item -ItemType Directory -Force $OutDir | Out-Null
$pak = Join-Path $OutDir 'web.pak'
& (Join-Path $PSScriptRoot 'make-pak.ps1') -WebDist $webDist -Out $pak
```

그리고 configure 호출의 `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` 줄을 아래로 바꾼다 (경로는 포워드 슬래시로):

```powershell
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
        "-DPULSE_WEB_PAK=$($pak.Replace('\', '/'))"
```

(b) `# --- 4. assemble` 절에서 `Copy-Item $webDist (Join-Path $stage 'web') -Recurse` 줄을 **지운다** (zip 에 `web/` 이 없다).

(c) 파일 끝, 기존 `Write-Host "==> sha256 $hash"` 다음에 덧붙인다:

```powershell

# --- 5. the bare exe and its checksum -------------------------------------------------------
$bareExe = Join-Path $OutDir 'pulse-engine.exe'
Copy-Item $exe $bareExe -Force
$exeHash = (Get-FileHash $bareExe -Algorithm SHA256).Hash.ToLower()
"$exeHash  pulse-engine.exe" | Set-Content -Path "$bareExe.sha256" -Encoding ascii
Write-Host "==> $bareExe ($([math]::Round((Get-Item $bareExe).Length / 1MB, 2)) MB)"
Write-Host "==> sha256 $exeHash"
Remove-Item $pak -Force

# --- 6. the exe alone must serve the frontend ----------------------------------------------
# Copy it to an empty folder (no web/ next to it) and ask it for the embedded assets. The
# no-argument launch would open a browser, so the script uses --serve --embedded-web instead.
$probeDir = Join-Path ([System.IO.Path]::GetTempPath()) ("pulse-probe-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $probeDir | Out-Null
$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$probeExe = Join-Path $probeDir 'pulse-engine.exe'
Copy-Item $bareExe $probeExe
$probe = Start-Process -FilePath $probeExe -ArgumentList '--serve', '--embedded-web', '--port', $port `
    -WorkingDirectory $probeDir -WindowStyle Hidden -PassThru
try {
    $script = Get-ChildItem (Join-Path $webDist 'assets') -Filter '*.js' | Select-Object -First 1
    $checks = @('/', "/assets/$($script.Name)", '/some/spa/route')
    foreach ($path in $checks) {
        $response = $null
        for ($i = 0; $i -lt 50 -and $null -eq $response; $i++) {
            try { $response = Invoke-WebRequest "http://127.0.0.1:$port$path" -UseBasicParsing -TimeoutSec 2 }
            catch { Start-Sleep -Milliseconds 200 }
        }
        if ($null -eq $response -or $response.StatusCode -ne 200) { throw "embedded web: GET $path did not return 200" }
    }
    $served = (Invoke-WebRequest "http://127.0.0.1:$port/assets/$($script.Name)" -UseBasicParsing).RawContentLength
    if ($served -ne $script.Length) { throw "embedded $($script.Name) is $served bytes, expected $($script.Length)" }
    Write-Host '==> embedded web check passed'
} finally {
    if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
    Remove-Item $probeDir -Recurse -Force -ErrorAction SilentlyContinue
}
```

- [ ] **Step 2: 패키징을 실제로 돌린다**

```bash
export PATH="/c/Program Files/CMake/bin:$PATH" VCPKG_ROOT="C:/vcpkg"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -Version 0.2.0 -SkipNpmCi
ls -l dist-release
unzip -l dist-release/pulse-universe-v0.2.0-win-x64.zip
```

Expected: `==> static link check passed`, `==> embedded web check passed`, `dist-release/` 에 `pulse-engine.exe`(약 2.7 MB)·`pulse-engine.exe.sha256`·zip·`.sha256` 4 개 (`web.pak` 은 없다), zip 안에는 `pulse-engine.exe`, 두 `.bat`, `README.txt` 만 있고 `web/` 은 없다.

- [ ] **Step 3: `.github/workflows/release.yml` 의 게시 단계 수정**

`Publish the release` 단계의 `$files = ...` 줄을 아래로 바꾼다 (exe 자산은 이름이 `pulse-universe-` 로 시작하지 않는다):

```yaml
          $files = Get-ChildItem dist-release -File |
            Where-Object { $_.Name -like 'pulse-universe-*' -or $_.Name -like 'pulse-engine.exe*' } |
            ForEach-Object { $_.FullName }
          if (@($files).Count -ne 4) { throw "expected 4 release files, found $(@($files).Count): $($files -join ', ')" }
```

(`gh release create` 줄은 그대로.) YAML 이 파싱되는지 확인한다:

```bash
python -c "import yaml; print(list(yaml.safe_load(open('.github/workflows/release.yml', encoding='utf-8'))['jobs']))"
```

- [ ] **Step 4: 문서**

`README.md` 의 "다운로드해서 실행" 문단(`[Releases](...)` 로 시작하는 줄)을 아래로 바꾼다:

```markdown
[Releases](https://github.com/Rafdidas/pulse-universe/releases) 에서 `pulse-engine.exe` 하나만 받아 더블클릭하면 브라우저에 화면이 뜬다 (화면이 exe 안에 들어 있다). 더블클릭 실행용 `.bat` 과 안내 `README.txt` 가 필요하면 `pulse-universe-vX.Y.Z-win-x64.zip` 을 받아 푼다. 관리자 권한이 필요한 실측 흐름은 zip 의 `Start Pulse Universe (Admin).bat` 이다 (UAC 창에서 '아니오'를 누르면 아무것도 뜨지 않는다). exe 옆에 `web/` 폴더(빌드한 `web/dist`)를 두면 내장 화면 대신 그 폴더가 쓰인다. 서명되지 않은 프로그램이라 처음에는 Windows 가 경고할 수 있다 — zip 안의 `README.txt` 에 대처법이 있다. 아래는 소스에서 직접 빌드하는 방법이다.
```

`README.md` 의 "릴리스 만들기" 문단을 아래로 바꾼다:

```markdown
`scripts/package.ps1 -Version 0.1.0` 이 프런트엔드를 빌드하고 `scripts/make-pak.ps1` 로 `web.pak` 으로 묶은 뒤, 엔진을 정적 링크(외부 DLL·Visual C++ 재배포 패키지 필요 없음)로 빌드하며 그 pak 을 exe 에 내장한다. `dist-release/` 에 `pulse-engine.exe`, zip, 각각의 `.sha256` 이 나오고, 스크립트 끝에서 exe 만으로 화면이 서빙되는지 확인한다. `v0.1.0` 같은 태그를 푸시하면 GitHub Actions 가 같은 스크립트로 이 네 파일을 릴리스에 올린다. 푸시와 PR 마다 `CI` 워크플로가 웹·엔진 테스트를 돌린다. 내장 없이 개발할 때는 지금처럼 `--serve --web-root web/dist` 를 쓴다.
```

`scripts/release/README.txt` 의 `Run` 절 첫 항목 아래(`2. Press Ctrl+C ...` 앞)에 들여쓴 문단을 더한다:

```
   The page is built into pulse-engine.exe, so the exe also works alone in any folder. If a "web"
   folder sits next to the exe, that folder is used instead (handy for trying your own build).
```

(ASCII, CRLF 로 쓴다.) 스펙 상태 줄을 `- 상태: 승인됨 (시제품 확인됨 — 11절)` 로 고친다.

- [ ] **Step 5: 전체 재확인**

```bash
cd engine && cmake --build --preset default && ./build/tests/Debug/pulse-tests.exe | tail -3
cd ../web && npm test 2>&1 | tail -4
```

Expected: 엔진 전체 통과 (0 실패), 웹 전체 통과.

- [ ] **Step 6: 커밋**

```bash
git add scripts/package.ps1 .github/workflows/release.yml README.md scripts/release/README.txt docs/superpowers/specs/2026-09-30-embedded-web-design.md
git commit -m "feat: package the exe with the embedded frontend and publish it beside the zip"
```
