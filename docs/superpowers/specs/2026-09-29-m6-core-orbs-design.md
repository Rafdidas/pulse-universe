# M6 — CPU Core Orb, 셰이더, 파티클 설계

- 작성일: 2026-09-29
- 상태: 승인 대기
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M4 `2026-09-28-m4-universe-scene-design.md`, M5 `2026-09-28-m5-lifecycle-focus-design.md`, M1~M5 구현 (`main`)
- 범위: CPU 코어를 프로세스 무리를 두르는 고리 위의 Orb 로 그리고, 부하에 따라 커지고·달아오르고·일그러지고·불꽃을 튀기게 한다. `ambient` 를 배경 먼지로 그린다.

## 1. 이 문서의 위치

계약서 8절의 M6 완료 조건은 "부하에 따라 일그러지고 빛나는 코어" 다. 계약서 4.5절은 `cores[].pct` 를 "Core Orb 크기·셰이더 왜곡·파티클 밀도" 에, `ambient` 를 "배경 미세 입자" 에 배정했다. M7(Thread Flow)은 그룹에서 코어로 선을 긋는다 — 코어의 자리는 M7 의 모양을 정한다.

## 2. 측정

본 개발 PC(논리 코어 28개), 실제 엔진, 34초.

| | 코어당 중앙값 | p90 | 최대 | 초당 코어 변화량 p90 |
|---|---|---|---|---|
| 대기 | 4.8% | 24% | 72% | 13 %p |
| 4코어 부하 | 13% | 62% | 91% | 31 %p |

```
ambient    service_proc_count 83, service_mem_mb 154
flows      9개 (예: Code.exe:16032 → 코어 8, 11, 20)
```

1. **코어 값은 초 단위로 크게 흔들린다.** M3 보간기가 1초에 걸쳐 선형으로 잇는다. 추가 평활은 두지 않는다 — 부하의 변화를 늦게 보여 주면 모니터의 의미가 줄어든다.
2. **대기 중 코어 절반은 5% 미만이다.** 대기 상태의 Orb 는 거의 매끈하고 작고 차가워야 하며, 부하가 걸린 코어만 눈에 띄어야 한다 — 일그러짐을 부하의 제곱에 비례시킨다(5.2).
3. **ambient 는 83개 · 154 MB 로 작다.** 배경 먼지 정도가 맞다.

## 3. 확정된 결정

D24~D30(M5)에 이어 번호를 매긴다.

| # | 결정 | 선택 | 근거 |
|---|---|---|---|
| D31 | 코어 배치 | 프로세스 무리를 두르는 XZ 평면 고리, id 순 등간격, 고정 위치 | M7 flow 가 안에서 밖으로 퍼져 무리를 덜 가로지른다. 기존 배치 코드 불변. 카메라가 돌아도 고리가 타원으로 보인다. 사용자가 세 안(둘레 고리·중심·아래 호) 중 선택 |
| D32 | Orb 재질 | 커스텀 `ShaderMaterial` (정점 변위 노이즈 + fresnel + 부하 색) | 계약서 "셰이더 왜곡". 코어마다 재질 하나, 같은 프로그램에 uniform 만 다르다 |
| D33 | 노이즈 | Ashima webgl-noise 3D simplex (MIT), 출처 주석 포함 | 검증된 공개 구현. 외부 패키지를 늘리지 않는다 |
| D34 | 코어 불꽃 | 공유 `Points` 하나, 코어당 최대 24개, 켜지는 수 = round(load × 24) | 계약서 "파티클 밀도". M5 버스트와 같은 공유 버퍼 방식 |
| D35 | ambient | 배경 먼지 `min(service_proc_count × 6, 800)` 개, 구각 40~90 | 계약서 "배경 미세 입자" |
| D36 | 코어 클릭 | 아무 동작 없음, 전파만 막는다 | 뒤쪽 천체로 클릭이 새면 초점이 바뀐다(M5 위성과 같은 문제) |

