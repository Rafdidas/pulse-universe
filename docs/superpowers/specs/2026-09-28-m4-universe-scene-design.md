# M4 — R3F 장면과 Process Node 설계

- 작성일: 2026-09-28
- 상태: 승인 대기
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M3 `2026-09-23-m3-frontend-design.md`, M1~M3 구현 (`main`)
- 범위: M3 데이터 레이어 위에 React Three Fiber 장면을 올리고, 그룹 40개를 크기·맥박·발광을 가진 천체로 그린다.

## 1. 이 문서의 위치

M3은 스냅샷을 받아 검증하고 보간하는 데이터 레이어(Layer 3)와 숫자 대시보드를 만들었다. M4는 **Layer 4(시각화)** 의 첫 단계다. 계약서 8절의 완료 조건은 "첫 우주. 크기·맥박·발광을 가진 40개 천체" 다.

생성·종료 연출, Focus, 카메라 전환은 M5, 코어 Orb는 M6, Flow는 M7, Bloom은 M8, 인스턴싱은 M9다. M4는 그 위에 쌓일 장면의 뼈대와 Process Node 하나를 완성한다.

## 2. 측정: 무엇을 그려야 하는가

설계 전에 저장소의 실제 픽스처(`web/tests/fixtures/snapshot.json`, 본 개발 PC, 28코어)를 다시 읽었다.

```
그룹 40
mem_mb   최소 45   중앙값 106   최대 4,237      ← 약 93배
cpu_pct  40개 중 34개가 0.0, 최대 1.4           ← 대기 상태
threads  9 ~ 923
```

두 가지 사실이 매핑을 결정한다.

1. **메모리 범위가 93배다.** 반지름을 메모리에 선형으로 대응시키면 가장 작은 천체가 가장 큰 것의 1% 크기가 되어 보이지 않는다.
2. **`cpu_pct` 는 전체 코어 수로 나눈 값이다** (`CpuDelta.cpp`: `cpu_ms / (wall_ms × core_count) × 100`). 28코어에서 스레드 하나가 코어 하나를 꽉 채워도 3.6%다. 0~100을 그대로 밝기에 대응시키면 모든 천체가 항상 어둡다.

## 3. 확정된 결정

D12~D17(M3)에 이어 번호를 매긴다.

| # | 결정 | 선택 | 근거 |
|---|---|---|---|
| D18 | 반지름 매핑 | `r = 0.25 · ∛mem_mb`, 절대값 | 부피 ∝ 메모리. 45 MB도 최대의 22% 크기로 보인다. 최댓값으로 정규화하지 않으므로 큰 프로세스가 사라져도 다른 천체 크기가 튀지 않는다. 사용자가 sqrt·log와 실제 수치로 비교해 선택 |
| D19 | 활동도 단위 | 사용 중인 코어 개수로 환산 | 2절의 두 번째 사실 |
| D20 | 프레임당 샘플링 | 장면 루트가 `sample()` 을 프레임당 한 번 호출, key로 조회 가능한 `FrameCache` 에 둔다 | 노드마다 호출하면 O(n²). M3에서 미뤄둔 항목 |
| D21 | 배치 | 직접 만든 힘 시뮬레이션, 순수 모듈 | 노드 40개 → 쌍 780개. 라이브러리가 필요한 규모가 아니고, 테스트 가능한 순수 함수로 두는 편이 낫다 |
| D22 | 대시보드와의 공존 | 기본은 우주, `#dashboard` 해시로 전환 | 대시보드는 화면이 이상할 때 숫자를 먼저 의심하게 해주는 도구다 (M3 15절). 지우지 않는다 |
| D23 | 3D 텍스트 | 쓰지 않는다. 호버 툴팁 하나만 DOM(`Html`) | drei `Text` 는 기본 폰트를 CDN에서 받는다. 엔진은 로컬 전용으로 서빙한다 |

### 3.1 되돌리기 쉬운 것과 어려운 것

