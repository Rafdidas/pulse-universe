# 웹 내장 exe 설계

- 작성일: 2026-09-30
- 상태: 승인됨 (시제품 확인됨 — 11절)
- 선행: 릴리스 zip (`v0.1.0`, `v0.1.1`), 인자 없는 실행 D70
- 범위: 빌드된 프런트엔드(`web/dist`)를 엔진 exe 안에 넣어, `pulse-engine.exe` 하나만으로 화면까지 뜨게 한다. 릴리스에는 exe 단독 파일과 기존 zip 을 함께 올린다. 압축, 다른 OS, 설치 프로그램, 코드 서명은 범위 밖이다.

## 1. 문제

지금 릴리스 zip 은 `pulse-engine.exe` 옆에 `web/` 폴더가 있어야 뜬다. exe 만 따로 옮기거나 `web/` 을 지우면 인자 없는 실행이 사용법 출력으로 물러난다. 사용자가 exe 하나만 받아 더블클릭하는 경로가 없다.

## 2. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D75 | `web/dist` 를 `web.pak` 하나로 묶어 Windows 리소스(RCDATA)로 exe 에 링크한다 | 소스에 큰 배열이 없어 컴파일이 빠르고, 리소스는 읽기 전용으로 exe 이미지에서 바로 읽힌다 |
| D76 | pak 은 압축하지 않는다 | 루프백 전송이고 exe 는 1.5 MB 커질 뿐이다. 압축기·해제기는 코드와 실패 지점을 늘린다 |
| D77 | 서빙 우선순위: `--web-root DIR` → (인자 없는 실행일 때) exe 옆 `web/` → 내장본 → 사용법 출력 | 폴더를 두면 사용자가 화면을 바꿔 볼 수 있고, 개발 흐름은 그대로다. 내장본은 마지막 안전망이다 |
| D78 | 개발 빌드(pak 없음)에서는 리소스를 넣지 않는다. 내장본이 없는 것과 같은 동작이다 | 테스트와 CI 가 프런트엔드 빌드에 의존하지 않는다 |
| D79 | 릴리스 Assets: `pulse-engine.exe` + `.sha256`, `pulse-universe-vX.Y.Z-win-x64.zip` + `.sha256`. zip 안에는 `web/` 폴더를 넣지 않는다 | 사용자가 고른 형태다. zip 은 `.bat`·README 가 필요한 사람용이다 |

## 3. pak 포맷

리틀 엔디언, 압축 없음.

```
magic      8 바이트   "PLSPAK1\0"
count      u32
entries    count 개:  path_len u16, path (UTF-8, "/index.html" 처럼 선행 '/'),
                       offset u64 (pak 처음부터), size u64
blobs      각 파일의 원본 바이트
```

- 경로는 `web/dist` 기준의 상대 경로에 선행 `/` 를 붙이고, 구분자는 `/` 로 통일한다. 정렬 순서는 경로 오름차순(결정적 출력).
- 읽는 쪽은 magic·count·범위(offset+size ≤ 전체 크기)를 검증하고, 하나라도 어긋나면 "내장본 없음" 으로 취급한다. 잘린 pak 이 조용히 일부만 서빙되는 일이 없다.
- 생성은 `scripts/package.ps1` 이 한다 (PowerShell 로 `BinaryWriter`). 읽기는 엔진 C++.

## 4. 엔진 구조

