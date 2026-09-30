# Pulse Universe — 프런트엔드

Pulse Universe 의 프런트엔드다. C++ 엔진이 WebSocket 으로 1Hz 로 보내는 JSON
스냅샷을 받아, 프로세스 그룹과 CPU 코어를 천체로 그리는 3D 우주와 숫자 검증
대시보드로 보여준다. 기본 화면은 우주이고 `D` 키나 우측 상단 버튼(`#dashboard`)으로
대시보드와 오간다. 프로젝트 전체 개요와 실행 방법은 [루트 README](../README.md) 에 있다.

## 레이어 구조

```
protocol/schema.ts        zod 스키마와 타입, parseMessage
stream/SystemStream.ts    순수 클래스, 소켓·타이머를 주입받는다; 재연결 담당; React 를 모른다
stream/useSystemStream.ts stream/ 을 React 에 연결하는 유일한 다리, 실제 WebSocket 을 어댑팅한다
state/snapshotStore.ts    Zustand: previous, current, arrivedAt, intervalMs, status, lifecycleLog
state/interpolator.ts     sample(input, now) — 순수 함수, React 도 Zustand 도 모른다
state/useInterpolated.ts  대시보드를 위해 100ms 마다 sample() 을 호출한다
visual/                   순수 TS: 매핑, 배치, 프레임 캐시, 존재 추적, lifecycle 소비, 버스트·궤도·카메라,
                          코어 고리·색·노이즈, 흐름 추적기·곡선·입자, 후처리 상수, 성능 계산
scene/                    R3F: 프레임당 sample() 한 번 → 존재 추적기 → 노드·위성·코어·흐름·입자가 key 로 읽는다.
                          PostEffects 가 심도(Focus 중에만)·Bloom·ACES 를 한 번씩 거치게 한다
dashboard/                React 컴포넌트; state/ 만 읽는다
shell/                    URL 해시로 우주와 대시보드 중 하나만 마운트한다
main.tsx                  스트림을 시작하는 루트 래퍼, Shell 을 렌더링한다
```

`dashboard/` 와 `scene/` 은 `stream/` 을 직접 참조하지 않는다 — 스트림 상태는
항상 `state/` 를 거쳐서만 들어온다. `visual/` 은 React·three·Zustand 를 모르는
순수 모듈이다. 이 경계들은 `.oxlintrc.json` 의 `no-restricted-imports` 규칙으로
강제된다. 장면의 프레임 값(보간된 CPU·메모리, 위치, 맥박)은 React 상태를 거치지
않고 `useFrame` 안에서 ref 로 바뀐다.

천체를 클릭하면 카메라가 다가가고 자식 프로세스가 위성으로 펼쳐진다. `Esc` 나 빈 곳 클릭으로 돌아온다. GSAP 은 이 전환의 진행도 하나만 움직이고, 형성·페이드·붕괴는 `visual/presence.ts` 가 시각으로 계산한다.

키: `D` 대시보드 전환, `Esc` 초점 해제, `P` 성능 표시(프레임 시간·fps·dpr·draw call·삼각형), `H` 범례 접기·펴기(상태는 `localStorage['pulse.legend']`). 가운데 별에 올리면 시스템 요약 툴팁이 뜨고, 가장 안쪽 궤도의 큰 천체는 이름표가 항상 보인다. 별을 클릭하면 빈 곳을 누른 것처럼 초점이 풀린다. 단축키 판정(IME·자동 반복·입력창 규칙)은 `shell/shortcut.ts` 하나로 모여 있다.

흐름 선(`flows[]`)은 `source` 마다 선명도만 달리 그린다 — `measured` 는 선명하게, `estimated` 는 흐리게. 사라진 선은 2.5 초에 걸쳐 흐려진다. 코어의 `flows[].core` 는 인덱스가 아니라 **id** 라서 `coreIndexById` 로 자리를 찾는다.

장면은 선형 HDR 버퍼에 그려진다. 새 재질은 선형 출력을 가정해야 하고(톤 매핑은 PostEffects 한 곳에서만 한다), 색을 직접 넣을 때는 `Color.setRGB(r, g, b, SRGBColorSpace)` 로 sRGB 값을 선형으로 바꿔 넣는다. 해상도(dpr)는 `PerformanceMonitor` 가 정하며 값은 `Universe` 의 React 상태다 (R3F 는 Canvas 가 렌더될 때마다 dpr prop 으로 되돌린다).

두 가지 규칙은 어디서나 지킨다: `cpu_pct` 값은 `number | null` 이고 **`null`
(모름)은 절대 `0`(측정된 0)으로 뭉개지지 않는다**. 그리고 프런트엔드는 엔진에
아무것도 보내지 않는다.

## 개발

```
pulse-engine --serve
```

로 엔진을 띄운 뒤 (관리자 권한이면 흐름이 실측이 된다 — 루트 README 참고), 이 디렉터리에서

```
npm run dev
```

`.env.development` 가 클라이언트를 `ws://127.0.0.1:9000` 으로 향하게 한다.

## 프로덕션

```
npm run build
```

로 `dist/` 를 만든 다음 `engine/` 디렉터리에서

```
pulse-engine --serve --web-root ..\web\dist
```

를 실행하고, 콘솔에 출력되는 `http://` 주소를 연다.

## 테스트와 린트

```
npm test
npm run typecheck
npm run lint
```

`npm test` 는 `tests/schema.test.ts` 와 `tests/visual/` 을 `node` 환경에서,
나머지는 `jsdom` 환경에서 실행한다 (`vite.config.ts` 의 `projects` 설정).
`visual/` 의 테스트가 DOM 없이 도는 것 자체가 그 모듈이 순수하다는 증거다.
출력에 React key 경고나 `act()` 경고가 남으면 안 된다.

`npm run typecheck` 는 앱과 테스트 파일을 모두 타입체크한다 (`tsconfig.test.json`).

3D 장면 자체는 자동 테스트 대상이 아니다. 엔진에 붙여 브라우저에서 확인한다.
콘솔의 `THREE.Clock: This module has been deprecated` 경고는 R3F 내부에서
나오는 것이다.