- **쉬움**: 5절의 모든 상수(반지름 계수, 활동도 포화점, 맥박 주파수, 색조). 전부 `visual/` 의 상수 하나다.
- **어려움**: D20(프레임 데이터가 React 상태를 거치지 않는 구조). M5~M9의 모든 장면 요소가 같은 경로로 값을 읽는다.

## 4. 프레임당 데이터 흐름

```
snapshotStore (1 Hz)
      │  getState()  — 구독하지 않는다
      ▼
SceneRoot useFrame ── sample(input, performance.now()) ── 프레임당 1회
      │
      ├─► FrameCache  { byKey: Map<key, InterpolatedGroup>, coreCount }
      ├─► LayoutSim.step(nodes, dt)  → 위치 Map<key, Vec3>
      │
      ▼
ProcessNode useFrame (노드마다)
      cache.byKey.get(key), layout.get(key) → mesh ref 를 직접 변경
```

규칙:

1. **시계는 `performance.now()` 다.** `arrivedAt` 이 같은 시계로 찍힌다. R3F의 `state.clock` 을 섞지 않는다.
2. **프레임 값은 React 상태에 들어가지 않는다.** 노드는 `useFrame` 에서 ref로 scale·material·position을 바꾼다.
3. **React 재렌더는 그룹 key 집합이 바뀔 때만 일어난다.** 장면은 `current.groups` 의 key 목록을 얕은 비교로 구독해 `<ProcessNode key=…>` 목록을 만든다.
4. **`SceneRoot` 의 `useFrame` 은 노드들보다 먼저 돈다.** R3F의 `useFrame(cb, priority)` 에서 우선순위를 음수로 준다. 양수 우선순위는 R3F의 자동 렌더를 끄므로 쓰지 않는다.
5. `FrameCache` 와 레이아웃 상태는 장면 루트가 소유하고 React context로 노드에 넘긴다. context 값은 매 프레임 바뀌지 않는 가변 객체다.
6. `dt` 는 1/30초에서 자른다. 탭이 백그라운드에 있다 돌아오면 수 초짜리 `dt` 가 들어와 시뮬레이션이 폭발한다.

검토 후 기각한 대안:

- **노드마다 `sample()` 호출** — O(n²)이고, 노드마다 다른 시각에 샘플해 같은 프레임 안에서 값이 어긋난다.
- **지금 InstancedMesh 하나로 그리기** — M9의 일이다. 노드별 호버와 헤일로가 어려워지고, 무엇이 느린지 측정하기 전에 최적화하는 셈이다.

## 5. 시각 매핑

전부 `web/src/visual/mapping.ts` 의 순수 함수다. React·three·zustand를 모른다.

### 5.1 반지름

```
radius(mem_mb) = max(0.3, 0.25 · ∛mem_mb)      (월드 단위)
4,237 MB → 4.05    1,221 MB → 2.67    92 MB → 1.13    45 MB → 0.89
```

### 5.2 활동도

```
cores    = cpu_pct × coreCount / 100
activity = min(1, log2(1 + cores) / log2(5))
```

| 상태 | 사용 코어 | activity |
|---|---|---|
| 대기 (0.1%, 28코어) | 0.028 | 0.02 |
| 한 코어 꽉 참 | 1 | 0.43 |
| 두 코어 | 2 | 0.68 |
| 4코어 이상 | ≥ 4 | 1.0 |

`coreCount` 는 현재 스냅샷의 `cores.length` 다. `cpu_pct` 가 `null` 이면 `activity` 도 `null` 이다. **0으로 바꾸지 않는다** (계약서 4.5, M3의 null≠0 규칙).

### 5.3 맥박

모든 천체는 활동이 없어도 느리게 숨 쉰다. 활동도가 주기와 진폭을 올린다. `null` 은 기본 호흡(`activity = 0` 과 같은 모양)으로 그린다 — 모름을 시각적으로 따로 표현할 수단이 M4에는 없고, 첫 스냅샷의 1초만 해당한다.

```
freq  = 0.15 + 1.2  · a      Hz
amp   = 0.02 + 0.06 · a      반지름 대비 비율
phase += 2π · freq · dt      ← 누적. sin(2π·freq·t) 로 계산하면 freq 가 바뀌는 순간 위상이 튄다
scale = radius · (1 + amp · sin(phase))
```