### 3.1 되돌리기 쉬운 것과 어려운 것

- **쉬움**: 모든 매핑 상수(크기, 진폭, 색 3단, 불꽃 수, 먼지 수), 고리 반지름.
- **어려움**: D31(고리 위치). M7 flow 가 이 위치를 끝점으로 쓴다.

## 4. 코어 고리 (`visual/coreRing.ts`)

```
RING_MIN_RADIUS = 28
ORB_SPACING     = 4.5     (고리 둘레 위 이웃 코어 간격)
ringRadius(n)   = max(RING_MIN_RADIUS, n × ORB_SPACING / 2π)
corePosition(index, n) = (r·cos θ, 0, r·sin θ),  θ = 2π·index / n
```

- 28코어 → 반지름 28, 이웃 간격 6.3. 64코어 → 반지름 45.8. Orb 최대 반지름 1.6 이 겹치지 않는다.
- 무리의 최대 반경은 약 15, M5 의 새 천체는 "가장 먼 노드 + 4" 에 생긴다 — 고리와 부딪히지 않는다.
- `index` 는 `cores` 배열에서의 순서다(엔진이 id 순으로 정렬해 보낸다, M1). 코어 수가 바뀌면 위치가 다시 계산된다 — 실제로는 바뀌지 않는다.

## 5. Orb 시각 매핑 (`visual/coreMapping.ts`)

`load = clamp(pct / 100, 0, 1)`.

### 5.1 크기

`orbRadius(load) = 0.7 + 0.9 × load` (0.7 ~ 1.6).

### 5.2 일그러짐

정점을 법선 방향으로 `amplitude × snoise(normal × 1.8 + offset)` 만큼 민다. `offset` 은 JS 에서 `offset += speed(load) × dt` 로 **누적**한다(`advanceNoiseOffset`). `시각 × speed(load)` 로 넘기면 `performance.now()` 가 수천 초일 때 부하가 조금만 바뀌어도 오프셋이 크게 튀어 Orb 가 떤다 — M4 맥박 위상과 같은 이유다.

```
amplitude(load) = 0.04 + 0.28 × load²     대기 5%: 0.041, 50%: 0.11, 90%: 0.27
speed(load)     = 0.25 + 1.75 × load
```

제곱인 이유는 2절 사실 2 — 대기 코어는 거의 매끈해야 한다.

### 5.3 색

부하 0 → 0.5 → 1 을 세 색으로 잇는다. **HSL 로 섞고 색조는 증가 방향으로만 돈다** (파랑 → 보라 → 자홍 → 주황):

```
COLD  h 205°  s 0.90  l 0.60   (0.0)  파랑
WARM  h 382°  s 1.00  l 0.62   (0.5)  주황 (382 = 22°)
HOT   h 402°  s 1.00  l 0.82   (1.0)  밝은 노랑흰색 (402 = 42°)
```

시제품에서 처음에는 RGB 로 섞었는데, 파랑과 주황 사이가 회색이 되어 부하가 조금 있는 코어가 "식은" 것처럼 보였다. HSL 경로는 중간이 보라·자홍이라 달아오르는 것처럼 읽힌다. 테스트가 전 구간에서 채도(최대−최소 채널 > 0.3)를 확인한다.

### 5.4 발광

```
rimIntensity(load)  = 0.3 + 1.2 × load      (셰이더 fresnel 가장자리)
haloOpacity(load)   = 0.04 + 0.30 × load    (가산 블렌딩 헤일로 구, 반지름 × 1.5)
```

Bloom 은 M8 이다.

### 5.5 셰이더 uniform

`uOffset`(누적 노이즈 오프셋), `uAmplitude`, `uColor`(vec3), `uRim`, `uOpacity`(Focus 로 어두워질 때). 매 프레임 JS 에서 값을 넣고 셰이더는 계산만 한다 — 매핑은 전부 5.1~5.4 의 순수 함수다.

