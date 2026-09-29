# M8 — 포스트프로세싱 + 비주얼 마감 설계

- 작성일: 2026-09-29
- 상태: 승인됨 (시제품 측정 반영)
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M4~M7 스펙, M1~M7 구현 (`main`)
- 범위: 장면 전체에 후처리(Bloom, Focus 중 심도, ACES 톤 매핑)를 한 번 거치게 하고, M6·M7 에서 넘긴 룩 문제를 정리한다. 엔진은 바꾸지 않는다.

## 1. 이 문서의 위치

계약서 8절의 M8 완료 조건은 "Bloom, 심도, 최종 룩" 이다. 계약서 9절은 기술 스택에 `@react-three/postprocessing` 을 올려 두었다. M6·M7 은 다음을 M8 로 넘겼다.

- Orb 본체 셰이더는 톤 매핑을 거치지 않고, 후광·불꽃·flow 선은 R3F 기본 ACES 를 거친다. 같은 색이 다르게 보인다.
- 카메라 가까이 온 먼지 입자가 크게 보인다.
- Focus 를 A→B 로 옮길 때 flow 선의 dim 만 카메라 전환을 따르지 않고 즉시 바뀐다 (`FlowStreams.dimFor` 와 `ProcessNode.dimFor` 의 차이).

## 2. 측정

탐색용 시제품으로 `EffectComposer` 를 붙여 잰 값이다. 창이 가려져 있어 `requestAnimationFrame` 이 멈추므로, R3F 의 `advance()` 로 프레임을 직접 돌리고 `readPixels` 로 GPU 를 동기화해 프레임 하나의 시간을 쟀다 (60회 중앙값).

| 구성 | 중앙값 | p90 |
|---|---|---|
| 현재 (후처리 없음) | 1.7 ms | 2.1 ms |
| + Bloom (mipmap blur) | 2.4 ms | 2.8 ms |
| + 심도 + Bloom | 2.7 ms | 3.1 ms |

환경: NVIDIA GeForce RTX 5050 (ANGLE D3D11), 캔버스 802×846, dpr 1, draw call 123, 삼각형 22.8만.

관찰:

- 기본값 Bloom(임계 0.6)에서는 가운데의 큰 천체 하나가 화면을 덮을 만큼 번졌다. 임계값·강도 조정이 필요하다.
- 톤 매핑 효과 없이 composer 만 두면 ACES 가 빠져 천체 색이 옅어졌다. composer 는 장면을 렌더 타깃에 그리고, three 는 기본 프레임버퍼에 그릴 때만 재질에서 톤 매핑을 적용하기 때문이다.
- 심도를 항상 켜면 개요 화면의 천체와 별이 뭉개져 읽을 수 없었다.

### 2.1 시제품 측정 (확정 값 적용 후)

장면의 재질에서 선형 휘도를 추정했다. 천체는 `lum(emissive) × emissiveIntensity + 0.2 × lum(color)`, Orb 는 `lum(uColor) × (0.8 + uRim)` 이다 (`orbGain` 적용 후).

| 상태 | 천체 상위 휘도 | Orb 상위 휘도 |
|---|---|---|
| 대기 | 0.41, 0.36, 0.33, 0.28 … (40개 모두 0.5 미만) | 0.46, 0.46, 0.39 … (28개 모두 0.5 미만) |
| 4코어 부하 | 0.74, 0.71, 0.69, 0.68, 그다음 0.33 | 2.38, 2.16, 1.81, 1.63, 1.61, 그다음 0.48 |

임계값 0.5 (0.75 에서 완전히 번짐)이면 대기 중에는 천체·Orb 가 번지지 않고(기존 후광만 보인다), 부하 중에는 바쁜 천체 4개와 뜨거운 코어 5개가 번진다. 둘 사이의 간격이 넓어 임계값이 흔들려도 결과가 같다.

1920×1080(dpr 1)에서 전체 파이프라인 프레임 시간은 중앙값 4.5 ms, p90 5.2 ms 다. 빌드 크기는 1.36 MB → 1.47 MB.

### 2.2 검토에서 확인한 점

배경 `#03040a` 는 ACES 를 거치면 순검정에 가까워진다 (허용). HiDPI(dpr 1.5·2)와 반정밀 렌더 타깃 미지원 환경은 측정·탐지하지 않았다 → M9.

Focus 시 초점 천체와 위성은 선명하고 배경 천체와 별은 흐려졌다. Esc 뒤 다시 선명해졌다. 카메라 가까이의 먼지는 몇 픽셀 크기의 점으로 보였고 Bloom 으로 번지지 않았다(휘도가 임계값보다 훨씬 낮다).

