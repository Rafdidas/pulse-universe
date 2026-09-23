# M3 — 프론트엔드 데이터 레이어와 검증 대시보드 설계

- 작성일: 2026-09-23
- 상태: 승인 대기
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M2 `2026-09-23-m2-transport-design.md`, M1·M2 구현 (`main`)
- 범위: WebSocket 수신 → 검증 → 보간 → 숫자 대시보드. 엔진의 정적 파일 서빙까지.

## 1. 이 문서의 위치

M1은 수집 엔진을, M2는 직렬화와 WebSocket 전송을 만들었다. `pulse-engine --serve` 가 1초마다 계약서 4.4절 형식의 스냅샷을 내보내고 있다. M3은 **Layer 3(프론트엔드 데이터 레이어)** 과 그 위의 검증용 대시보드를 만든다.

계약서 7절이 데이터 레이어의 설계를 이미 담고 있다. 이 문서는 그것을 구현 가능한 수준으로 구체화하고, M2 구현에서 드러난 사실들을 반영한다.

M4가 이 위에 Three.js 시각화를 올린다. **이 문서의 설계 결정 대부분은 M4를 위한 것이다** — 특히 5절의 보간 구조가 그렇다.

## 2. 측정: 프론트엔드가 실제로 받는 것

설계 전에 실제 페이로드를 떴다 (본 개발 PC, `--max-groups 40`):

```
26,132 bytes
그룹 40   자식 120   코어 28   flows 8
프로세스 418   스레드 8,581
system.cpu_pct 15.75        ← 숫자
groups[0].cpu_pct 4.78
```

보간 대상인 연속값의 개수:

```
그룹 cpu+mem      80
자식 cpu+mem     240
코어 pct          28
                ----
합계             348
```

60 fps에서 초당 약 2만 번의 선형 보간이다. **계산량은 문제가 아니다.** 문제는 그 값을 어디에 두느냐이며, 5절이 그것을 다룬다.

## 3. 범위 조정

계약서 12절은 정적 파일 서빙을 M3에 배정했다. 유지하되 **M3의 마지막 단계**로 둔다.

정적 서빙은 C++ 작업이고 나머지는 전부 TypeScript다. 순서를 뒤집으면 서빙할 대상이 없어 검증할 수 없다. 프론트엔드가 빌드되는 시점에 붙이면 M3이 "exe 하나가 전부 서빙한다" 로 끝나고, 그 자체가 인수 조건이 된다.

## 4. 확정된 결정

계약서 D1~D6, M2 스펙 D7~D11에 이어 번호를 매긴다.

| # | 결정 | 선택 | 근거 |
|---|---|---|---|
| D12 | 스키마 검증 | zod | 스키마 하나에서 타입과 검증이 함께 나온다. 타입 선언과 검증 코드가 갈라질 수 없다 |
| D13 | 보간의 위치 | React 상태 **밖**. 순수 모듈 | 5절 참조. M4의 R3F가 `useFrame` 안에서 읽어야 한다 |
| D14 | 대시보드 렌더 주기 | 100 ms 간격 (10 Hz) | DOM은 60 fps가 필요 없다. 보간기는 60 fps를 낼 수 있지만 소비자가 주기를 정한다 |
| D15 | 대시보드 범위 | 계약의 전 필드 + 원시 JSON 토글 | 존재 이유가 숫자 검증이다 |
| D16 | 재연결 정책 | close code 1000과 1006을 다르게 취급 | 6.3절 참조 |
| D17 | 테스트 픽스처 | 엔진에서 실제로 뜬 페이로드를 저장소에 넣는다 | 손으로 만든 가짜가 아니라 진짜 출력에 대해 검증한다 |

### 4.1 되돌리기 쉬운 것과 어려운 것

- **쉬움**: 대시보드 렌더 주기, 백오프 상수, 대시보드 구성.
- **어려움**: D13. 보간을 React 상태에 넣었다가 빼려면 M4의 모든 컴포넌트를 다시 써야 한다.

## 5. 보간은 React를 모른다

이 절이 M3에서 가장 중요하다.

### 5.1 문제

348개 연속값을 React 상태에 넣고 60 fps로 갱신하면 매 프레임 컴포넌트 트리 전체가 다시 렌더된다. M3의 DOM 대시보드에서도 낭비지만, **M4에서는 잘못된 구조다.** R3F의 올바른 패턴은 `useFrame` 안에서 값을 읽어 Three.js 객체의 속성을 직접 수정하는 것이고, React 상태를 거치면 안 된다.

### 5.2 구조

보간기는 React·Zustand·Three.js를 전부 모르는 순수 모듈이다.

```
snapshotStore          최신 + 직전 스냅샷, 그리고 최신이 도착한 시각
      ↓
interpolator.sample(nowMs)  →  InterpolatedSnapshot (평범한 객체)
      ↓                              ↓
M3 대시보드 (약 10 Hz)          M4 시각화 (useFrame, 60 fps)
```

