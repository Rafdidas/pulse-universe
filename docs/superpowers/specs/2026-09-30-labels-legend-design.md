# 이름표·별 정보·범례 설계

- 작성일: 2026-09-30
- 상태: 승인됨 (시제품 확인)
- 선행: 태양계형 배치 스펙 (`2026-09-30-solar-layout-design.md`), M4·M5 스펙, M1~M9·ETW 구현 (`main`)
- 범위: 우주 화면을 처음 보는 사람이 무엇인지 읽을 수 있게 한다 — 가운데 별에 시스템 요약 툴팁, 가장 큰 천체의 이름표, 읽는 법을 알려 주는 범례. 엔진과 계약은 바꾸지 않는다.

## 1. 문제

태양계형 배치로 천체가 겹치지 않게 되었지만, 화면만 봐서는 크기·발광·궤도·선이 무엇을 뜻하는지 알 수 없고, 어느 것이 무슨 프로세스인지는 마우스를 올려 봐야 안다. 가운데 별은 아무 정보도 주지 않고, 별을 가리키면 별 뒤의 안쪽 궤도 천체가 대신 잡힌다 (태양계형 배치 최종 리뷰 M6).

## 2. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D66 | 별이 호버를 받는다 (`Hovered` 에 `{ kind: 'system' }`). 툴팁은 기존 `Tooltip` 을 쓴다 | 이미 있는 호버·툴팁 경로를 그대로 쓰고, 별이 뒤 천체를 가리는 문제도 없어진다 |
| D67 | 이름표는 가장 안쪽 궤도(`layout.plan().rings[0].keys`)의 천체에만 항상 보인다 | 40 개 전부에 띄우면 글자로 덮인다. 안쪽 궤도 = 큰 천체라는 새 배치를 그대로 쓰므로 별도의 순위 계산이 없고, 궤도 안 자리를 key 순으로 고정했으므로 대상이 깜빡이지 않는다 |
| D68 | 범례는 오른쪽 아래 작은 패널. `H` 키로 접고 펼친다. 처음에는 펼쳐 두고, 접으면 `localStorage` 에 기억한다 | 처음 보는 사람은 읽게 하고, 아는 사람은 한 번 접으면 다시 보지 않는다 |
| D69 | 글은 기존 화면과 같은 영어다 | 기존 UI(`dashboard (D)`, `waiting for the first snapshot…`)가 영어다. 한글 표시는 범위 밖 |

## 3. 별 호버 (D66)

- `sceneContext.ts` 의 `Hovered` 에 `{ kind: 'system' }` 를 더한다.
- `SystemStar` 의 본체 메시가 `onPointerOver`·`onPointerOut` 을 받는다 (`raycast` 무시를 뺀다). 후광 메시는 계속 `raycast={() => null}` 이다. 클릭하면 초점을 푼다 (빈 곳을 누른 것과 같다). 별이 클릭을 받으면서 뒤에 있는 천체로 클릭이 새는 것을 막아야 하고, 예전에 가운데가 빈 곳이라 `onPointerMissed` 로 초점이 풀리던 동작도 이어야 한다 (최종 리뷰 I1).
- `Tooltip.labelFor` 가 `system` 을 다룬다: 위치 = 원점, 반지름 = `STAR_RADIUS`, 제목 `System`, 세부 `visual/labels.ts` 의 `systemDetail(system)` (CPU 모름은 `CPU -%`. `formatPct` 와 같은 출력을 내지만 순수 모듈 규칙상 `dashboard/` 를 import 하지 않고 다시 구현한다).
- `systemDetail(system: SystemTotals)`: `CPU 12.3% · Mem 26.3 / 32.5 GB · 406 procs · 8,481 threads`. CPU 가 `null` 이면 `CPU -`. 메모리는 GB 로 소수 한 자리. 숫자는 `dashboard/format.ts` 의 `formatPct` 를 쓴다.
- `FlowStreams.highlightFor` 는 `system` 을 호버 없음처럼 다룬다 (모든 선 밝게). `Satellites` 등 다른 호버 소비자도 `system` 을 무시한다.

## 4. 이름표 (D67)

- `scene/BodyLabels.tsx`: `layout.plan().rings[0]?.keys` 가 바뀔 때(문자열 signature 비교)만 React 상태를 갱신해 `BodyLabel` 을 그린다.
- `BodyLabel(key)`: drei `Html` + 앵커 그룹. 툴팁과 같이 `useFrame`(우선순위 `FRAME_PRIORITY.tooltip`)에서 앵커를 `floatingPosition(layout, key, timeSec)` + 반지름 × 1.25 위로 옮기고, 글자는 DOM 에 직접 쓴다. 존재 추적기에서 `fading-out`·`collapsing` 이면 숨긴다.
- 글자: 그룹 이름. 작은 회색 글씨(툴팁보다 작고 배경 없음), `pointer-events: none`.
- 초점 중(`focus.weight > 0.5`)에는 숨긴다. 호버 중인 천체는 툴팁이 있으므로 그 이름표는 숨긴다.
- 대상 판정은 순수 함수 `labelKeys(plan: OrbitPlan, limit: number): string[]` (`visual/labels.ts`): 첫 궤도의 key 를 최대 `MAX_LABELS = 8` 개.

## 5. 범례 (D68)

- `shell/Legend.tsx` (React, 우주 화면에서만): 오른쪽 아래 고정 패널.

```
Legend                              [H]
● Size        memory
● Glow, pulse CPU
○ Inner orbit larger memory
◌ Outer ring  CPU cores (blue → orange → white = load)
— Line        group → core it runs on
              sharp = measured · faint = estimated
```