## 3. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D43 | 장면을 HalfFloat 선형 HDR 버퍼에 그리고, 심도 → Bloom → ACES 톤 매핑을 한 번씩 거친다 | 모든 재질이 같은 톤 매핑을 거친다. M6 의 톤 매핑 차이가 구조적으로 사라진다 |
| D44 | Bloom 은 선형 휘도 임계값으로 고른다. 선택적 Bloom(레이어)은 쓰지 않는다 | 이미 활동도가 발광 세기로 옮겨져 있다(emissive 0.15 + 1.6·a). 밝기가 곧 "번질 자격" 이다 |
| D45 | 심도는 Focus 중에만. 초점은 OrbitControls 의 target, 흐림 세기는 `focus.weight` 에 비례 | 사용자 선택. 개요 화면의 판독성을 지킨다 |
| D46 | 초점 dim 계산을 `scene/interaction.ts` 의 `dimFor` 하나로 모은다 | ProcessNode 와 FlowStreams 가 같은 전환을 따른다 |
| D47 | Orb `uColor` 를 sRGB → 선형으로 바꿔 넣고, 부하에 비례해 HDR 로 밝힌다 | 버퍼가 선형이므로 변환하지 않으면 색이 뜬다. 뜨거운 코어만 번지게 한다 |

### 3.1 되돌리기 쉬운 것과 어려운 것

임계값·강도·반경·Orb HDR 배율·흐림 세기는 `visual/postfx.ts` 의 상수다. 되돌리기 쉽다. 되돌리기 어려운 것은 D43(장면이 렌더 타깃에 그려진다는 전제)이다. 이후 추가되는 재질은 선형 출력을 가정해야 한다.

## 4. 파이프라인 (`scene/PostEffects.tsx`)

```
장면 → HalfFloat 렌더 타깃 (선형 HDR, 깊이 포함)
     → DepthOfField   (Focus 중에만 세기 > 0)
     → Bloom          (mipmapBlur, 휘도 임계값)
     → ToneMapping    (ACES Filmic)
     → 화면 (sRGB 출력)
```

- `EffectComposer` 는 `SceneRoot` 안, 컨텍스트 제공자 아래에 마운트한다. 초점 상태(`FocusFrame`)를 읽기 위해서다.
- composer 의 `multisampling` 은 기본값(8)을 쓴다. 2.1절의 1080p 프레임 시간은 이 값으로 잰 것이다.
- 톤 매핑은 효과 하나로만 한다. composer 의 장면은 렌더 타깃에 그려지고, 렌더 타깃에 그릴 때 three 는 재질에 톤 매핑을 적용하지 않는다. 또 `@react-three/postprocessing` 은 마운트되어 있는 동안 `gl.toneMapping` 을 `NoToneMapping` 으로 강제한다. 따라서 `ToneMapping` 효과가 유일한 톤 매핑이다. composer 밖에서 그리는 것은 톤 매핑을 받지 못하므로 D43 의 전제(모든 재질이 composer 안에서 그려진다)가 중요하다.
- 세 효과(심도, Bloom, 톤 매핑)는 래퍼가 하나의 `EffectPass` 로 합친다. Bloom 은 심도를 거치지 않은 선명한 장면을 읽고, 그 결과가 심도 결과 뒤에 더해진다.

## 5. Bloom (D44)

- `luminanceThreshold` 0.5, `luminanceSmoothing` 0.25, `intensity` 0.8, mipmap `radius` 0.7. `visual/postfx.ts` 의 상수다.
- 목표: 대기 상태에서 번지는 것은 활동도가 높은 소수의 천체뿐이다. 부하를 걸면 뜨거운 코어, 그 코어로 가는 flow 선, 불꽃이 번진다. 한가한 천체, 먼지, 별은 번지지 않는다.
- 값은 2.1절의 측정으로 정했다.

## 6. 코어 Orb 색 (D47, `scene/CoreOrb.tsx`, `visual/postfx.ts`)

- `uColor` 는 `coreColor(load)` 의 sRGB 값을 `Color.setRGB(r, g, b, SRGBColorSpace)` 로 선형으로 바꿔 넣는다.
- HDR 배율 `orbGain(load) = 1 + (ORB_GAIN_MAX − 1) × load²`, `ORB_GAIN_MAX = 2.2` 를 곱한다. 제곱이므로 한가한 코어는 1 근처(번지지 않음)에 머물고, 뜨거운 코어만 임계값을 넘는다.
- 후광·불꽃은 이미 `SRGBColorSpace` 로 넣고 있다. 바꾸지 않는다.

## 7. 심도 (D45)

