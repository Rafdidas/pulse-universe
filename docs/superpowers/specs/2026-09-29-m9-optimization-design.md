# M9 — 최적화 설계

- 작성일: 2026-09-29
- 상태: 승인됨 (시제품 측정 반영)
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md`, M4~M8 스펙, M1~M8 구현 (`main`)
- 범위: 측정으로 찾은 병목(고해상도에서의 GPU 픽셀 채우기)을 줄이고, 느린 GPU 를 위한 자동 해상도와 화면 성능 표시를 더한다. 엔진은 바꾸지 않는다.

## 1. 이 문서의 위치

계약서 8절의 M9 완료 조건은 "인스턴싱, 오브젝트 풀, 성능 측정" 이다. 이 문서는 먼저 측정하고, 측정이 가리키는 곳만 고친다. 인스턴싱과 오브젝트 풀은 측정상 줄일 것이 없어 하지 않는다(2절, D52). M6~M8 이 넘긴 항목 중 이 결론에 맞는 것만 다룬다.

## 2. 측정

환경: NVIDIA GeForce RTX 5050 (ANGLE D3D11), Windows 11, 28 논리 코어, 엔진 Debug 빌드. 브라우저 창이 가려져 `requestAnimationFrame` 이 멈추므로 R3F `advance()` 로 프레임을 직접 돌리고 `readPixels` 로 GPU 를 동기화해 프레임 하나의 시간을 쟀다 (60회 중앙값 / p90). 캔버스 크기는 `setSize(1920, 1080)` 와 `setDpr` 로 정했다.

### 2.1 현재 (M8)

| 구성 | dpr 1 (1920×1080) | dpr 1.5 (2880×1620) | dpr 2 (3840×2160) |
|---|---|---|---|
| M8 그대로 (MSAA 8, 심도 상시, 캔버스 안티에일리어싱) | 3.1~3.7 / 3.8~4.6 ms | 6.1 / 7.3 ms | 8.6~9.9 / 9.5~11.8 ms |

### 2.2 무엇이 비용인가 (dpr 2 기준 중앙값)

| 바꾼 것 | dpr 1 | dpr 2 |
|---|---|---|
| 없음 (MSAA 8) | 3.1 ms | 8.6 ms |
| MSAA 4 | 2.1~2.4 ms | 7.6 ms |
| 심도 패스 제거 (MSAA 8) | 1.4~1.7 ms | 5.2~5.3 ms |
| MSAA 4 + 심도 제거 + 캔버스 안티에일리어싱 끔 | 1.4~1.5 ms | 2.7~2.8 ms |

심도 패스는 Focus 가 없어 흐림 세기가 0 일 때도 CoC·블러·마스크·보케 패스를 모두 돈다 (dpr 2 에서 약 3.3 ms). 캔버스 자체 안티에일리어싱은 후처리 뒤에 전체화면 사각형 하나만 그리므로 효과가 없다.

### 2.3 JS 와 엔진

- 프레임당 JS (렌더 호출을 비우고 `advance()` 500회 평균): 대기 0.094 ms. 4코어 부하·flow 30개: 중앙값 0.1 ms, p99 0.3 ms, 최대 0.7 ms.
- 12초 동안 실제 렌더를 포함해 프레임을 돌렸을 때 50 ms 넘는 긴 작업(longtask) 0건. 프레임 중앙값 0.9 ms, 최대 2.1 ms (800px, dpr 1).
- draw call 152, 삼각형 23만, 점 4,616, 선 120 (대기, 프레임 합산). useFrame 구독자 80.
- 엔진: CPU 26.6 ms/초 (코어 하나의 2.7%), 작업 집합 15 MB.

결론: 병목은 고해상도에서의 GPU 픽셀 채우기다. JS·draw call 은 여유가 크다.

### 2.4 시제품 결과 (D48 적용, Focus 없음)

| dpr | 1 (1920×1080) | 1.5 (2880×1620) | 2 (3840×2160) |
|---|---|---|---|
| 중앙값 / p90 | 1.5~2.7 / 1.7~3.6 ms | 2.0 / 2.2~2.4 ms | 2.8 / 3.6~3.7 ms |

dpr 2 에서 8.6~9.9 ms → 2.8 ms. draw call 은 152 → 141 (심도 패스 제외).

Focus 를 잡을 때 심도 패스가 composer 에 들어가며 셰이더 프로그램이 12 → 20 개가 된다. 그 구간의 프레임 간격은 83 ms (평소 약 21 ms, FocusPanel 마운트·GSAP 전환 시작 포함), 그 프레임의 렌더는 15.3 ms 였다. 50 ms 를 넘는 긴 작업은 0건이다. 초점을 푼 뒤 1초가 지나 패스가 빠질 때는 프로그램이 21 → 13 개로 줄고 가장 긴 프레임은 4.5 ms 였다. 기준(100 ms) 안이므로 composer 두 벌 방식은 쓰지 않는다.

## 3. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D48 | 심도 패스는 Focus 중에만 composer 에 넣는다. MSAA 는 4, 캔버스 안티에일리어싱은 끈다 | 2.2절. dpr 2 에서 8.6 → 약 2.7 ms |
| D49 | drei `PerformanceMonitor` 로 실제 fps 를 보고 dpr 을 `[1, 기기 dpr]` 사이에서 단계적으로 조정한다 | 이 기계에서는 거의 작동하지 않는다. 느린 내장 GPU 의 안전장치다 |
| D50 | `P` 키로 켜고 끄는 성능 표시: 프레임 ms, fps, dpr, draw call, 삼각형 | 계약서의 "성능 측정". 값은 React 상태를 거치지 않는다 |
| D51 | 반정밀 렌더 타깃을 쓸 수 없으면 8비트 버퍼로 그린다 | 지금은 검은 화면이 된다. Bloom 은 덜 밝아진다 |
| D52 | 인스턴싱·오브젝트 풀·그 밖의 프레임당 할당 제거는 하지 않는다 | 2.3절. 프레임당 JS 0.1 ms 에서 줄일 것이 없다. 호버·클릭·Focus 를 다시 짜야 하는 비용이 크다 |
| D53 | FlowStreams 위상 정리(O(P·E))와 FlowTracker 의 프레임당 배열 복사만 고친다 | 선이 최대 96개일 때를 대비한 값싼 정리 |

### 3.1 되돌리기 쉬운 것과 어려운 것

전부 되돌리기 쉽다. MSAA 값, dpr 경계, 성능 표시 갱신 주기는 상수다. 심도 패스를 넣고 빼는 방식(D48)만 composer 가 셰이더를 다시 만드는 비용이 있다. 시제품에서 쟀다(2.4절).

## 4. 심도 패스 조건부 실행 (D48, `scene/PostEffects.tsx`)

- `DepthOfField` 는 `focusedKey !== null` 이거나, 초점이 풀린 뒤 카메라 전환 시간(`FOCUS_TRANSITION_SEC` = 1초)이 지나기 전일 때만 composer 에 들어간다. 흐림 세기는 지금처럼 `bokehFor(focus.weight)` 로 매 프레임 정한다.
- 효과 목록이 바뀌면 `@react-three/postprocessing` 이 EffectPass 를 다시 만들고 셰이더를 새로 컴파일한다. 잰 값은 2.4절(가장 긴 프레임 간격 83 ms, 기준 100 ms 안).
- 해제 판정: 초점이 풀릴 때마다 번호를 1 늘리고, 번호가 바뀔 때마다 `FOCUS_TRANSITION_SEC` 타이머를 새로 건다. 풀고 곧바로 다시 잡았다 풀어도 마지막 해제부터 잰다.
- `EffectComposer` 의 `multisampling={4}`. Canvas 의 `gl={{ antialias: false }}`.

## 5. 자동 해상도 (D49)

- `Universe.tsx` 의 Canvas 안에 drei `PerformanceMonitor` 를 둔다. `onChange({ factor })` 에서 `setDpr(dprFor(factor, window.devicePixelRatio))`.
- `dprFor(factor, deviceDpr)` (`visual/perf.ts`): `1 + (maxDpr − 1) × clamp(factor, 0, 1)`, `maxDpr = min(2, max(1, deviceDpr))`, 0.25 단위로 내림. NaN 은 1 로 본다.
- 시작 factor 는 1(최대 해상도). 경계·flipflop 은 drei 기본값을 쓴다.
- Canvas 의 `dpr` 과 `gl` 옵션은 모듈 상수로 넘긴다. 재렌더마다 새 배열을 넘기면 R3F 가 바뀐 값으로 보고 자동 해상도가 정한 dpr 을 덮어쓸 수 있다.
- 알려진 한계: 창이 가려져 브라우저가 프레임을 크게 줄이면(예: 초당 3 프레임) 자동 해상도는 dpr 을 1 로 낮추고, 다시 보이면 서서히 올린다.

## 6. 성능 표시 (D50)

- `scene/PerfMeter.tsx` 는 Canvas 안에서 useFrame 으로 값을 모으고, `document.body` 에 붙인 작은 고정 위치 요소의 textContent 를 초당 2번 바꾼다. 보이는지 여부만 React 상태다(`P` 키).
- 프레임 시간은 useFrame 의 `delta` 로 모은다. 우선순위는 새 `FRAME_PRIORITY.meter = -2` (가장 먼저). 통계(평균 ms, fps, 최대 ms)는 `visual/perf.ts` 의 `FrameStats` 가 계산한다.
- draw call·삼각형은 composer 가 한 프레임에 여러 번 렌더하므로 `gl.info.autoReset = false` 로 두고 프레임 시작(가장 이른 우선순위)에 앞 프레임 합계를 읽은 뒤 `reset()` 한다. 표시가 꺼지면 `autoReset` 을 되돌린다.
- 입력창에 포커스가 있을 때 `P` 는 무시한다 (기존 `D` 토글과 같은 규칙). 이 규칙(물리 키·글자·IME·반복·수정 키)을 `shell/shortcut.ts` 의 `isShortcut(event, letter)` 로 모아 Shell 의 `D` 와 Universe 의 `P` 가 함께 쓴다.
- 표시 형식은 `visual/perf.ts` 의 `formatPerf` 가 만든다: `3.5 ms · 289 fps · max 7.0 ms` / `dpr 1.5 · 152 calls · 230k tris`.

## 7. 반정밀 버퍼 탐지 (D51)

WebGL 탐지를 `Universe.tsx` 에서 `scene/renderSupport.ts` 로 옮기고, WebGL2 컨텍스트가 `EXT_color_buffer_float` 또는 `EXT_color_buffer_half_float` 를 지원하는지 함께 본다. 지원하지 않으면 `PostEffects` 는 `frameBufferType` 을 `UnsignedByteType` 으로 쓴다. 판정은 모듈 로드 시 한 번이다.

## 8. 값싼 정리 (D53)

- `FlowStreams.tsx`: 위상 맵 정리를 `edges.some(...)` 대신 이번 프레임의 edge key `Set` 으로 한다. Set 은 컴포넌트가 한 번 만들어 프레임마다 비운다.
- `flowTracker.ts`: `update()` 의 `[...this.items.values()]` 복사를 없애고 Map 을 직접 순회한다 (순회 중 삭제는 JS Map 에서 안전하다). 동작은 같다 — 기존 테스트가 그대로 통과해야 한다.

## 9. 모듈 구조

```
web/src/visual/perf.ts          dprFor, FrameStats, formatPerf
web/src/scene/renderSupport.ts  WebGL·반정밀 렌더 타깃 탐지 (Universe 에서 이동)
web/src/scene/PerfMeter.tsx     성능 표시
web/src/scene/PostEffects.tsx   심도 조건부, MSAA 4, 버퍼 타입
web/src/scene/Universe.tsx      antialias 끔, PerformanceMonitor, P 키, PerfMeter
web/src/scene/framePriority.ts  meter 우선순위
web/src/shell/shortcut.ts       isShortcut (Shell 에서 이동)
web/src/shell/Shell.tsx         isShortcut 사용
web/src/shell/shell.css         .perf-meter
web/src/scene/FlowStreams.tsx   위상 정리
web/src/visual/flowTracker.ts   순회 복사 제거
```

## 10. 실패 동작

- `PerformanceMonitor` 가 계속 떨어뜨려도 dpr 은 1 아래로 가지 않는다.
- 성능 표시가 켜진 채 Universe 가 언마운트되면 요소를 지우고 `gl.info.autoReset` 을 되돌린다.

## 11. 테스트 전략

| 대상 | 방식 |
|---|---|
| `dprFor`, `FrameStats`, `formatPerf` | node 단위 테스트 (`tests/visual/perf.test.ts`): 경계, 내림 단위, NaN, 통계 창, 표시 형식 |
| `isShortcut` | jsdom 테스트 (`tests/shortcut.test.ts`): 물리 키·글자·IME, 수정 키·반복·조합·입력창. Shell 의 기존 D 테스트가 그대로 통과 |
| `flowTracker` 순회 변경 | 기존 10개 테스트가 그대로 통과 |
| 장면·성능 표시 | 컨트롤러가 브라우저에서 확인 (jsdom 에는 WebGL 이 없어 Universe 가 안내문만 그린다) |

## 12. 완료 조건

- 테스트, 타입체크, 린트(기존 경고 1개), 빌드 통과.
- 1920×1080 dpr 2 에서 Focus 없는 프레임 시간 중앙값 4 ms 이하 (2절 방식).
- Focus 를 잡을 때·풀 때의 가장 긴 프레임을 재어 기록.
- `P` 로 성능 표시가 켜지고 꺼진다. draw call 이 프레임 합계로 보인다.
- 화면이 M8 과 같게 보인다 (안티에일리어싱 차이는 스크린샷 비교로 확인).
- 콘솔 오류 없음. 엔진이 서빙하는 빌드에서도 같다.

## 13. 범위 밖

인스턴싱, 오브젝트 풀, sparkPosition·coreColor 등 그 밖의 프레임당 할당, 버퍼 부분 업로드, 공유 구 지오메트리, 먼지 크기 상한 (M8 에서 문제 없음 확인). 측정이 바뀌면 다시 본다.

## 14. 구현 순서

1. 순수 계산: `visual/perf.ts` + 테스트.
2. 정리: FlowTracker 순회, FlowStreams 위상 정리.
3. 단축키 공용화: `shell/shortcut.ts` + 테스트, Shell 적용.
4. 장면: renderSupport, PostEffects 조건부 심도·MSAA·버퍼 타입, Universe (antialias, PerformanceMonitor, P), PerfMeter, framePriority, CSS.