- `network/AssetPack.{h,cpp}` (새 파일, 플랫폼 무관): `AssetPack::parse(std::string_view bytes)` → `std::optional<AssetPack>`, `find(path)` → `std::optional<std::string_view>`. 바이트를 복사하지 않는다.
- `platform/windows/EmbeddedAssets.{h,cpp}`: `const AssetPack* embeddedAssetPack()`. `FindResourceW` 로 리소스 `WEBPAK`(RCDATA) 를 찾아 `LoadResource`/`LockResource` 로 뷰를 얻고 `parse` 한다. 리소스가 없거나 parse 가 실패하면 `nullptr`. 결과는 프로세스 수명 동안 캐시한다.
- `network/ServerConfig`: `const AssetPack* assets = nullptr;` (소유하지 않는다).
- `network/WebSocketServer::serveStatic`: `assets != nullptr` 이면 pak 에서 찾는다. 그렇지 않으면 기존 폴더 경로를 그대로 쓴다. pak 조회는 요청 대상에서 쿼리/프래그먼트를 떼고, `resolveWebPath` 와 같은 형태 검사(선행 `/` 필수, 역슬래시와 `..` 세그먼트는 403)를 거쳐 403/폴백을 정한다. pak 모드에서는 `..` 가 어디에 있든 403 이다 (경로를 정규화하는 폴더 모드보다 엄격하다). `:` 가 든 경로는 대체 데이터 스트림 구문이므로 없는 파일처럼 `/index.html` 로 폴백한다. 없는 경로는 `/index.html` 로 SPA 폴백, index 가 없으면 404. Content-Type 은 기존 `mimeTypeFor`, index 의 캐시 방지 헤더도 그대로다.
- `cli/Options`: `bool use_embedded_web = false;`. `applyDefaultLaunch(Options&, web_root, web_root_exists, has_embedded)` — 폴더가 있으면 지금처럼, 폴더가 없고 `has_embedded` 이면 `mode=Serve`, `use_embedded_web=true`, `web_root` 비움. 둘 다 없으면 `false`(사용법 출력). 사용법 문구에 내장본 설명을 한 줄 더한다.
- `main.cpp`: 인자 없는 실행에서 `embeddedAssetPack() != nullptr` 를 `has_embedded` 로 넘기고, `use_embedded_web` 이면 `cfg.server.assets` 를 채운다. 브라우저 열기·실패 대화상자는 지금 경로를 그대로 쓴다.
- `engine/CMakeLists.txt`: `PULSE_WEB_PAK`(경로, 기본 빈 값). 값이 있으면 `resources/web.rc.in` 을 `configure_file` 로 바꾸어 `pulse-engine` 타깃에만 소스로 추가한다 (`pulse_core`·테스트에는 넣지 않는다). `web.rc.in`: `WEBPAK RCDATA "@PULSE_WEB_PAK@"`.

## 5. 패키징 (`scripts/package.ps1`)

1. 프런트엔드 빌드 (지금과 같다).
2. `web/dist` → `<OutDir>/web.pak` 생성.
3. 엔진을 `-DPULSE_WEB_PAK=<web.pak>` 로 구성·빌드 (지금과 같은 정적 트리플릿).
4. 정적 링크 검사 (지금과 같다).
5. 조립: `pulse-engine.exe`, `scripts/release/*` 를 폴더에 넣고 zip (`web/` 제외). `pulse-engine.exe` 를 `<OutDir>` 로도 복사한다.
6. SHA256: zip 과 exe 각각 `.sha256`.
7. 검증(스크립트 끝): 빈 임시 폴더에 exe 만 복사해 `--serve --embedded-web --port <임의>` 로 띄우고 `/`·`/assets/*.js` 가 200 인지 확인한 뒤 끈다. 인자 없는 실행은 브라우저가 열리므로 스크립트에서는 쓰지 않는다 (6절).

## 6. 신호: `--embedded-web`

내장본을 검증하려면 `--serve` 가 내장본을 쓰게 하는 스위치가 필요하다. `--serve --embedded-web` 를 추가한다 (`--web-root` 와 같이 쓰면 오류). 인자 없는 실행은 내부적으로 같은 설정이 된다. 내장본이 없는 exe 에서 이 스위치를 쓰면 `no embedded web assets in this build` 로 종료 코드 2.

## 7. 실패 동작

| 상황 | 동작 |
|---|---|
| pak 손상·잘림 | 내장본 없음으로 취급. 인자 없는 실행이면 폴더 → 사용법 출력 |
| `--embedded-web` 인데 내장본 없음 | stderr 안내, 종료 코드 2 |
| 내장본과 exe 옆 `web/` 이 둘 다 있음 | `web/` 이 이긴다 (D77) |
| pak 안에 없는 경로 | SPA 폴백, index 도 없으면 404 |