- 초점 목표: `state.controls.target` (OrbitControls). Focus 중에는 카메라 연출이 이 target 을 초점 천체로 옮긴다. 초점 대상의 좌표를 따로 구하지 않는다.
- 흐림 세기: `bokehFor(weight) = BOKEH_SCALE × clamp(weight, 0, 1)`, `BOKEH_SCALE = 4`. 개요 화면(weight 0)에서는 0 이다. NaN 은 0 으로 본다.
- 초점 범위 `focusRange = 12` (월드 단위). 초점 천체와 그 위성이 선명하게 남는다.
- 매 프레임 `useFrame` 에서 효과 객체의 `target` 과 `bokehScale` 을 갱신한다. 우선순위는 새 `FRAME_PRIORITY.postfx = -0.1` (모든 기준점이 정해진 뒤). 렌더 자체는 EffectComposer 가 양수 우선순위에서 맡는다.

## 8. 초점 dim 공용화 (D46, `scene/interaction.ts`)

`ProcessNode.tsx` 의 `dimFor(focus, key)`(A→B 전환 중 `focus.t` 로 교차 페이드)를 `interaction.ts` 로 옮겨 export 한다. `ProcessNode` 와 `FlowStreams` 가 이것을 쓴다. `FlowStreams` 의 자체 `dimFor` 는 지운다. 동작은 ProcessNode 의 현재 동작 그대로다.

초점 dim 으로만 투명해진 천체는 `depthWrite` 를 켠 채로 둔다(존재 페이드 중일 때만 끈다). 심도가 깊이로 흐림을 정하므로, 그래야 A→B 이동 중에도 심도가 끊기지 않고 이어진다.

## 9. 먼지 크기

시제품에서 확인했다(2.1절). 먼지는 Bloom 으로 번지지 않는다. 크기 상한은 M8 에서 두지 않고 M9 로 넘긴다.

## 10. 모듈 구조

```
web/src/visual/postfx.ts         상수(Bloom, 심도), bokehFor, orbGain
web/src/scene/PostEffects.tsx    EffectComposer + 심도·Bloom·톤 매핑, 심도 갱신
web/src/scene/interaction.ts     dimFor 추가 (ProcessNode 에서 이동)
web/src/scene/ProcessNode.tsx    공용 dimFor 사용
web/src/scene/FlowStreams.tsx    공용 dimFor 사용
web/src/scene/CoreOrb.tsx        선형 uColor × orbGain
web/src/scene/SceneRoot.tsx      PostEffects 마운트
web/src/scene/framePriority.ts   postfx 우선순위
web/src/scene/coreShader.ts      uColor 주석(선형 HDR)
web/package.json                 postprocessing 6.39.5, @react-three/postprocessing 3.1.3
```

`visual/**` 순수성 규칙은 그대로다. `postfx.ts` 는 three 를 import 하지 않는다.

## 11. 실패 동작

- WebGL2 가 없거나 HalfFloat 렌더 타깃을 만들 수 없는 환경: 기존 WebGL 탐지가 대시보드로 안내한다. 별도 처리를 더하지 않는다.
- 장면 오류는 기존 `SceneErrorBoundary` 가 잡는다.

## 12. 테스트 전략

| 대상 | 방식 |
|---|---|
| `bokehFor`, `orbGain` | node 단위 테스트 (`tests/visual/postfx.test.ts`): 경계(0, 1), 단조성, 범위 밖 입력 자르기 |
| 공용 `dimFor` | jsdom 프로젝트 단위 테스트 (`tests/focusDim.test.ts`): 초점 없음, 초점 그룹, 무관 그룹, A→B 전환 중 교차 페이드 |
| 장면 | 자동 테스트하지 않는다(계약서 10절). 컨트롤러가 실제 엔진에 붙여 확인 |

## 13. 완료 조건

- 테스트, 타입체크, 린트(기존 경고 1개), 빌드 통과.
- 브라우저 (dev 서버 + 엔진):
  - 대기 중에는 활동도가 높은 소수의 천체만 번진다. 부하를 걸면 뜨거운 코어와 flow 가 빛난다.
  - Orb 색이 M6 와 같은 계열이다(뜨지 않음).
  - 천체를 클릭하면 배경이 서서히 흐려지고, Esc 로 다시 선명해진다.
  - Focus A→B 전환에서 flow 선의 dim 이 천체와 함께 바뀐다.
  - 1080p 창에서 프레임 시간 중앙값 8 ms 이하.
  - 콘솔 오류 없음. 엔진이 서빙하는 빌드에서도 같다.

## 14. 범위 밖

- 선택적 Bloom, SSAO, 색수차, 비네트 등 추가 효과.
- 인스턴싱·프레임당 할당 제거·버퍼 부분 업로드 (M9).
- 효과를 끄는 사용자 설정.

## 15. 구현 순서

1. 순수 계산: `visual/postfx.ts` + 테스트.
2. 공용 `dimFor`: `interaction.ts` 로 이동, ProcessNode·FlowStreams 적용 + 테스트.
3. 장면: 의존성 추가, `PostEffects`, `CoreOrb` 색, SceneRoot 마운트.
