# M7 — Thread Flow 설계

- 작성일: 2026-09-29
- 상태: 승인 대기
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M4~M6 스펙, M1~M6 구현 (`main`)
- 범위: 엔진의 `flows[]`(그룹 → 코어, 가중치, 출처)를 프로세스 천체에서 코어 Orb 로 흐르는 빛의 선과 입자로 그린다. 엔진은 바꾸지 않는다.

## 1. 이 문서의 위치

계약서 8절의 M7 완료 조건은 "프로세스와 코어를 잇는 빛의 흐름" 이다. 계약서 4.5절은 `flows[].weight` 를 "Flow 굵기·입자 밀도" 에, `flows[].source` 를 "Flow 선명도" 에 배정했고, 3.1절은 "프론트엔드는 source 에 따라 시각적 확신도(Flow 선명도)만 달리하고 나머지 로직은 동일하게 유지한다" 고 정했다. M6 가 코어를 무리를 두르는 고리 위에 고정 자리로 두었다.

## 2. 측정

본 개발 PC(논리 코어 28개), 실제 엔진. 대기 19초, 이어서 코어 4개 부하 19초.

| | 스냅샷당 flow | flow 를 가진 그룹 | 가중치 p10 / 중앙값 / p90 / 최대 | 매초 새로 생기는 선 |
|---|---|---|---|---|
| 대기 | 4~14 (평균 5.4) | 1.6 | 0.068 / 0.105 / 0.183 / 0.367 | 5.4 개 중 2.8 (52%) |
| 부하 | 4~20 (평균 15.8) | 4.1 | 0.082 / 0.116 / 0.170 / 0.188 | 16.4 개 중 5.1 (31%) |

부하 중 예: `node.exe → 코어 8, 10, 2, 14`, `whale.exe → 코어 8, 10, 2, 14`. 바쁜 그룹 넷이 모두 같은 코어 넷으로 선을 긋는다.

1. **추정식은 그룹이 실제로 어느 코어에서 도는지 모른다.** `FlowEstimator` 는 `weight = (그룹 CPU / 최대 그룹 CPU) × (코어 부하 / 전체 코어 부하)` 이고, 0.05 미만을 버리고 그룹당 상위 4개를 남긴다. 그래서 바쁜 그룹들이 전부 그 순간 가장 뜨거운 코어들로 향한다. 이는 계약 D2(1차는 추정, ETW 실측은 후속)의 알려진 한계다. M7 은 추정식을 고치지 않고, `source` 로 "추정" 임을 선명도로 드러낸다.
2. **선이 매초 1/3~1/2 씩 바뀐다.** 뜨거운 코어의 순위가 초마다 바뀌기 때문이다. 그대로 그리면 선들이 계속 반짝이며 교체된다.
3. **가중치가 작다.** 최대 0.37, 대부분 0.07~0.19. 굵기·입자 수 매핑은 이 범위에서 차이가 보여야 한다.

## 3. 확정된 결정

D31~D36(M6)에 이어 번호를 매긴다.

| # | 결정 | 선택 | 근거 |
|---|---|---|---|
| D37 | 선 깜빡임 | **잔광**: 사라진 선은 2.5초에 걸쳐 흐려진다. 다시 나타나면 그 밝기에서 이어서 밝아진다 | 2절 사실 2. 사용자가 실측 수치로 선택. 흐려지는 동안의 값이 옛 값임을 밝기가 드러낸다 |
| D38 | 추정식 | 바꾸지 않는다 | 2절 사실 1. 실측(ETW)은 M9 이후 독립 확장이다. 계약에 `source` 가 이미 있다 |
| D39 | 선명도 | `estimated` 선은 흐리게(×0.55), `measured` 는 또렷하게(×1.0). 입자는 같게 | 계약서 3.1 |
| D40 | 모양 | 그룹 → 코어 2차 베지어 아치 + 흐르는 입자 | 계약서 4.5 "굵기·입자 밀도". WebGL 선 굵기는 1px 로 고정되므로 굵기는 밝기와 입자 수로 표현한다 |
| D41 | 코어 식별 | `flows[].core` 는 코어 **id** — 같은 프레임의 `cores` 에서 id 로 index 를 찾는다 | M6 최종 리뷰. id ≠ index 인 머신(64코어 초과 등)이 있다 |
| D42 | 강조 | 그룹·코어 호버 시 그것과 이어진 선만 밝고 나머지는 30% | 16개 넘는 선이 같은 코어로 몰릴 때 읽을 수 있어야 한다 |

### 3.1 되돌리기 쉬운 것과 어려운 것

