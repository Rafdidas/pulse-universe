# 릴리스 zip 설계

- 작성일: 2026-09-30
- 상태: 승인됨 (시제품 확인)
- 선행: README·`run.bat`, ETW 확장(`--mapping`), M1~M9 구현 (`main`)
- 범위: 저장소를 받지 않아도, 빌드 도구가 없어도 실행할 수 있는 Windows 릴리스 zip 을 만들고, GitHub Actions 로 검사와 릴리스를 자동화한다. 설치 프로그램·데스크톱 앱·코드 서명은 범위 밖이다.

## 1. 문제

지금 실행하려면 저장소를 받고, Node·CMake·vcpkg·MSVC 를 갖춰 프런트엔드와 엔진을 빌드해야 한다 (`run.bat` 이 빌드를 대신해도 도구는 있어야 한다). 빌드한 엔진 exe 도 `boost_json.dll` 과 Visual C++ 재배포 패키지에 기대므로 exe 만 다른 PC 에 복사하면 뜨지 않을 수 있다. 인자 없이 exe 를 더블클릭하면 사용법을 출력하고 바로 닫힌다. GitHub Actions 가 없어 푸시마다 테스트를 돌리는 곳이 없다.

## 2. 측정

| | 동적 링크 (지금) | 정적 링크 (시험 빌드) |
|---|---|---|
| 엔진 exe | 960 KB + `boost_json-vc143-mt-x64-1_92.dll` 314 KB | 1,283,072 바이트 하나 |
| 외부 의존 | Boost DLL, `MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`, UCRT `api-ms-win-crt-*` | `pdh`, `WS2_32`, `MSWSOCK`, `ADVAPI32`, `KERNEL32` (전부 Windows 기본) |
| 프런트엔드 `web/dist` | 1.5 MB (`index-*.js` 1,478,264 바이트, `*.css` 4,024 바이트) | 같음 |

정적 링크: vcpkg 트리플릿 `x64-windows-static`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`. 처음 구성 188 초(Boost 를 새로 빌드), 그 뒤 엔진 빌드 26 초. 정적 exe 로 `--json` 이 스냅샷을 내고, `--serve --web-root web/dist` 가 `/` 에 HTTP 200 을 돌려주는 것을 확인했다. 저장소에는 `.github/` 도 태그도 없다.

## 3. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D70 | 엔진을 인자 없이 실행하면 exe 옆 `web/` 로 `--serve` 하고 브라우저를 연다. `web/` 이 없으면 지금처럼 사용법을 출력한다 | exe 더블클릭만으로 뜬다. 기존 옵션과 사용법은 그대로다 |
| D71 | 패키징은 `scripts/package.ps1` 하나가 한다: 프런트엔드 빌드 → 엔진 정적 빌드 → zip 조립 → SHA256. CI 도 같은 스크립트를 부른다 | 내 PC 에서 지금 검증할 수 있고, CI 와 로컬이 같은 결과를 낸다 |
| D72 | GitHub Actions: 모든 푸시·PR 은 검사(테스트·타입체크·린트·빌드), `v*` 태그는 릴리스 | 회귀를 푸시 시점에 잡고, 태그 하나로 릴리스가 나온다 |
| D73 | exe 는 서명하지 않는다. README.txt 에 SmartScreen 경고 대처를 적는다 | 인증서가 없다. 서명은 스크립트에 한 단계로 나중에 붙일 수 있다 |
| D74 | 관리자 권한은 두 번째 `.bat` 로 제공한다 (`Start-Process -Verb RunAs`). exe 매니페스트로 항상 승격하지 않는다 | 권한 없이도 추정으로 동작하는 것이 기본이다. 승격은 사용자가 고른다 |

### 3.1 되돌리기 쉬운 것과 어려운 것

모두 되돌리기 쉽다. 릴리스 이름 규칙과 zip 안 구조는 첫 릴리스를 공개한 뒤에는 바꾸면 받는 사람에게 영향이 있으므로 첫 태그 전에 확정한다 (4절).

## 4. zip 구조

```
pulse-universe-v0.1.0-win-x64.zip
  pulse-universe-v0.1.0-win-x64/            zip 안의 최상위 폴더 하나 (풀 때 파일이 흩어지지 않게)
    Start Pulse Universe.bat
    Start Pulse Universe (Admin).bat
    pulse-engine.exe
    web/...
    README.txt