초기 위상은 key 해시로 흩는다. 40개가 동시에 숨 쉬지 않게 한다.

### 5.4 발광

```
emissiveIntensity = 0.15 + 1.6  · a
halo opacity      = 0.05 + 0.35 · a      (가산 블렌딩 구, 반지름 × 1.35)
```

진짜 Bloom은 M8이다. M4의 헤일로는 그 전까지 발광을 읽을 수 있게 하는 장치이며, M8에서 제거하거나 약하게 조정한다.

### 5.5 색

계약서 4.5절은 `account` 를 색상 계열에 배정했다.

```
user    색조 185° ± 12°    (청록)
system  색조 270° ± 12°    (보라)
채도 0.65, 명도 0.55
```

`±12°` 는 key 해시에서 나온다. 같은 계열 안에서 이웃한 천체를 구분하기 위해서다. 함수는 `{h, s, l}` 을 돌려주고 `THREE.Color` 변환은 장면 쪽이 한다.

## 6. 배치

`web/src/visual/layout.ts`. 상태를 가진 순수 TypeScript 클래스이며 three를 모른다 (위치는 `{x, y, z}` 객체).

```
step(nodes: { key, radius }[], dt)
```

1. 처음 보는 key는 key 해시로 정한 위치(반지름 18인 구 안)에 놓는다. **새로고침해도 같은 모양이 나온다.**
2. 사라진 key는 상태에서 지운다.
3. 힘:
   - **충돌**: 두 노드 거리가 `rᵢ + rⱼ + 1.0` 보다 가까우면 겹친 만큼 강하게 밀어낸다.
   - **원거리 반발**: `k / max(d, 1)²`. 노드가 한 점에 뭉치지 않게 한다. `d` 가 1보다 작을 때 힘이 무한히 커지지 않도록 `max(d, 1)` 로 잘라낸다. 이 클램프는 충돌 영역(최소 충돌 거리 `0.3 + 0.3 + 1.0 = 1.6`, 즉 최소 반지름 두 개 + gap) 안에서만 값이 바뀌므로, 충돌 밖의 동작에는 영향이 없다.
   - **중심 인력**: `-g · (0.5 + rᵢ²) · pos`. 반지름의 제곱(대략 표면적)에 비례하므로 큰 천체가 가운데로 모인다.
4. 속도에 감쇠를 곱한다.
5. **고정 스텝**: 적분은 항상 1/60초 단위로 한다. 호출마다 들어온 `dt`(1/30초에서 자름, NaN·음수는 0)를 누적기에 더하고 1/60초씩 소비한다. 프레임 간격을 그대로 적분에 쓰면 같은 데이터라도 프레임률에 따라 다른 모양으로 수렴한다.
6. **사전 수렴**: 빈 시뮬레이션에 처음 노드가 들어오면 그 자리에서 600 스텝(10초 분량)을 미리 돈다. 첫 화면이 한 점에서 퍼져 나오는 대신 이미 자리 잡은 모양으로 뜨고, 그 모양은 프레임 타이밍과 무관하다 — "새로고침해도 같은 모양"이 이것으로 보장된다.

상수(`REPULSION 3`, `GRAVITY 0.05`, `COLLISION_STIFFNESS 8`, `DAMPING 0.9`/프레임)는 실제 픽스처 40개로 돌려 정했다. 10초 수렴 후 최소 여유 거리 0.69(겹침 없음), 큰 10개의 중심 거리 평균 7.3 대 작은 10개 11.4, 가장 먼 노드 14.6.

부유는 시뮬레이션 밖에서 더한다. 노드가 그릴 때 `위치 + 노이즈 오프셋`(서로 다른 주기의 sin 합, 진폭 0.2, 주기 6~10초, 위상은 key 해시)을 쓴다. 시뮬레이션 상태에는 들어가지 않는다. 계약서 7.1절대로 위치는 보간 대상이 아니다.

새 그룹은 즉시 나타나고 사라진 그룹은 즉시 없어진다. 생성·붕괴 연출은 M5다.