`sample()` 은 호출될 때마다 계산해 돌려줄 뿐 아무것도 구독하지 않는다. 호출 주기는 소비자가 정한다.

M3의 대시보드는 `useInterpolated(hz)` 훅으로 소비한다. 훅이 `setInterval` 로 100 ms마다 `sample(performance.now())` 을 호출해 React 상태를 한 번 갱신한다. **React 상태에 들어가는 것은 보간기의 출력이지 보간 과정이 아니다** — 그 구분이 D13의 핵심이고, M4는 같은 `sample()` 을 `useFrame` 안에서 상태 없이 호출한다.

### 5.3 보간 규칙

`alpha = clamp((now - arrivedAt) / interval_ms, 0, 1)`

- **연속값**(`system.cpu_pct`, `groups[].cpu_pct`, `groups[].mem_mb`, `children[].cpu_pct`, `children[].mem_mb`, `cores[].pct`)은 직전 값에서 현재 값으로 선형 보간한다.
- **이산값**(`proc_count`, `thread_count`, `account`, `image_path`, 이름, pid, `flows`, `lifecycle`, `ambient`)은 현재 스냅샷 값을 그대로 통과시킨다.
- **대응 규칙**: 그룹은 `key` 로, 코어는 `id` 로 짝짓는다. 자식은 **짝지어진 그룹 안에서** `pid` 로 짝짓는다 — pid는 전역적으로 유일하지만 자식 목록은 그룹에 중첩돼 있으므로, 전체를 훑어 찾으면 그룹이 바뀐 프로세스에서 엉뚱하게 대응된다.
- **새로 나타난 항목**은 보간하지 않고 현재 값에서 시작한다. 0에서 자라 올라오면 실제보다 작게 보이는 순간이 생긴다.
- **사라진 항목**은 결과에서 빠진다. 페이드아웃은 시각화의 일이지 보간기의 일이 아니다.
- **`null` 이 섞이면 보간하지 않는다.** 한쪽이 `null` 이면 현재 값을 그대로 쓴다. `null`(모름)과 `0.0`(측정했고 0)은 다른 값이고, 그 사이를 보간하는 것은 의미가 없다.
- **`seq` 불연속**(`cur.seq !== prev.seq + 1`)이면 보간을 리셋하고 현재 값으로 즉시 점프한다. 계약서 4.5절이 정한 동작이다.
- 스냅샷이 아직 없으면 `sample()` 은 `null` 을 돌려준다.

`alpha` 가 1에서 고정되는 것은 정상이다. 다음 스냅샷이 늦으면 값이 멈춘 채 유지되며, 이는 없는 데이터를 추정해 만들어내는 것보다 낫다.

## 6. 연결 관리

계약서 7.2절을 구현한다.

### 6.1 엔드포인트 탐색

계약서 4.1절 그대로다.

```
VITE_PULSE_WS_URL 가 정의되어 있으면  → 그 값
없으면                               → window.location 에서 유도
```

개발 중에는 Vite가 5173, 엔진이 9000에 있으므로 `VITE_PULSE_WS_URL=ws://127.0.0.1:9000` 을 `.env.development` 에 둔다. 배포 빌드는 엔진이 프론트엔드를 서빙하므로 같은 origin이 되어 환경변수가 필요 없다.

### 6.2 상태

```
connecting → open → closed → connecting → ...
                  ↘ version-mismatch  (재연결하지 않는다)
```

`hello` 를 받으면 거기 담긴 `interval_ms`, `core_count`, `host` 를 보관한다. `interval_ms` 는 보간기의 분모가 되므로 하드코딩하지 않는다.

`host.elevated` 가 `false` 면 대시보드에 안내를 띄운다. 비권한 실행에서는 보호된 프로세스의 메모리가 0으로 남는다는 것을 M1에서 확인했고, 그걸 모르면 숫자가 틀린 것처럼 보인다.

### 6.3 재연결

M2에서 종료 핸드셰이크를 구현해 두 경우를 구분할 수 있다.

| close code | 의미 | 정책 |
|---|---|---|
| 1000 | 서버가 정상 종료했다 | 2초 고정 간격 재시도. 개발자가 다시 띄우면 곧바로 붙는다 |
| 1006 등 | 연결이 끊겼다 | 지수 백오프 500ms → 8초 상한, 지터 포함 |

구분하는 이유는 이렇다. 1000은 엔진이 꺼진 것이므로 곧 다시 켜질 가능성이 높고 빠른 복귀가 유용하다. 1006은 원인을 모르므로 두드리지 않는다.

### 6.4 나쁜 메시지