```

- 파일 이름의 버전은 태그(`v0.1.0`)에서 온다. 로컬에서는 인자로 준다 (`-Version 0.1.0`, 기본 `dev`).
- 같은 디렉터리에 `pulse-universe-v0.1.0-win-x64.zip.sha256` (한 줄, `해시  파일이름`).
- `Start Pulse Universe.bat`: `start "" pulse-engine.exe` 대신 창에서 실행해 로그를 볼 수 있게 `pulse-engine.exe` 를 직접 실행한다. 인자가 없으므로 D70 이 브라우저를 연다.
- `Start Pulse Universe (Admin).bat`: 관리자로 승격해 같은 동작.
- `README.txt`: 실행 방법, 관리자 실행(`elevated` 배지), 끝내는 법(창에서 Ctrl+C), SmartScreen 안내(추가 정보 → 실행), 제거(폴더 삭제), 저장소 주소.
- `.bat` 은 ASCII 와 CRLF 로 쓴다 (한글이 들어가면 cmd 가 잘못 해석한다 — `run.bat` 을 만들 때 확인).

## 5. 인자 없는 실행 (D70)

- `cli/Options` 에 `bool applyDefaultLaunch(Options& out, const std::string& web_root, bool web_root_exists)` 를 둔다. `web_root_exists` 가 참이면 `out` 을 `mode = Serve`, `web_root = web_root`, 나머지 기본값으로 채우고 `true`, 아니면 `out` 을 건드리지 않고 `false`.
- `main` 은 `argc == 1` 일 때 exe 경로(`GetModuleFileNameW`)의 디렉터리 옆 `web` 을 검사해 `applyDefaultLaunch` 를 부른다. 참이면 그 옵션으로 진행하고 서버가 뜬 뒤 `ShellExecuteW` 로 `http://127.0.0.1:<port>/` 를 연다. 거짓이면 사용법(기존 동작).
- 포트가 이미 쓰이면 기존처럼 이유를 출력하고 끝난다. 그 경우 창이 바로 닫히지 않게 `.bat` 은 `pause` 로 끝난다 (exe 를 직접 더블클릭했을 때는 콘솔이 닫히므로, 실패 메시지는 `MessageBoxW` 로도 한 번 보여 준다).

## 6. 패키징 스크립트 (D71)

`scripts/package.ps1 [-Version <문자열>] [-OutDir <경로>]` (기본 `dist-release/`).

