# 코드 서명 준비 설계

- 작성일: 2026-10-01
- 상태: 초안 (사용자 검토 대기, 시제품 확인 전)
- 선행: 릴리스 zip, 웹 내장 exe (`v0.2.0`)
- 범위: SignPath Foundation 의 오픈소스 무료 서명을 받을 수 있도록 저장소와 릴리스 파이프라인을 준비한다. 신청·승인·SignPath 쪽 설정은 저장소 소유자가 해야 하므로 범위 밖이고, 그 절차를 체크리스트로 남긴다. 다른 서명 서비스, 설치 프로그램, 서명 인증서 구매는 범위 밖이다.

## 1. 문제

배포한 exe 는 서명이 없어서 Windows SmartScreen 이 처음 실행할 때 경고한다. 무료 서명(SignPath Foundation)을 받으려면 조건이 있다 (signpath.org/terms 확인, 2026-10-01).

| 조건 | 지금 |
|---|---|
| OSI 승인 오픈소스 라이선스 | `LICENSE` 없음 |
| 서명 대상 바이너리에 제품 이름·버전 메타데이터 | exe 에 버전 리소스 없음 |
| 홈페이지에 서명 정책: 출처 문구, 팀 역할, 개인정보 문구 | 없음 |
| 소스와 빌드 스크립트에서 검증 가능하게 CI 로 빌드 | 있음 (GitHub Actions) |
| 릴리스마다 수동 승인 | SignPath 쪽 서명 정책에서 설정 |
| 제거 방법 제공, 시스템 설정을 경고 없이 바꾸지 않음 | 폴더 삭제로 제거, 설치 없음 |
| 팀원 다단계 인증 | 소유자가 SignPath·GitHub 에서 설정 |

또 지금 `package.ps1` 은 exe 를 빌드한 뒤 곧바로 zip·해시를 만든다. 서명은 빌드된 exe 를 서비스에 보내 서명본을 받아야 하므로, 서명본으로 zip·해시를 만들려면 그 사이에 끼울 자리가 필요하다.

## 2. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D80 | `LICENSE` 는 MIT, 저작권자 `Rafdidas` (GitHub 계정 이름), 연도 2026 | 사용자가 정하지 않으면 가장 단순하고 널리 쓰이는 OSI 승인 라이선스다. 실명이 필요하면 소유자가 바꾼다 |
| D81 | exe 에 `VERSIONINFO` 리소스를 넣는다. 제품명 `Pulse Universe`, 버전은 CMake 옵션 `PULSE_VERSION` (기본 `0.0.0`) | SignPath 조건. 패키징 때 태그 버전이 들어간다 |
| D82 | `package.ps1` 에 `-Phase All|Build|Assemble` (기본 `All`) 을 둔다. `Build` 는 서명 전 exe 까지, `Assemble` 은 넘겨받은 exe(`-ExePath`)로 zip·해시를 만든다 | 서명 단계를 사이에 끼운다. 기본값은 지금과 같아서 로컬 사용이 달라지지 않는다 |
| D83 | 릴리스 워크플로의 서명 단계는 저장소 변수 `SIGNPATH_ORGANIZATION_ID` 가 있을 때만 돈다. 없으면 서명 없이 지금처럼 릴리스한다 | 승인 전에도 워크플로가 깨지지 않는다. 승인 뒤 변수만 넣으면 켜진다 |
| D84 | 서명을 켠 릴리스는 `Assemble` 에 `-RequireSignature` 를 줘서 서명이 `Valid` 가 아니면 실패한다 | 서명 없는 exe 가 서명된 릴리스로 나가지 않는다 |
| D85 | README 에 서명 정책 절을 추가한다 (출처 문구, 팀 역할, 개인정보 문구). 설정 절차는 `docs/signing-setup.md` 체크리스트로 둔다 | SignPath 조건과 소유자가 할 일을 분리해 기록한다 |

## 3. 버전 리소스 (D81)