- **쉬움**: 모든 상수(잔광 2.5초, 밝아짐 0.4초, 아치 높이, 입자 수, 선명도 계수, 최대 선 수).
- **어려움**: 없음. 엔진·계약을 건드리지 않는다.

## 4. 선 추적기 (`visual/flowTracker.ts`)

선 key 는 `그룹key>코어id` 다. 상태: `{ key, group, coreId, weight, source, intensity(0~1), present }`.

```
FADE_IN_SEC   = 0.4    있는 선은 밝기가 0 → 1 로 0.4초에 오른다
AFTERGLOW_SEC = 2.5    없는 선은 밝기가 1 → 0 으로 2.5초에 내려간다
MAX_EDGES     = 96     넘치면 (weight × intensity) 가 가장 작은 선부터 버린다
```

- `update(flows, liveGroups, nowSec)`:
  - 이번 `flows` 에 있는 선: `weight`·`source` 를 새 값으로, `present = true`, 밝기 `+dt / FADE_IN_SEC` (1 에서 멈춤).
  - 없는 선: `present = false`, 밝기 `−dt / AFTERGLOW_SEC`. 0 이 되면 지운다. `weight` 는 마지막 값을 유지한다.
  - `liveGroups`(존재 추적기에 있는 그룹 key 집합)에 없는 그룹의 선은 즉시 지운다 — 그 천체가 더 이상 그려지지 않는다. 붕괴·페이드아웃 중인 그룹은 아직 있으므로 선이 자연스럽게 흐려진다.
- 밝기는 **시간 기반**이다. `dt` 는 프레임 간격(`cache.dtSec`, 1/30 이하로 자르지 않는다 — 탭 복귀 뒤 잔광은 끝나 있는 것이 맞다).
- 흐름이 한 스냅샷(1초) 동안 같으므로 `update` 는 매 프레임 불려도 같은 스냅샷에 대해 멱등이다(있는 선은 계속 밝아지고, 없는 선은 계속 흐려진다).
- 세션 변경·스토어 비움 시 `reset()`.
- 출력: `edges()` — 그릴 선 목록. 그리는 세기 `strength = weight × intensity`.

## 5. 코어 id → 자리 (`visual/coreRing.ts` 에 추가)

```
coreIndexById(cores: { id: number }[]): Map<number, number>
```

선의 코어 끝점은 `corePosition(index, count)` 이다. `count` 는 CoreRing 과 같은 값(`cores.length`). id 가 없으면 그 선은 그리지 않는다(추적기에는 남겨 두어, 코어가 다시 나타나면 이어서 그린다).

## 6. 곡선 (`visual/flowCurve.ts`)

```
from = 그룹 천체의 부유 위치 (floatingPosition)
to   = 코어 위치
mid  = (from + to) / 2
dist = |to − from|
control = mid + (0, ARCH × dist, 0) + side × (SPREAD × dist × (hash(key) − 0.5))
ARCH = 0.35,  SPREAD = 0.3
side = (to − from) × (0,1,0) 을 정규화한 수평 법선
point(t) = (1−t)²·from + 2(1−t)t·control + t²·to
```

- 아치는 무리 위로 솟아 고리로 내려앉는다 — 무리 안을 가로지르는 선이 줄어든다.
- 같은 코어로 가는 선들이 key 해시로 옆으로 흩어져 한 줄로 겹치지 않는다.
- 결정적이다(같은 key·끝점 → 같은 곡선).

## 7. 그리기

### 7.1 선 (`scene/FlowLines.tsx`)

- 공유 `LineSegments` 하나. 선마다 `SEGMENTS = 24` 구간(정점 48개). 버퍼는 `MAX_EDGES × 48` 정점.
- 정점 색: 그룹 색(`colorFor(account, key)`)에서 코어 열 색(`coreColor(load)`)으로 `t` 에 따라 섞는다. 밝기는 색에 곱한다(가산 블렌딩).
- 선 밝기: `lineAlpha = clarity(source) × (0.25 + 1.6 × strength) × dim × highlight`, 1 에서 자른다.
  - `clarity(estimated) = 0.55`, `clarity(measured) = 1.0`
- 색은 sRGB → 선형으로 바꿔 넣는다 (M6 불꽃과 같다).
- `raycast` 없음, `frustumCulled = false`, `depthWrite = false`.

### 7.2 흐르는 입자 (`scene/FlowParticles.tsx`, `visual/flowParticles.ts`)

