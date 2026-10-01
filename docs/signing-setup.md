# 코드 서명 설정 (소유자가 할 일)

이 저장소는 [SignPath Foundation](https://signpath.org/) 의 오픈소스 무료 서명을 받을 수 있게 준비되어 있다. 신청·승인·SignPath 쪽 설정과 GitHub 비밀값은 저장소 소유자만 할 수 있다. 아래 순서로 한다. 서명이 켜지기 전에도 릴리스는 지금처럼 서명 없이 나간다.

## 1. 신청

1. 저장소에 `LICENSE`(MIT)와 README 의 "코드 서명 정책" 절이 있는지 확인한다. README 의 팀 역할이 실제와 맞는지 고친다.
2. GitHub 과 SignPath 계정에 다단계 인증을 켠다 (조건이다).
3. <https://signpath.org/apply> 에서 신청한다. 승인에는 시간이 걸린다. 릴리스마다 수동 승인을 요구하는 것이 조건이다.

## 2. 승인 뒤: SignPath 쪽

1. 프로젝트를 만든다. 슬러그를 적어 둔다 (예: `pulse-universe`).
2. 아티팩트 설정을 만들고 `signpath/artifact-configuration.xml` 의 내용을 붙여 넣는다. 슬러그를 적어 둔다 (예: `initial`).
3. 서명 정책 `release-signing` 을 만든다. 수동 승인(approval)을 켠다. 슬러그를 적어 둔다.
4. 신뢰 빌드 시스템 `GitHub.com` 을 프로젝트에 연결한다 (기본 커넥터 `https://pipelineconnector.connectors.signpath.io/GitHub/GitHubCom`).
5. SignPath GitHub 앱을 설치하고 이 저장소 접근을 허용한다 (빌드 출처 검증용).
6. 사용자 설정에서 API 토큰을 만든다.

## 3. GitHub 쪽 (저장소 Settings → Secrets and variables → Actions)

- 비밀(Secrets): `SIGNPATH_API_TOKEN` = 위의 API 토큰.
- 변수(Variables): `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`, `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG`.
- `SIGNPATH_ORGANIZATION_ID` 가 비어 있지 않으면 릴리스 워크플로가 서명 단계를 켠다. 이 변수를 지우면 다시 서명 없이 릴리스한다.

## 4. 첫 서명 릴리스

1. 태그를 푸시한다 (예: `git tag v0.3.0 && git push origin v0.3.0`).
2. 워크플로가 서명 요청을 올리고 기다린다. SignPath 에서 요청을 확인하고 승인한다.
3. 워크플로가 끝나면 Releases 의 `pulse-engine.exe` 속성 → 디지털 서명에서 게시자가 `SignPath Foundation` 인지 확인한다. 워크플로는 서명이 `Valid` 가 아니면 릴리스를 올리지 않는다.
4. 워크플로가 서명본을 찾지 못하거나(`expected exactly one signed pulse-engine.exe`) 서명 단계가 실패하면 로그를 보고 `output-artifact-directory` 에 무엇이 내려왔는지 확인해 워크플로의 `Assemble` 단계를 고친다. 서명본의 폴더 구성은 첫 실제 실행에서 확인되는 부분이다.

참고: 서명을 해도 SmartScreen 은 다운로드가 쌓여 평판이 생길 때까지 경고할 수 있다. 서명하면 게시자가 `Unknown publisher` 대신 표시된다.