- `v` 가 1이 아니면 소켓을 닫고 `version-mismatch` 로 간다. **재연결하지 않는다** — 다시 붙어도 같은 버전이 온다. 대시보드가 사용자에게 알린다.
- zod 검증에 실패한 메시지는 **버리고 연결은 유지한다.** 한 프레임 때문에 스트림을 끊지 않는다. 누적 개수를 상태에 남겨 대시보드가 보여준다.
- 계약서 4.2절대로 클라이언트는 아무것도 보내지 않는다.

## 7. 프로토콜 타입

`web/src/protocol/schema.ts` 가 zod 스키마 하나로 타입과 검증을 동시에 낸다.

```ts
export const SnapshotSchema = z.object({ ... });
export type Snapshot = z.infer<typeof SnapshotSchema>;
```

필드 이름은 계약서 4.3~4.5절과 철자까지 같아야 한다. 엔진의 `core/Snapshot.h` 와 `network/Serializer.cpp` 가 같은 이름을 쓴다.

`cpu_pct` 계열은 `z.number().nullable()` 이다. `null` 과 `0` 을 구분하는 것이 이 프로젝트의 반복되는 주제다.

### 7.1 픽스처

`web/tests/fixtures/snapshot.json` 에 **엔진에서 실제로 뜬 페이로드**를 넣는다 (`pulse-engine --json --max-groups 40`). 손으로 만든 가짜가 아니라 진짜 출력에 대해 스키마를 검증한다. 엔진이 필드를 바꾸면 이 테스트가 깨진다 — 그게 목적이다.

**픽스처에서 사용자명을 반드시 치환한다.** 실측 확인 결과, 그룹 40개 전부에 `image_path` 가 채워져 있고 그중 **13개가 사용자 홈 아래 경로**다.

```
C:\Users\<사용자명>\AppData\Local\Programs\Microsoft VS Code\Code.exe
C:\Users\<사용자명>\AppData\Local\Figma\app-126.9.10\Figma.exe
```

저장소는 공개돼 있다. 픽스처를 넣기 전에 `C:\Users\<사용자명>\` 을 `C:\Users\testuser\` 로 일괄 치환하고, 치환 후 파일에 사용자명이 남아 있지 않은지 확인한다. 경로의 모양은 그대로 유지되므로 스키마 검증의 가치는 줄지 않는다.

## 8. 대시보드

존재 이유는 숫자 검증이다. M4에서 화면이 이상할 때, 데이터가 맞는지 먼저 확인할 수 있어야 한다. 예쁠 필요는 없고 전부 보여야 한다.

| 영역 | 내용 |
|---|---|
| 연결 배지 | 상태, `seq`, `interval_ms`, `core_count`, `host.os`, `host.elevated`, 버려진 메시지 수 |
| 시스템 바 | `cpu_pct`, `mem_used_mb / mem_total_mb`, `process_total`, `thread_total` |
| 코어 그리드 | 코어 28개, id 순, 각 `pct` |
| 그룹 표 | 40행. `name`, `root_pid`, `proc_count`, `cpu_pct`, `mem_mb`, `thread_count`, `account`. 행을 펼치면 자식 목록 |
| Flows | `group` → `core`, `weight`, `source` |
| 생명주기 로그 | `spawned` / `terminated` 를 도착 순으로 누적. 최근 50개 |
| Ambient | `service_proc_count`, `service_mem_mb` |
| 원시 JSON | 토글. 마지막 스냅샷 원문 |

생명주기 로그가 특히 중요하다. 프로그램을 켜고 끄는 것이 화면에 즉시 반영되는지는 이 로그로만 확인할 수 있고, M4의 생성·소멸 연출이 그 위에 올라간다.

`cpu_pct` 가 `null` 인 항목은 `-` 로, `0.0` 인 항목은 `0.0` 으로 표시한다. 둘이 같아 보이면 대시보드가 제 역할을 못 하는 것이다.

## 9. 정적 파일 서빙

엔진에 `--web-root <dir>` 를 더한다. 지정되면 WebSocket 업그레이드가 아닌 HTTP 요청에 파일로 응답한다.

- 업그레이드 요청 → 기존 WebSocket 경로.
- 그 외 GET → `web-root` 아래 파일. 없으면 `index.html` 로 대체한다 (SPA 라우팅).
- `--web-root` 가 없으면 업그레이드가 아닌 요청에 426 Upgrade Required 로 답한다. 현재는 아무 응답 없이 연결을 끊는데, 브라우저로 주소를 열어본 개발자가 빈 화면만 보게 된다.
- **경로 탈출을 막는다.** 요청 경로를 정규화해 `web-root` 바깥을 가리키면 403으로 거절한다. 이 프로세스는 관리자 권한으로 도는 일이 많다.
- MIME: `.html .js .css .json .svg .png .woff2`. 그 외는 `application/octet-stream`.

배포 절차는 `npm run build` 후 `pulse-engine --serve --web-root ../web/dist` 이다.

## 10. 모듈 구조

```
web/
  package.json  vite.config.ts  tsconfig.json  .env.development
  src/
    protocol/
      schema.ts        zod 스키마 + 추론된 타입
    stream/
      endpoint.ts      VITE_PULSE_WS_URL 또는 window.location
      useSystemStream.ts  연결·재연결·검증. React 훅
    state/
      snapshotStore.ts    최신 + 직전 + 도착 시각 (Zustand)
      interpolator.ts     sample(now). React 를 모른다
    dashboard/
      App.tsx  ConnectionBadge.tsx  SystemBar.tsx  CoreGrid.tsx
      GroupTable.tsx  FlowList.tsx  LifecycleLog.tsx  RawJson.tsx
    main.tsx
  tests/
    fixtures/snapshot.json