- `engine/resources/version.rc.in`: `#include <winver.h>` 후 `VS_VERSION_INFO VERSIONINFO` — `FILEVERSION @PULSE_VERSION_MAJOR@,@PULSE_VERSION_MINOR@,@PULSE_VERSION_PATCH@,0`, `StringFileInfo` (언어 0409, 코드페이지 04B0): `ProductName` = `Pulse Universe`, `FileDescription` = `Pulse Universe engine`, `ProductVersion`/`FileVersion` = `@PULSE_VERSION@`, `OriginalFilename` = `pulse-engine.exe`, `InternalName` = `pulse-engine`.
- `engine/CMakeLists.txt`: 캐시 변수 `PULSE_VERSION` (STRING, 기본 `0.0.0`). `^[0-9]+\.[0-9]+\.[0-9]+$` 에 맞지 않으면 `FATAL_ERROR`. 세 숫자를 분해해 `configure_file` 로 `version.rc` 를 만들고 `pulse-engine` 타깃의 소스에 **항상** 더한다 (웹 pak 과 달리 개발 빌드에도 들어가도 무해하다).
- `package.ps1`: `-Version` 이 `x.y.z` 형태면 `-DPULSE_VERSION=<버전>` 을 넘기고, 아니면(`dev` 등) 기본값을 쓴다.

## 4. 패키징 단계 (D82, D84)

`package.ps1` 매개변수: `-Phase All|Build|Assemble` (기본 `All`), `-ExePath <파일>` (`Assemble` 에서 필수), `-RequireSignature` (`Assemble` 에서만).

| 단계 | 하는 일 | 산출물 |
|---|---|---|
| `Build` | 웹 빌드, pak, 엔진 정적 빌드, 정적 링크 검사, 프로브 | `<OutDir>/unsigned/pulse-engine.exe` |
| `Assemble` | `-ExePath` 의 exe 로 프로브를 다시 하고, `-RequireSignature` 이면 `Get-AuthenticodeSignature` 상태가 `Valid` 인지 확인한 뒤 zip·exe·`.sha256` 조립 | 지금의 릴리스 파일 4 개 |
| `All` | `Build` 후 `unsigned` 의 exe 로 `Assemble` (서명 요구 없음) | 지금과 같다 |

- 프로브(`--serve --embedded-web`)는 `Assemble` 에서도 돌아서, 서명이 exe 의 리소스를 깨뜨리지 않았는지 확인한다. 프로브는 `web/dist` 의 자산 파일 이름이 필요하므로 같은 작업에서 `Build` 가 끝난 뒤여야 한다 (릴리스 워크플로는 한 작업이다).
- 산출 파일 이름·내용·sha256 형식은 변하지 않는다.

## 5. 릴리스 워크플로 (D83)

기존 `Build the exe and the zip` 단계를 아래로 나눈다.

1. `package.ps1 -Version $version -SkipNpmCi -Phase Build`
2. (서명 켜짐) `actions/upload-artifact` 로 `dist-release/unsigned/pulse-engine.exe` 업로드 — 아티팩트 id 를 얻는다.
3. (서명 켜짐) `signpath/github-action-submit-signing-request` 로 서명 요청, `wait-for-completion: true`, 서명본을 `dist-release/signed` 에 받는다. 입력은 저장소 변수 `SIGNPATH_ORGANIZATION_ID`·`SIGNPATH_PROJECT_SLUG`·`SIGNPATH_SIGNING_POLICY_SLUG`·`SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` 와 비밀 `SIGNPATH_API_TOKEN`.
4. `package.ps1 -Version $version -SkipNpmCi -Phase Assemble -ExePath <서명본 또는 unsigned exe>` (서명 켜짐이면 `-RequireSignature`).
5. 엔진 테스트, 게시는 지금과 같다 (자산 4 개 검사 포함).

서명 켜짐 조건은 `vars.SIGNPATH_ORGANIZATION_ID != ''`. 서명 요청은 SignPath 쪽 정책에 따라 수동 승인을 기다리고, 그동안 작업은 대기한다.

서명본의 압축 풀림 위치와 형식(SignPath 액션이 `output-artifact-directory` 에 무엇을 놓는지)은 첫 실제 서명 실행에서 확인한다. 워크플로는 그 폴더 아래에서 `pulse-engine.exe` 를 재귀로 찾고, 정확히 하나가 아니면 실패한다.

## 6. SignPath 쪽 파일과 문서 (D85)