## 6. 코어 불꽃 (`visual/coreSparks.ts`, `scene/CoreSparks.tsx`)

```
SPARKS_PER_CORE = 24
activeSparks(load) = round(load × 24)
```

불꽃 i 는 제 코어 둘레를 돈다. 궤도 반지름 `orbRadius × (1.3 ~ 2.2)`, 궤도면 기울기·초기 위상은 (코어 index, i) 해시. 코어마다 궤도 위상을 **누적**한다: `phase += (0.6 + 2.0 × load) × dt` (`advanceSparkPhase`, 5.2 와 같은 이유). 위치는 `sparkPosition(coreIndex, i, center, orbRadius, orbitPhase)` 순수 함수다. 입자 크기는 0.3 (시제품에서 0.18 은 카메라 거리 58 에서 거의 보이지 않았다). 켜진 불꽃만 앞에서부터 버퍼에 채운다(M5 `Particles` 와 같은 방식). 최대 28 × 24 = 672개 — 코어가 64개면 1536개. 버퍼는 코어 수 × 24 로 잡는다.

## 7. ambient 먼지 (`visual/ambient.ts`, `scene/AmbientDust.tsx`)

```
AMBIENT_PER_SERVICE = 6
AMBIENT_MAX         = 800
ambientCount(service_proc_count) = min(service_proc_count × 6, 800)
ambientPoint(i) = key 해시로 정한 방향 × 반지름(40 ~ 90)
```

- 위치는 800개를 미리 계산해 두고 `drawRange` 로 개수만 바꾼다.
- 먼지 전체(Points 객체 하나)를 y 축으로 아주 천천히 돌린다(0.01 rad/s). 입자별 계산이 없다.
- 크기 0.25, 불투명도 0.35, 색 `#8fa3c0`, 가산 블렌딩 없음(배경이 밝아지지 않게).
- 호버·클릭 대상이 아니다.

## 8. 상호작용

- **호버**: 코어 Orb 에 올리면 툴팁 "CPU 7 · 43.2%". `Hovered` 에 `{ kind: 'core'; index: number }` 를 더한다. 표시 값은 `formatPct` (대시보드와 같다).
- **클릭**: 드래그가 아닌 클릭(`delta ≤ CLICK_SLOP`)이면 전파만 막는다. `CLICK_SLOP` 은 `scene/interaction.ts` 한 곳으로 옮기고 ProcessNode·SatelliteNode·CoreOrb 가 함께 쓴다(M5 이월).
- **Focus 중**: Orb·헤일로·불꽃이 다른 천체와 같이 `1 − 0.75 × weight` 로 어두워진다.

## 9. 렌더링 구조

- Orb 목록은 `cores.length` 가 바뀔 때만 다시 그린다 — 스토어 selector 가 숫자 하나(`current?.cores.length ?? 0`)를 돌려준다.
- 각 `CoreOrb` 는 `useFrame` 에서 `cache.snapshot.cores[index].pct` 를 읽어 uniform·scale 을 바꾼다. 프레임 값은 React 상태에 들어가지 않는다.
- `CoreSparks` 는 `FRAME_PRIORITY.particles` 에서 돈다.

## 10. M5 이월