- 접으면 `Legend [H]` 한 줄. 상태는 `localStorage['pulse.legend']` (`'hidden'` 이면 접힘, 없거나 그 밖이면 펼침). 저장이 막힌 환경(예외)에서는 펼침으로 시작하고 저장은 건너뛴다.
- `H` 는 `shell/shortcut.ts` 의 `isShortcut(event, 'h')` 로 처리한다 (IME·수정 키·입력창 규칙 공용).
- **초점 중에는 범례를 숨긴다** (`focusStore.focusedKey !== null`). FocusPanel 이 오른쪽에서 아래까지 내려와 범례를 덮기 때문이다 (최종 리뷰 I2). 접은 상태(`localStorage`)는 건드리지 않는다.
- 이름표 z-index: drei `Html` 은 카메라 거리로 z-index 를 정하므로 범위가 겹치면 앞쪽 이름표가 툴팁을 덮는다. 이름표는 `[900, 0]`, 툴팁은 `[16777271, 1000]` 으로 나눈다.
- 이름표 대상은 첫 궤도의 key 오름차순 앞 8 개다. 첫 궤도에 8 개를 넘는 천체가 있으면 가장 큰 8 개가 아니라 이름 순 8 개다 (궤도 안 자리가 key 순이고 `OrbitPlan` 에 반지름이 없다). 기본 40 그룹에서는 첫 궤도가 4~6 개라 해당하지 않는다.
- 패널은 `pointer-events` 를 받지만 캔버스의 드래그를 가로채지 않게 작게 둔다. 성능 표시(`P`, 왼쪽 아래)·FocusPanel(오른쪽 위)과 겹치지 않는다.

## 6. 모듈 구조

```
web/src/visual/labels.ts      MAX_LABELS, labelKeys, systemDetail
web/src/scene/BodyLabels.tsx  안쪽 궤도 천체의 이름표
web/src/scene/SystemStar.tsx  (전체 교체) 호버 받기
web/src/scene/Tooltip.tsx     (전체 교체) system 처리
web/src/scene/sceneContext.ts (전체 교체) Hovered 에 system
web/src/scene/FlowStreams.tsx (전체 교체) system 은 호버 없음처럼
web/src/scene/SceneRoot.tsx   (전체 교체) BodyLabels 마운트
web/src/shell/Legend.tsx      범례
web/src/shell/Shell.tsx       (전체 교체) 우주 화면에 Legend
web/src/shell/shell.css       (끝에 붙임) .universe-label, .legend
```

`visual/**` 순수성 규칙은 그대로다.

## 6.1 시제품 결과

실제 엔진(그룹 40 개, 비권한)에 붙여 확인했다 (창 1000×563).

- 별에 올리면 `System` 툴팁: `CPU 8.3% · Mem 23.8 / 31.8 GB · 395 procs · 8,327 threads`. 별 뒤의 천체가 대신 잡히지 않는다.
- 안쪽 궤도의 6 개 천체(whale, Code, Figma, claude, ChatGPT, msedgewebview2)에 이름표가 붙어 공전을 따라간다. 800×450 으로 줄인 스크린샷에서는 11px 글씨가 잘 안 읽히지만 1000×563 부터 읽힌다.
- 범례가 오른쪽 아래에 펼쳐져 있고, `H` 로 접히며(`localStorage['pulse.legend'] = 'hidden'`) 다시 `H` 로 펴진다 (`'shown'`).
- 테스트 296 (기존 284 + 이름표 6 + 범례 6), 타입체크·린트·빌드 통과.
- 저장은 상태가 바뀔 때마다 하는 `useEffect` 로 한다 (처음 방문에서도 `'shown'` 이 저장된다). 상태 갱신 함수 안에서 저장하면 StrictMode 가 그 함수를 두 번 부르므로 순수하지 않다.
- 확인하지 못한 것: 초점을 잡을 때 이름표가 사라지는 것과 그룹이 사라질 때 이름표가 함께 사라지는 것은 화면에서 보지 못했다 (코드는 `focus.weight > 0.5` 와 존재 추적기의 phase 로 숨긴다).

## 7. 실패 동작

- 스냅샷이 아직 없으면 별 툴팁은 뜨지 않는다 (`labelFor` 가 null).
- 그룹이 없으면 이름표도 없다 (궤도 없음).
- `localStorage` 가 없거나 예외를 던져도 범례는 동작한다 (저장만 건너뜀).

## 8. 테스트

| 대상 | 방식 |
|---|---|
| `labelKeys`, `systemDetail` | node 단위 테스트: 첫 궤도만, 한도, 빈 계획, CPU null, 메모리 GB 반올림, 천 단위 구분 |
| `Legend` | jsdom: 처음 펼침, `H` 로 접고 펼침, 접으면 저장, 저장된 상태로 시작, 입력창 포커스 중 `H` 무시, `localStorage` 예외에서도 동작 |
| 장면 | 컨트롤러가 실제 엔진에 붙여 스크린샷 확인 |

## 9. 완료 조건

- 테스트·타입체크·린트·빌드 통과.
- 실제 엔진 화면: 별에 올리면 시스템 요약이 뜬다. 안쪽 궤도의 큰 천체 이름이 항상 보이고 공전을 따라간다. 초점을 잡으면 이름표가 사라진다. 오른쪽 아래 범례가 보이고 `H` 로 접힌다. 콘솔 오류 없음.

## 10. 범위 밖

한글 표시, 이름표 겹침 자동 회피, 별 클릭 동작, 코어 이름표.