- 공유 `Points` 하나. 선마다 `particleCount(strength) = min(12, ceil(strength × 30))` 개. 버퍼는 `MAX_EDGES × 12`.
- 선마다 흐름 위상을 **누적**한다: `phase += (0.25 + 0.6 × weight) × dt` (곡선 매개변수/초, 1 에서 감는다). 입자 i 의 위치는 `point(frac(phase + i / n))` — 그룹에서 코어 쪽으로 흐른다.
- 입자 색은 그 자리의 곡선 색, 밝기 `0.4 + 0.6 × intensity` × dim × highlight. 크기 0.22.
- 입자는 `source` 와 무관하다(D39).

### 7.3 강조 (D42)

- 호버가 그룹이면 그 그룹의 선, 코어면 그 코어로 가는 선만 `highlight = 1`, 나머지 `0.3`. 호버가 없으면 모두 1.
- Focus 중에는 초점 그룹의 선은 `dim = 1`, 나머지는 `1 − DIM_DEPTH × weight` (M5·M6 와 같다).

## 8. 데이터 흐름

```
SceneRoot useFrame (scene, -1)   … 기존: 보간, 존재 추적, 레이아웃
FlowField useFrame (scene 직후)   flowTracker.update(frame.flows, 존재 추적기 key 집합, dt)
FlowLines, FlowParticles (particles, -0.2)   추적기 edges() → 버퍼
```

- `FlowField` 는 추적기를 소유하고 context 에 싣는다(새 필드 `flows`). 우선순위는 `FRAME_PRIORITY.flows = -0.9` (scene 뒤, camera 앞).
- `frame === null` 이면 추적기를 비운다. 세션이 바뀌면 SceneRoot 가 이미 존재 추적기를 비우므로 다음 프레임에 `liveGroups` 에 없는 선들이 지워진다.

## 9. 모듈 구조

```
web/src/visual/
  flowTracker.ts     FlowTracker (잔광), 상수
  flowCurve.ts       flowControlPoint, curvePoint
  flowParticles.ts   particleCount, advanceFlowPhase, particleT
  coreRing.ts        (수정) coreIndexById
web/src/scene/
  framePriority.ts   (수정) flows: -0.9
  sceneContext.ts    (수정) flows: FlowTracker
  FlowField.tsx      추적기 갱신
  FlowLines.tsx      LineSegments
  FlowParticles.tsx  Points
  SceneRoot.tsx      (수정) 마운트, context
```

## 10. 실패 동작

| 상황 | 동작 |
|---|---|
| flows 가 비었음 | 선이 잔광으로 흐려지다 사라진다 |
| 코어 id 가 cores 에 없음 | 그 선을 그리지 않는다. 추적기에는 남는다 |
| 그룹이 존재 추적기에서 사라짐 | 그 선을 즉시 지운다 |
| 선이 96개 초과 | 가장 약한 것부터 버린다 |
| 탭 복귀(큰 dt) | 잔광이 한 번에 끝난다 — 옛 흐름을 되살리지 않는다 |
| 스냅샷 없음 | 추적기를 비운다 |

## 11. 테스트 전략

| 대상 | 검증 |
|---|---|
| `flowTracker` | 밝아짐 0.4초, 잔광 2.5초 뒤 삭제, 재등장 시 이어서 밝아짐, 흐려지는 동안 weight 유지, 그룹이 사라지면 즉시 삭제, 최대 96개와 약한 것부터 버림, reset, 큰 dt |
| `flowCurve` | t=0·1 에서 끝점, 제어점 높이 = 0.35 × 거리, 옆 흩뜨림 범위, 결정성, 같은 끝점·다른 key 는 다른 곡선 |
| `flowParticles` | 입자 수 매핑과 상한, 위상 누적·감기, 입자 t 가 [0,1) |
| `coreIndexById` | 연속하지 않는 id, 빈 목록 |

브라우저 확인:

- 대기 중 whale 등 바쁜 그룹에서 뜨거운 코어로 아치형 선과 흐르는 입자가 보인다.
- 코어 4개 부하 시 선이 늘고, **1초마다 선이 번쩍이며 교체되지 않는다**(잔광).
- 그룹이나 코어에 호버하면 그 선들만 밝다. Focus 중 초점 그룹의 선만 밝다.
- 콘솔 오류 없음. 엔진 서빙 빌드에서도 같다.
- M5·M6 교훈: 시제품의 마지막 수정 뒤 전체 검사(테스트 타입체크 포함).

## 12. 범위 밖

- 추정식 개선, ETW 실측 (M9 이후, 계약 D2)
- Bloom·심도 (M8), 인스턴싱·성능 측정 (M9)
- flow 클릭 동작

## 13. 구현 순서

1. `visual/flowTracker` (TDD)
2. `visual/flowCurve`, `visual/flowParticles`, `coreIndexById` (TDD)
3. 장면: FlowField, FlowLines, FlowParticles, 강조·dim
4. 브라우저 확인