- `signpath/artifact-configuration.xml`: SignPath 프로젝트의 아티팩트 설정에 붙여 넣을 원본. `upload-artifact` 가 파일을 zip 으로 감싸므로 `<zip-file>` 안의 `*.exe` 를 `<authenticode-sign/>` 한다 (네임스페이스 `http://signpath.io/artifact-configuration/2023-12`).
- `docs/signing-setup.md`: 소유자 체크리스트 — (1) `LICENSE`·README 정책이 올라간 것을 확인하고 signpath.org/apply 로 신청, (2) 승인 뒤 프로젝트·아티팩트 설정·`release-signing` 서명 정책(수동 승인 켬) 만들기, (3) 신뢰 빌드 시스템 `GitHub.com` 연결과 SignPath GitHub 앱 설치, (4) 저장소에 비밀 `SIGNPATH_API_TOKEN` 과 변수 네 개 추가, (5) 태그 푸시 후 SignPath 에서 승인, (6) 결과 exe 의 서명 확인. 두 서비스 계정에 다단계 인증을 켠다.
- `README.md`: 새 절 "코드 서명 정책" — `Free code signing provided by SignPath.io, certificate by SignPath Foundation` 문구, 팀 역할(Committers·reviewers·approvers 모두 `Rafdidas`; 소유자가 실제에 맞게 고친다), 개인정보 문구(`This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.` — 이 엔진은 127.0.0.1 에서만 듣고 아무것도 보내지 않는다). 서명 승인 전에는 "신청 중"이라고 쓰지 않고 문구 위에 "서명은 SignPath Foundation 승인 뒤부터 적용된다" 를 덧붙인다.

## 7. 실패 동작

| 상황 | 동작 |
|---|---|
| `-Version` 이 `x.y.z` 가 아님 (`dev`) | 버전 리소스는 `0.0.0` |
| `PULSE_VERSION` 형식 오류 | CMake `FATAL_ERROR` |
| `Assemble` 인데 `-ExePath` 없음·없는 파일 | 스크립트 오류 |
| `-RequireSignature` 인데 서명이 없거나 `Valid` 아님 | 스크립트 오류, 릴리스 안 올라감 |
| 서명 켜짐인데 서명본이 없거나 둘 이상 | 워크플로 실패 |
| SignPath 승인 거절·시간 초과 | 서명 단계 실패 → 릴리스 안 올라감 (서명 없는 exe 로 내려가지 않는다) |
| 서명 꺼짐 (변수 없음) | 서명 없이 지금처럼 릴리스 |

## 8. 테스트

- `version.rc` 가 들어간 exe: `(Get-Item exe).VersionInfo` 의 `ProductName`·`FileVersion`·`ProductVersion` 이 기대값. 버전 `1.2.3` 과 기본(`dev` → `0.0.0`) 모두.
- `package.ps1`: `-Phase Build` 후 `-Phase Assemble -ExePath unsigned` 의 산출물이 `-Phase All` 과 같은 파일 이름과 구조 (zip 안 파일 목록, sha256 형식). 서명 없는 exe 에 `-RequireSignature` 를 주면 실패해야 한다 (음성 시험). `Assemble` 에서 `-ExePath` 가 없으면 실패.
- 워크플로 YAML 이 파싱되고, 서명 단계에 `if: vars.SIGNPATH_ORGANIZATION_ID != ''` 가 있고, 서명이 꺼진 경로가 `unsigned` exe 를 쓰는지 읽어서 확인.
- CMake: `PULSE_VERSION` 형식 오류가 구성을 실패시키는지, 개발 빌드(기본값)가 경고 없이 빌드되고 엔진 테스트가 그대로 통과하는지.
- 서명이 `Valid` 인 경로는 실제 SignPath 서명 전에는 시험할 수 없다. 첫 서명 릴리스에서 `-RequireSignature` 통과와 서명 정보(게시자 `SignPath Foundation`)를 확인한다 — 이 항목은 소유자 체크리스트의 마지막 단계다.

## 9. 완료 조건

- `LICENSE`(MIT), README 서명 정책 절, `docs/signing-setup.md`, `signpath/artifact-configuration.xml` 이 있다.
- exe 파일 속성에 제품명과 버전이 보인다.
- `package.ps1` 의 세 단계가 위 시험을 통과하고, 기본(`All`) 산출물은 이전과 같다.
- 릴리스 워크플로가 서명 켜짐/꺼짐 두 경로를 갖고, 꺼짐이 기본이다. 변수를 넣으면 켜진다.
- 서명 승인 전에도 새 태그로 서명 없는 릴리스가 지금처럼 나간다.

## 10. 범위 밖

SignPath 신청·승인·계정 설정, 서명 인증서 구매, 다른 서명 서비스(Azure Artifact Signing 등), 설치 프로그램·MSI 서명, 서명 타임스탬프 설정(서비스가 처리한다), 서명된 zip(zip 자체는 서명 대상이 아니다).