## 7. 장면

| 요소 | 선택 |
|---|---|
| 배경 | 단색 `#03040a` + drei `Stars` (절차 생성, 네트워크 없음) |
| 카메라 | fov 50, 위치 `(0, 10, 58)`, drei `OrbitControls`, 느린 자동 회전. 조작하면 자동 회전을 멈춘다 |
| 조명 | 방향광(`(20, 30, 25)`, 1.1) + 약한 주변광(0.2). 중심 점광원은 가운데의 큰 천체 안에 묻혀 그 천체를 안쪽에서 비추므로 쓰지 않는다 |
| 노드 | 구(`meshStandardMaterial`, emissive) + 헤일로 구(`meshBasicMaterial`, 가산 블렌딩, `depthWrite: false`) |
| 호버 | 노드 포인터 이벤트로 호버 key를 장면 로컬 상태에 둔다. 툴팁 하나(`Html`)가 이름·메모리·CPU를 보여주며, 값은 `useFrame` 에서 DOM 텍스트를 직접 갱신한다 |
| 대기 | 스냅샷이 없으면 별 배경만 그리고 "waiting for the first snapshot…" 오버레이를 띄운다 |
| DPR | `[1, 2]` |

연결이 끊기면 `sample()` 은 마지막 스냅샷에 머무르고 천체는 계속 숨 쉰다. 연결 상태는 배지가 알린다. 버전 불일치로 스토어가 비면 천체가 사라진다.

## 8. 화면 전환

`main.tsx` 의 `Root` 가 스트림을 띄우고 `Shell` 을 렌더한다. `Shell` 은:

- `location.hash === '#dashboard'` 면 대시보드, 아니면 우주를 렌더한다. 숨겨진 쪽은 마운트하지 않는다 — 대시보드의 100 ms 타이머와 WebGL 컨텍스트가 동시에 돌지 않는다.
- 우측 상단 토글 버튼과 `D` 키로 해시를 바꾼다. 해시이므로 새로고침·북마크에도 유지되고, 엔진의 SPA 폴백과 충돌하지 않는다.
- 우주 화면에서도 `ConnectionBadge` 를 표시한다.

## 9. 모듈 구조

```
web/src/
  visual/            순수 TS. react·three·zustand 금지
    hash.ts          key → [0,1) 결정적 해시
    mapping.ts       radius, activity, pulse 파라미터, glow, color
    layout.ts        LayoutSim
    frameCache.ts    InterpolatedSnapshot → { byKey, coreCount }
  scene/             R3F. stream/ 금지
    Universe.tsx     Canvas, 카메라, 조명, 배경, 대기 오버레이
    SceneRoot.tsx    프레임당 sample + 레이아웃, context 제공, 노드 목록
    ProcessNode.tsx  구 + 헤일로, useFrame 에서 ref 갱신
    Tooltip.tsx
    sceneContext.ts
    nodeList.ts      스토어 → 노드 id 문자열 배열 (useShallow 비교용)
  shell/
    view.ts          해시 ↔ 화면 (순수)
    Shell.tsx        해시 전환, 토글, 배지
    shell.css
```

lint 경계(`.oxlintrc.json` 의 `no-restricted-imports` 오버라이드):

- `src/visual/**` → `react`, `react-dom`, `three`, `@react-three/*`, `zustand` 금지
- `src/scene/**` → `stream/` 금지 (대시보드와 같은 규칙)

의존성: `three` 0.186, `@react-three/fiber` 9.8 (peer: React ≥19 <19.4 — 현재 19.2), `@react-three/drei` 10, `@types/three`. GSAP은 M5에서 추가한다.

번들은 1.26 MB(gzip 349 KB) 단일 청크가 되어 Vite의 500 KB 경고를 넘는다. 엔진이 로컬 디스크에서 서빙하므로 문제가 아니며, `chunkSizeWarningLimit` 을 1600으로 올리고 이유를 주석으로 남긴다. 코드 분할은 M9에서 측정 후 판단한다.