1. `web/`: `npm ci` (`-SkipNpmCi` 로 생략할 수 있다), `npm run build`.
2. 엔진: `cmake -S engine -B engine/build-release -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, `cmake --build engine/build-release --config Release --target pulse-engine`.
3. 정적 링크 확인: `dumpbin /dependents` 결과에 `boost`·`VCRUNTIME`·`MSVCP` 가 있으면 실패한다.
4. 스테이징 디렉터리에 `pulse-engine.exe`, `web/`, 두 `.bat`, `README.txt` 를 모으고 zip. 항목 이름은 슬래시로 직접 쓴다 (Windows PowerShell 5.1 의 `CreateFromDirectory` 는 역슬래시를 써서 다른 압축 해제 도구가 잘못 읽는다).
5. SHA256 파일을 쓴다.
6. 결과 경로와 크기를 출력한다.

`.bat`·`README.txt` 의 원본은 `scripts/release/` 아래에 두고 스크립트가 복사한다.

### 6.1 시제품 결과

- `scripts/package.ps1 -Version 0.1.0 -SkipNpmCi` 가 끝까지 돈다 (엔진 정적 구성·빌드 포함). 정적 링크 검사 통과. 결과: `pulse-universe-v0.1.0-win-x64.zip` 0.9 MB (풀면 exe 1,284,096 바이트 + web 1.5 MB), `.sha256` 한 줄.
- 다른 폴더에 풀어 인자 없이 `pulse-engine.exe` 를 실행하면 `/` 가 200 으로 화면을 내고, WebSocket 이 `hello` 를 보낸다. 포트가 이미 쓰이면 실패 이유를 출력하고 끝난다 (그 경우 `MessageBoxW` 는 대화형 세션이 아니라 시험에서 확인하지 못했다).
- 엔진 테스트 198 통과 + ETW 3 SKIP (기존 195 + 새 3), 경고 0.
- 워크플로 두 개는 YAML 로 읽힌다.
- 확인하지 못한 것: `Start Pulse Universe (Admin).bat` 은 UAC 승인이 필요해 이 시험에서는 사용자가 승인하지 않아 끝까지 보지 못했다. 브라우저가 실제로 열리는지(`ShellExecuteW`)와 GitHub Actions 의 실제 실행도 확인하지 못했다.
- 만드는 중 실수 하나: 스크립트를 파이썬 문자열로 쓰다가 ``·``·`\d` 가 제어 문자로 바뀌어 경로가 깨졌다. 파일을 다시 읽어 발견하고 직접 다시 썼다. 계획의 코드 블록은 그렇게 추출하지 말고 원문 그대로 옮겨야 한다.

## 7. GitHub Actions (D72)

`.github/workflows/ci.yml` — 트리거: `push`(브랜치 `main`), `pull_request`. `windows-latest`.

- 웹: `actions/setup-node`(Node 24) → `npm ci` → `npm test`, `npm run typecheck`, `npm run lint`, `npm run build` (`working-directory: web`).
- 엔진: 러너의 vcpkg(`VCPKG_INSTALLATION_ROOT`)로 `cmake --preset default` 대신 위 정적 구성 → `cmake --build ... --config Release` → `pulse-tests.exe` (Release). 권한이 없어 ETW 테스트는 SKIP 이다.
- vcpkg 바이너리 캐시(`actions/cache`, `~\AppData\Local\vcpkg\archives`)로 Boost 재빌드를 피한다.

`.github/workflows/release.yml` — 트리거: `push` 의 태그 `v*`. 검사와 같은 단계 뒤에 `scripts/package.ps1 -Version ${tag}` 를 부르고, 만든 zip 과 `.sha256` 을 그 태그의 GitHub Release 에 올린다 (`gh release create` 를 `GITHUB_TOKEN` 으로, `permissions: contents: write`). 릴리스 본문은 자동 생성(`--generate-notes`).

## 8. 실패 동작

- 스크립트는 어느 단계든 실패하면 즉시 멈추고 0 이 아닌 코드로 끝난다 (`$ErrorActionPreference = 'Stop'`, 외부 명령의 종료 코드 검사).
- 정적 링크가 풀려 있으면(3단계) zip 을 만들지 않는다.
- 태그 이름이 `v숫자.숫자.숫자` 가 아니면 릴리스 워크플로가 실패한다.

## 9. 테스트

| 대상 | 방식 |
|---|---|
| `applyDefaultLaunch` | Catch2: `web/` 있음 → serve·web_root 채움, 없음 → 옵션 그대로·false, 사용자가 준 다른 값(`port` 등)이 기본값과 겹칠 때의 기대 |
| 패키징 스크립트 | 내 PC 에서 실행해 zip 을 만들고, 풀어서 `pulse-engine.exe` 를 인자 없이 실행해 HTTP 200 과 브라우저 열기를 확인 |
| `.bat` | 풀린 폴더에서 더블클릭 상당(`cmd /c`)으로 실행해 서버가 뜨는지 확인. 관리자 `.bat` 은 UAC 를 승인해 `elevated` 를 확인 |
| 워크플로 | 문법(YAML) 검사. 실제 실행은 GitHub 서버에서만 가능하다 — 첫 실행에서 고칠 수 있다 |

## 10. 완료 조건

- 엔진 테스트·웹 테스트 통과, 빌드 경고 0.
- `scripts/package.ps1 -Version 0.1.0` 이 zip·sha256 을 만든다. 정적 링크 검사 통과.
- 만든 zip 을 다른 폴더에 풀어 인자 없이 `pulse-engine.exe` 를 실행하면 브라우저가 열리고 화면이 뜬다. Visual C++ 재배포 패키지에 기대지 않는다.
- 워크플로 두 개가 유효한 YAML 이다. 저장소에 푸시한 뒤 CI 가 초록으로 끝난다 (첫 실행의 수정 포함).
- 태그 푸시·릴리스 공개는 사용자 확인 뒤에 한다.

## 11. 범위 밖

설치 프로그램(.exe/.msi), exe 안에 웹 파일 내장, 자동 업데이트, 코드 서명, macOS·Linux.