- **존재 추적기 version**: `PresenceTracker` 에 항목이 추가·삭제될 때마다 증가하는 `version` 을 둔다. SceneRoot·Satellites 는 매 프레임 key 문자열을 이어 붙여 비교하는 대신 version 이 바뀌었을 때만 id 목록을 만든다(M5 최종 리뷰 #7). 계정은 key 에 딸려 바뀌지 않으므로 version 으로 충분하다.
- **CLICK_SLOP** 한 곳으로 (8절).

## 11. 모듈 구조

```
web/src/visual/
  coreRing.ts       ringRadius, corePosition
  coreMapping.ts    coreLoad, orbRadius, distortion, noiseSpeed, coreColor, rimIntensity, coreHaloOpacity
  coreSparks.ts     activeSparks, sparkPosition
  ambient.ts        ambientCount, ambientPoint
  presence.ts       (수정) version
web/src/scene/
  interaction.ts    CLICK_SLOP
  coreShader.ts     정점·조각 셰이더 문자열 (simplex noise 포함)
  CoreOrb.tsx       Orb 하나 (ShaderMaterial + 헤일로)
  CoreRing.tsx      Orb 목록 (cores.length 구독)
  CoreSparks.tsx
  AmbientDust.tsx
  SceneRoot.tsx     (수정) version 비교, 코어·먼지 마운트
  Satellites.tsx    (수정) version 비교
  ProcessNode.tsx, SatelliteNode.tsx  (수정) CLICK_SLOP import
  Tooltip.tsx       (수정) core 라벨
  sceneContext.ts   (수정) Hovered 에 core
```

## 12. 실패 동작

| 상황 | 동작 |
|---|---|
| 첫 스냅샷의 코어 pct 0.0 (PDH 첫 표본) | 가장 작고 차가운 Orb. 다음 초부터 실제 값 |
| 스냅샷 없음 | 고리를 그리지 않는다 |
| 코어 수 변화 | Orb 목록 재생성, 위치 재계산 |
| 셰이더 컴파일 실패 | 장면 오류 경계(M4)가 받아 대시보드 안내를 보여 준다 |
| 먼지 수 변화 | `drawRange` 만 바뀐다 |

## 13. 테스트 전략

| 대상 | 검증 |
|---|---|
| `coreRing` | 28 → 반지름 28, 64 → 45.8, 이웃 간격, y = 0, 결정성, 첫 코어는 +x |
| `coreMapping` | 5절 표의 수치, load 자르기(음수·100 초과), 색 3단의 양 끝·중간 색조, 전 구간 회색 없음, HSL→RGB 변환, 노이즈 오프셋 누적 |
| `coreSparks` | 켜지는 수, 궤도 반지름 범위, 결정성, 위상에 따라 움직임, 위상 누적, 부하가 바뀌어도 한 프레임에 튀지 않음 |
| `ambient` | 개수 상한, 반지름 범위, 결정성 |
| `presence` version | 추가·삭제 시 증가, 값 갱신·진행만으로는 불변, reset 시 증가 |
| 셰이더 | jsdom 에서 컴파일할 수 없다 — 브라우저에서 확인 |

브라우저 확인:

- 28개 Orb 가 무리를 두르고, 대기 코어는 작고 파랗고 거의 매끈하다.
- 한 코어를 태우면(`node -e` 바쁜 루프) 그 코어가 커지고 주황~흰색으로 달아오르고 일그러지며 불꽃이 늘어난다. 툴팁 % 가 대시보드 코어 값과 맞는다.
- 코어 클릭 시 초점이 바뀌지 않는다. Focus 중 코어가 어두워진다.
- 배경 먼지가 보이고 천천히 돈다.
- 콘솔에 셰이더 오류가 없다. 엔진 서빙 빌드에서도 같다.
- M5 교훈: 시제품의 마지막 수정 뒤 전체 검사(테스트 타입체크 포함).

## 14. 범위 밖

- Flow 선 (M7), Bloom·심도 (M8), 인스턴싱 (M9)
- 코어 토폴로지(P/E 코어, 하이퍼스레딩 짝) — 계약에 정보가 없다
- 코어 클릭 동작(예: 그 코어로 가는 flow 강조) — M7 에서 판단

## 15. 구현 순서

1. `visual/coreRing`, `visual/coreMapping` (TDD)
2. `visual/coreSparks`, `visual/ambient` (TDD)
3. `presence` version + SceneRoot·Satellites 적용, `interaction.ts` (M5 이월)
4. 장면: coreShader, CoreOrb, CoreRing, 툴팁·클릭
5. 장면: CoreSparks, AmbientDust
6. 브라우저 확인