engine/
  src/network/StaticFiles.{h,cpp}   경로 해석 + MIME
  src/network/WebSocketServer.cpp   비업그레이드 요청 분기
```

경계 규칙은 M1·M2와 같다. `interpolator.ts` 는 React를 import하지 않는다. `dashboard/` 는 WebSocket을 모른다. `schema.ts` 는 아무것도 모른다.

## 11. 실패 동작

| 상황 | 동작 |
|---|---|
| 엔진이 떠 있지 않다 | 6.3절 백오프로 재시도. 배지가 상태를 보여준다 |
| 연결 중 엔진이 정상 종료 | 1000 → 2초 간격 재시도 |
| 연결이 끊김 | 1006 → 지수 백오프 |
| `v` 불일치 | 재연결 중단. 배너로 알림 |
| 스키마 위반 | 해당 메시지만 버림. 개수 누적 |
| 경로 탈출 시도 | 403 |
| `--web-root` 경로가 없음 | 기동 시 오류와 종료 코드 2 |

## 12. 테스트 전략

| 대상 | 방식 |
|---|---|
| zod 스키마 | 실제 픽스처가 통과한다. 필드 누락·타입 오류·`v` 불일치가 거부된다 |
| `null` vs `0` | 스키마와 보간기 양쪽에서 구분이 유지되는지 |
| 보간기 | alpha 0/0.5/1, 클램프, `seq` 불연속 리셋, 항목 추가·삭제, `null` 혼합, 스냅샷 없음 |
| 엔드포인트 탐색 | 환경변수 있음/없음 두 경로 |
| `useSystemStream` | 가짜 WebSocket 주입. 재연결, 1000과 1006의 백오프 차이, 버전 불일치 시 재연결 중단, 나쁜 메시지 폐기 |
| 대시보드 | `null` 이 `-` 로, `0` 이 `0.0` 으로 나오는지. 렌더 스모크 |
| 정적 서빙 | Catch2. 경로 해석·탈출 거절·MIME·index 대체·426 |

프론트엔드는 Vitest를 쓴다. `useSystemStream` 테스트는 실제 소켓을 열지 않고 가짜를 주입한다 — 엔진 없이도 돌아야 한다.

## 13. 범위 밖

- Three.js, R3F, 셰이더, 파티클, GSAP 일체 (M4~).
- 노드 배치, Force 시뮬레이션.
- 디자인. 대시보드는 검증 도구이고 최종 UI가 아니다.
- 인증, TLS, 원격 접속.
- 프론트엔드에서 엔진으로 보내는 명령 (계약서 4.2절이 금지).

## 14. 구현 순서

| # | 단계 | 완료 시 확인 가능한 것 |
|---|---|---|
| F1 | Vite + React + TS 골격, zod 스키마, 실제 픽스처 | 픽스처가 스키마를 통과한다. 필드를 지우면 실패한다 |
| F2 | 엔드포인트 탐색 + `useSystemStream` | 가짜 소켓으로 재연결·백오프·버전 불일치가 검증된다 |
| F3 | `snapshotStore` + `interpolator` | 보간 규칙 전부가 테스트로 고정된다 |
| F4 | 대시보드 | 브라우저에서 실제 엔진에 붙어 숫자가 흐른다 |
| F5 | 엔진 정적 서빙 | `pulse-engine --serve --web-root ../web/dist` 하나로 전부 동작한다 |

F1이 픽스처를 포함하는 이유는, 그 뒤의 모든 단계가 진짜 페이로드에 대해 테스트되게 하기 위해서다.

## 15. 다음

M4가 계약서 5절의 Three.js 장면을 이 데이터 레이어 위에 올린다. `interpolator.sample()` 을 `useFrame` 안에서 호출하는 것이 그 접점이다. 대시보드는 지우지 않는다 — 화면이 이상할 때 숫자를 먼저 의심할 수 있게 해주는 도구다.
