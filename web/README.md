# Pulse Universe — 프런트엔드

Pulse Universe 검증 대시보드의 프런트엔드다. C++ 엔진이 WebSocket 으로 1Hz 로
보내는 JSON 스냅샷을 받아 실시간으로 보여준다.

## 레이어 구조

```
protocol/schema.ts        zod 스키마와 타입, parseMessage
stream/SystemStream.ts    순수 클래스, 소켓·타이머를 주입받는다; 재연결 담당; React 를 모른다
stream/useSystemStream.ts stream/ 을 React 에 연결하는 유일한 다리, 실제 WebSocket 을 어댑팅한다
state/snapshotStore.ts    Zustand: previous, current, arrivedAt, intervalMs, status, lifecycleLog
state/interpolator.ts     sample(input, now) — 순수 함수, React 도 Zustand 도 모른다
state/useInterpolated.ts  대시보드를 위해 100ms 마다 sample() 을 호출한다
dashboard/                React 컴포넌트; state/ 만 읽는다
main.tsx                  스트림을 시작하는 루트 래퍼, App 을 렌더링한다
```

`dashboard/` 는 `stream/` 을 직접 참조하지 않는다 — 스트림 상태는 항상
`state/` 를 거쳐서만 들어온다. 이 경계는 `.oxlintrc.json` 의
`no-restricted-imports` 규칙으로도 강제된다.

두 가지 규칙은 어디서나 지킨다: `cpu_pct` 값은 `number | null` 이고 **`null`
(모름)은 절대 `0`(측정된 0)으로 뭉개지지 않는다**. 그리고 프런트엔드는 엔진에
아무것도 보내지 않는다.

## 개발

```
pulse-engine --serve
```

로 엔진을 띄운 뒤, 이 디렉터리에서

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
npm run lint
```

`npm test` 는 `tests/schema.test.ts` 는 `node` 환경에서, 나머지는 `jsdom`
환경에서 실행한다 (`vite.config.ts` 의 `projects` 설정). 출력에 React key
경고나 `act()` 경고가 남으면 안 된다.