알려진 콘솔 경고: `THREE.Clock: This module has been deprecated` — R3F 9.8 내부가 three 0.186에서 폐기 예고된 `Clock` 을 쓴다. 우리 코드가 아니다.

## 10. 실패 동작

| 상황 | 동작 |
|---|---|
| WebGL을 쓸 수 없음 | Canvas 대신 "WebGL unavailable — open #dashboard" 안내. 대시보드는 여전히 동작 |
| WebGL 프로브는 통과했지만 렌더러 생성이나 장면 렌더 도중 실패함 | `Shell` 의 `SceneErrorBoundary`(Universe 만 감쌈)가 잡아 "3D scene failed — open #dashboard" 로 대체. 토글 버튼과 D 키는 그대로 살아 있어 대시보드로 이동할 수 있다 |
| 첫 스냅샷 전 | 별 배경 + 대기 오버레이 |
| `cpu_pct` null | 기본 호흡, 기본 발광. 툴팁에 `-` |
| 백그라운드 탭 복귀 | `dt` 1/30초 제한으로 시뮬레이션 안정 |
| 그룹 key 집합 변화 | 노드 추가·제거 (재렌더 1회), 레이아웃 상태 정리 |

## 11. 테스트 전략

계약서 10절대로 시각화 자체는 자동 테스트 대상이 아니다. 그러나 `visual/` 은 순수 모듈이므로 전부 단위 테스트한다.

| 대상 | 검증 |
|---|---|
| `mapping` | 5절 표의 수치, 반지름 하한, null → null (0 아님), 색 계열 범위, 같은 key → 같은 색 |
| `layout` | 고정 입력으로 N 스텝 후 겹침 없음, 무게중심이 원점 근처, 큰 노드가 평균적으로 중심에 가까움, 같은 입력 → 같은 결과, 사라진 key 제거, 큰 `dt` 에도 발산 없음 |
| `frameCache` | 실제 픽스처로 key 조회, `coreCount = cores.length` |
| `hash` | 결정성, 범위 [0,1) |
| `Shell`, `view` | jsdom에서 해시에 따라 대시보드/우주 선택, 토글 버튼, `D` 키(수정키·입력 중 무시). Universe는 모킹 |
| `nodeList` | 스냅샷 → id 배열, 빈 스냅샷의 같은 참조, id 왕복 |

장면은 실제 엔진에 붙여 브라우저에서 눈으로 확인한다. 확인 항목:

- 40개 천체가 겹치지 않고 큰 것이 중심 쪽에 있다.
- CPU를 쓰는 프로세스가 더 빨리 숨 쉬고 더 밝다 (예: 부하 스크립트 실행).
- 호버 툴팁의 숫자가 대시보드와 일치한다.
- 새로고침해도 배치가 같다.
- 콘솔 오류가 없고, 엔진이 서빙하는 빌드에서도 같다.

M3에서 미룬 **테스트 파일 타입체크**도 이번에 넣는다. `tsconfig.test.json` 을 추가하고 `npm run typecheck` 가 앱과 테스트를 모두 검사한다.

## 12. 범위 밖

- 생성 파티클, 붕괴, Focus, 자식 위성, 카메라 전환, GSAP (M5)
- CPU Core Orb, 셰이더 (M6)
- Thread Flow (M7)
- Bloom 등 포스트프로세싱 (M8)
- 인스턴싱, 성능 측정 (M9)
- 그룹 간 관계에 따른 군집. 계약에 그룹 간 관계 데이터가 없다.
- 실행 파일 아이콘 (`image_path`)

## 13. 구현 순서

1. 의존성 추가, `tsconfig.test.json`, lint 경계
2. `visual/hash`, `visual/mapping` (TDD)
3. `visual/layout` (TDD)
4. `visual/frameCache` (TDD)
5. `shell/Shell` 과 `main.tsx` 연결 (우주 자리에 빈 Canvas)
6. `scene/` — Universe, SceneRoot, ProcessNode
7. Tooltip, 대기 오버레이, WebGL 실패 안내
8. 브라우저 확인 (dev 서버 + 엔진 서빙 빌드)