## 8. 테스트

- `AssetPack`: 정상 pak 의 조회, 빈 pak, magic 불일치, count 가 실제보다 큼, offset+size 초과, 경로 중복 없음 확인, 잘린 입력, 멀티바이트(UTF-8) 경로.
- `serveStatic` (pak 모드): `/` → index, 자산 경로 → 그 바이트와 Content-Type, 없는 경로 → index 폴백, `/../x`·`\` → 403, `:` 가 든 경로(`/app.js:stream`)는 없는 파일처럼 index 폴백, 쿼리스트링 무시. 기존 폴더 모드 테스트는 변경 없이 통과해야 한다.
- `applyDefaultLaunch`: 폴더 있음/없음 × 내장 있음/없음 네 조합. 기존 세 테스트는 새 인자로 갱신한다.
- `--embedded-web` 파싱: 단독, `--web-root` 와 동시 사용 오류, 없는 빌드에서의 동작(main 수준은 수동 확인).
- 수동/스크립트: 패키징한 exe 만 빈 폴더에 두고 `--serve --embedded-web` 로 `/` 200, 자산 200, 존재하지 않는 경로 200(SPA). 더블클릭 실행은 브라우저가 열리는 것을 눈으로 확인한다.

## 9. 완료 조건

- exe 하나를 빈 폴더에 두고 더블클릭하면 브라우저에 화면이 뜬다.
- 같은 exe 옆에 `web/` 폴더를 두면 그 폴더가 쓰인다.
- 개발 빌드(`cmake --build --preset default`)는 프런트엔드 없이 그대로 빌드되고 테스트 201 건 이상이 통과한다 (새 테스트 포함해 늘어난 만큼).
- `scripts/package.ps1` 이 exe·zip 과 각각의 `.sha256` 을 만들고, exe 가 정적 링크 검사를 통과한다.
- 태그 푸시가 4 개 자산(exe, zip, 각 `.sha256`)을 Release 에 올린다.
- README·`README.txt`·릴리스 절이 새 배포 형태를 설명한다.

## 10. 범위 밖

pak 압축·gzip 응답, 내장본의 핫 리로드, 리소스 서명·무결성 해시 검증, 내장 폰트·이미지 외의 다른 자산 종류, macOS/Linux, 설치 프로그램, 코드 서명.

## 11. 시제품 결과 (2026-10-01)

pak 생성(`make-pak.ps1`)·읽기(`AssetPack`)·리소스 링크·`FindResource` 를 실제 빌드로 확인했다. 서빙 연결(`serveStatic`, `Options`, `main`)은 확인하지 않았다.

| 항목 | 결과 |
|---|---|
| `web/dist` (4 파일) → `web.pak` | 1,492,445 바이트. 헤더 `PLSPAK1 `, count 4, 경로 오름차순 |
| 내장 후 정적 exe | 2,785,280 바이트 (내장 전 1,283,072 + pak) |
| 내장 exe 에서 `embeddedAssetPack()` | 4 파일, `/index.html` 477 바이트 |
| pak 없이 빌드한 개발 exe | `embedded=no` (D78) |
| pak 원본 경로가 한글·공백(`/tmp/한글 폴더/web.pak`) | 그대로 내장된다 (CMake 가 빌드 폴더의 `web.pak` 으로 복사해 rc 에는 상대 이름만 준다) |
| `AssetPack` 단위 테스트 | 3 케이스 87 단언 통과 (정상, 빈 pak·UTF-8 경로, 잘림 전 구간·잘못된 magic·count·범위 넘침·정렬 위반) |
| 경고 | 개발·릴리스 빌드 모두 0 |

시제품 파일은 스크래치패드 `embedproto/` 에 있고 계획의 Task 1 코드가 이와 바이트 단위로 같다.
