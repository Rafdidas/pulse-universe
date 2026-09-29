# M8 — 포스트프로세싱 + 비주얼 마감 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 장면 전체를 선형 HDR 버퍼에 그린 뒤 심도(Focus 중에만) → Bloom → ACES 톤 매핑을 한 번씩 거치게 하고, Focus 전환 dim 을 천체와 flow 가 함께 따르게 한다.

**Architecture:** 상수와 계산(`bokehFor`, `orbGain`)은 `web/src/visual/postfx.ts` 의 순수 모듈이다. 초점 dim 계산은 `scene/interaction.ts` 의 `dimFor` 하나로 모아 ProcessNode 와 FlowStreams 가 함께 쓴다. `scene/PostEffects.tsx` 가 `@react-three/postprocessing` 의 EffectComposer 를 SceneRoot 안에 두고, 매 프레임 심도 초점(OrbitControls target)과 세기(focus.weight)를 갱신한다. 코어 Orb 는 선형 색 × HDR 배율을 쓴다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / postprocessing 6.39.5 / @react-three/postprocessing 3.1.3 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-29-m8-final-look-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`.
- 작업 브랜치는 `feature/m8-final-look` 다. `engine/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `@react-three/*`, `postprocessing`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다.
- 프레임 값은 React 상태에 넣지 않는다. useFrame 우선순위는 전부 음수다 (렌더는 EffectComposer 가 양수 우선순위에서 맡는다).
- oxlint 에 새 경고가 생기면 안 된다 (특히 `react(immutability)`).
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 테스트·타입체크·린트·빌드가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
명령은 C:\dev\pulse-uni\web 에서 실행한다.
```

기준선(작업 전 `main` + 스펙 커밋): 웹 254 tests 통과.

## File Structure

```
web/src/visual/postfx.ts          Bloom·심도 상수, bokehFor, ORB_GAIN_MAX, orbGain
web/src/scene/interaction.ts      (전체 교체) dimFor 추가 (ProcessNode 에서 이동)
web/src/scene/ProcessNode.tsx     (전체 교체) 공용 dimFor 사용
web/src/scene/FlowStreams.tsx     (전체 교체) 공용 dimFor 사용
web/src/scene/PostEffects.tsx     EffectComposer + 심도·Bloom·톤 매핑, 심도 갱신
web/src/scene/framePriority.ts    (전체 교체) postfx 우선순위
web/src/scene/CoreOrb.tsx         (전체 교체) 선형 uColor × orbGain
web/src/scene/coreShader.ts       (전체 교체) uColor 주석
web/src/scene/SceneRoot.tsx       (전체 교체) PostEffects 마운트
web/package.json, package-lock.json  postprocessing, @react-three/postprocessing
web/tests/visual/postfx.test.ts
web/tests/focusDim.test.ts
```

---

## Task 1: 후처리 상수와 계산

**Files:**
- Create: `web/src/visual/postfx.ts`
- Test: `web/tests/visual/postfx.test.ts`

**Interfaces:**
- Produces:
  - `BLOOM_THRESHOLD = 0.5`, `BLOOM_SMOOTHING = 0.25`, `BLOOM_INTENSITY = 0.8`, `BLOOM_RADIUS = 0.7`
  - `BOKEH_SCALE = 4`, `FOCUS_RANGE = 12`, `bokehFor(weight: number): number`
  - `ORB_GAIN_MAX = 2.2`, `orbGain(load: number): number`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/postfx.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { BOKEH_SCALE, ORB_GAIN_MAX, bokehFor, orbGain } from '../../src/visual/postfx';

describe('bokehFor', () => {
  it('does not blur the overview and blurs fully when focused', () => {
    expect(bokehFor(0)).toBe(0);
    expect(bokehFor(1)).toBe(BOKEH_SCALE);
    expect(bokehFor(0.5)).toBeCloseTo(BOKEH_SCALE / 2, 9);
  });

  it('clamps weights outside 0..1 and treats NaN as no focus', () => {
    expect(bokehFor(-1)).toBe(0);
    expect(bokehFor(2)).toBe(BOKEH_SCALE);
    expect(bokehFor(Number.NaN)).toBe(0);
  });
});

describe('orbGain', () => {
  it('leaves an idle core at 1 and brightens a saturated core to the maximum', () => {
    expect(orbGain(0)).toBe(1);
    expect(orbGain(1)).toBeCloseTo(ORB_GAIN_MAX, 9);
  });

  it('rises slowly at low load and grows with load', () => {
    expect(orbGain(0.2)).toBeLessThan(1.1);
    let previous = orbGain(0);
    for (let load = 0.1; load <= 1.0001; load += 0.1) {
      const gain = orbGain(load);
      expect(gain).toBeGreaterThan(previous);
      previous = gain;
    }
  });

  it('clamps loads outside 0..1 and treats NaN as idle', () => {
    expect(orbGain(-0.5)).toBe(1);
    expect(orbGain(3)).toBeCloseTo(ORB_GAIN_MAX, 9);
    expect(orbGain(Number.NaN)).toBe(1);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/postfx.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/postfx"`.

- [ ] **Step 3: `web/src/visual/postfx.ts`**

```ts
// M8 스펙 5~7절. 후처리 상수와 계산. 장면은 선형 HDR 버퍼에 그려지고
// 심도 → Bloom → ACES 톤 매핑을 한 번씩 거친다 (D43).

function clamp01(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(1, Math.max(0, value));
}

// Bloom (D44). 선형 휘도가 임계값을 넘는 것만 번진다.
export const BLOOM_THRESHOLD = 0.5;
export const BLOOM_SMOOTHING = 0.25;
export const BLOOM_INTENSITY = 0.8;
export const BLOOM_RADIUS = 0.7;

// 심도 (D45). Focus 중에만 흐려진다.
export const BOKEH_SCALE = 4;
export const FOCUS_RANGE = 12;

// 개요 화면(weight 0)에서는 흐리지 않는다.
export function bokehFor(weight: number): number {
  return BOKEH_SCALE * clamp01(weight);
}

// 코어 Orb 의 HDR 배율 (D47). 한가한 코어는 임계값 아래에 머물고, 뜨거운 코어만
// 임계값을 넘어 번진다.
export const ORB_GAIN_MAX = 2.2;

export function orbGain(load: number): number {
  const l = clamp01(load);
  return 1 + (ORB_GAIN_MAX - 1) * l * l;
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/postfx.test.ts`
Expected: PASS, `Tests  5 passed (5)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  259 passed (259)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 6: 커밋**

```
git add src/visual/postfx.ts tests/visual/postfx.test.ts
git commit -m "feat(web): add bloom and depth-of-field constants, focus blur and orb HDR gain"
```

---

## Task 2: 초점 dim 공용화

**Files:**
- Modify (전체 교체): `web/src/scene/interaction.ts`, `web/src/scene/ProcessNode.tsx`, `web/src/scene/FlowStreams.tsx`
- Test: `web/tests/focusDim.test.ts`

**Interfaces:**
- Consumes: `FocusFrame` 타입 (`scene/sceneContext.ts`: `key`, `previousKey`, `t`, `weight`), `DIM_DEPTH` (`scene/interaction.ts`).
- Produces: `dimFor(focus: Pick<FocusFrame, 'key' | 'previousKey' | 't' | 'weight'>, key: string): number` — `scene/interaction.ts` 에서 export. 동작은 기존 `ProcessNode.tsx` 의 모듈 내부 `dimFor` 와 같다 (A→B 전환 중 `focus.t` 로 교차 페이드). `FlowStreams.tsx` 의 자체 `dimFor` 는 지워진다.

- [ ] **Step 1: 실패하는 테스트 — `web/tests/focusDim.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { DIM_DEPTH, dimFor } from '../src/scene/interaction';

function frame(key: string | null, previousKey: string | null, t: number, weight: number) {
  return { key, previousKey, t, weight };
}

describe('dimFor', () => {
  it('leaves everything bright without a focus', () => {
    expect(dimFor(frame(null, null, 1, 0), 'a.exe:1')).toBe(1);
  });

  it('keeps the focused group bright and dims unrelated groups by the focus weight', () => {
    const focus = frame('a.exe:1', null, 1, 0.5);
    expect(dimFor(focus, 'a.exe:1')).toBe(1);
    expect(dimFor(focus, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);
  });

  it('keeps the previous group bright while the focus is released', () => {
    const focus = frame(null, 'a.exe:1', 1, 0.4);
    expect(dimFor(focus, 'a.exe:1')).toBe(1);
    expect(dimFor(focus, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.4, 9);
  });

  it('cross-fades from A to B with the camera transition while moving the focus', () => {
    const start = frame('b.exe:2', 'a.exe:1', 0, 1);
    expect(dimFor(start, 'a.exe:1')).toBe(1);
    expect(dimFor(start, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH, 9);

    const middle = frame('b.exe:2', 'a.exe:1', 0.5, 1);
    expect(dimFor(middle, 'a.exe:1')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);
    expect(dimFor(middle, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);

    const end = frame('b.exe:2', 'a.exe:1', 1, 1);
    expect(dimFor(end, 'a.exe:1')).toBeCloseTo(1 - DIM_DEPTH, 9);
    expect(dimFor(end, 'b.exe:2')).toBe(1);
    expect(dimFor(end, 'c.exe:3')).toBeCloseTo(1 - DIM_DEPTH, 9);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project jsdom tests/focusDim.test.ts`
Expected: FAIL — `dimFor` 가 `../src/scene/interaction` 에서 export 되지 않음 (`dimFor is not a function` 또는 동등한 import 오류).

- [ ] **Step 3: `web/src/scene/interaction.ts` 전체 교체**

```ts
import type { FocusFrame } from './sceneContext';

// 장면의 포인터 상호작용 상수. R3F 는 드래그 끝에 버튼을 뗀 것에도 onClick 을
// 부르므로, 움직인 거리(event.delta)가 이보다 크면 클릭으로 보지 않는다 (px).
// ProcessNode·SatelliteNode·CoreOrb 가 함께 쓴다.
export const CLICK_SLOP = 2;

// 초점이 잡히면 초점과 무관한 천체·코어의 발광·불투명도가 이만큼까지 줄어든다
// (M5 스펙 9.3). 1 − DIM_DEPTH × weight.
export const DIM_DEPTH = 0.75;

// 초점과 무관한 천체·선일수록 1 보다 작다. ProcessNode 와 FlowStreams 가 함께 쓴다 (M8 D46).
export function dimFor(focus: Pick<FocusFrame, 'key' | 'previousKey' | 't' | 'weight'>, key: string): number {
  // 초점이 A 에서 B 로 옮겨 가는 동안은 weight 가 1 로 유지되므로, 카메라 전환
  // 진행도 t 로 A 는 어두워지고 B 는 밝아지게 섞는다.
  if (focus.key !== null && focus.previousKey !== null && focus.previousKey !== focus.key) {
    if (key === focus.key) {
      return 1 - DIM_DEPTH * focus.weight * (1 - focus.t);
    }
    if (key === focus.previousKey) {
      return 1 - DIM_DEPTH * focus.weight * focus.t;
    }
    return 1 - DIM_DEPTH * focus.weight;
  }
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === key) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}
```

- [ ] **Step 4: `web/src/scene/ProcessNode.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Group,
  type Mesh,
  type MeshBasicMaterial,
  type MeshStandardMaterial,
} from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { floatingPosition } from '../visual/layout';
import {
  HALO_SCALE,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../visual/mapping';
import { presenceVisual } from '../visual/presence';
import { useFocusStore } from './focusStore';
import { CLICK_SLOP, dimFor } from './interaction';
import { useSceneContext } from './sceneContext';

const SALT_PHASE = 2;

interface Props {
  nodeKey: string;
  account: ProcessGroup['account'];
}

// 그룹 하나. 프레임 값은 React 상태를 거치지 않는다 — useFrame 에서 존재
// 추적기의 항목을 key 로 읽어 ref 를 직접 바꾼다. 떠나는 중인 천체는 고정된
// 마지막 값으로 그린다.
export function ProcessNode({ nodeKey, account }: Props) {
  const { cache, layout, presence, focus, setHovered } = useSceneContext();

  const root = useRef<Group>(null);
  const body = useRef<Mesh>(null);
  const bodyMaterial = useRef<MeshStandardMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 40 개가 동시에 숨 쉬지 않도록 초기 위상을 key 로 흩는다.
  const phase = useRef(hash01(nodeKey, SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, nodeKey);
    return new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  }, [account, nodeKey]);

  useFrame(() => {
    if (
      root.current === null ||
      body.current === null ||
      bodyMaterial.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const entry = presence.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    if (entry === undefined || position === undefined) {
      root.current.visible = false;
      return;
    }
    root.current.visible = true;

    const group = entry.value;
    const visual = presenceVisual(entry);
    const dim = dimFor(focus, nodeKey);
    const activity = activityFor(group.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);
    const scale =
      radiusFor(group.mem_mb) * visual.scale * (1 + pulse.amplitude * Math.sin(phase.current));

    root.current.position.set(position.x, position.y, position.z);
    body.current.scale.setScalar(scale);
    halo.current.scale.setScalar(scale * HALO_SCALE);

    const glow = glowFor(activity);
    const opacity = visual.opacity * dim;
    const material = bodyMaterial.current;
    material.emissiveIntensity = glow.emissiveIntensity * dim;
    material.opacity = opacity;
    // 불투명할 때는 transparent 를 끈다. 정렬 비용과 깊이 문제를 피한다.
    const transparent = opacity < 1;
    if (material.transparent !== transparent) {
      material.transparent = transparent;
      material.depthWrite = !transparent;
      material.needsUpdate = true;
    }
    haloMaterial.current.opacity = glow.haloOpacity * opacity;
  });

  return (
    <group ref={root} visible={false}>
      <mesh
        ref={body}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'group', key: nodeKey }));
        }}
        onPointerOut={() =>
          setHovered((current) =>
            current?.kind === 'group' && current.key === nodeKey ? null : current,
          )
        }
        onClick={(event) => {
          // R3F 는 드래그 끝에도 onClick 을 부른다. 궤도 조작을 초점 전환으로
          // 읽지 않도록 움직인 거리를 본다.
          if (event.delta > CLICK_SLOP) {
            return;
          }
          event.stopPropagation();
          const phase = presence.get(nodeKey)?.phase;
          if (phase === 'fading-out' || phase === 'collapsing') {
            return;
          }
          useFocusStore.getState().toggle(nodeKey);
        }}
      >
        <sphereGeometry args={[1, 32, 32]} />
        <meshStandardMaterial
          ref={bodyMaterial}
          color={color}
          emissive={color}
          roughness={0.55}
          metalness={0.1}
        />
      </mesh>
      {/* 헤일로는 호버 대상이 아니다. 광선 검사에서 빼야 뒤쪽 천체를 가리지 않는다. */}
      <mesh ref={halo} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          color={color}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 5: `web/src/scene/FlowStreams.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry, Color, SRGBColorSpace } from 'three';

import { coreColor, coreLoad } from '../visual/coreMapping';
import { coreIndexById, corePosition } from '../visual/coreRing';
import { curvePoint, flowControlPoint } from '../visual/flowCurve';
import { MAX_PARTICLES_PER_EDGE, advanceFlowPhase, particleCount, particleT } from '../visual/flowParticles';
import { FlowTracker, MAX_EDGES, edgeStrength, type FlowEdge } from '../visual/flowTracker';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { colorFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { dimFor } from './interaction';
import { useSceneContext, type Hovered } from './sceneContext';

// M7 스펙 7절.
const SEGMENTS = 24;
const PARTICLE_SIZE = 0.22;
// 계약서 3.1: source 에 따라 선명도만 달리한다. 입자는 같다.
const CLARITY: Record<FlowEdge['source'], number> = { estimated: 0.55, measured: 1.0 };
// 호버한 대상과 이어지지 않은 선의 밝기 (D42).
const UNRELATED = 0.3;

// 프레임마다 새로 만들지 않는 임시 값들.
const groupRgb = new Color();
const coreRgb = new Color();
const from: Vec3 = { x: 0, y: 0, z: 0 };
const to: Vec3 = { x: 0, y: 0, z: 0 };
const a: Vec3 = { x: 0, y: 0, z: 0 };
const b: Vec3 = { x: 0, y: 0, z: 0 };

// 호버한 그룹·코어와 이어진 선만 밝다. 호버가 없거나 위성이면 전부 밝다.
function highlightFor(hovered: Hovered, edge: FlowEdge, coreIndex: number): number {
  if (hovered === null || hovered.kind === 'satellite') {
    return 1;
  }
  if (hovered.kind === 'group') {
    return hovered.key === edge.group ? 1 : UNRELATED;
  }
  return hovered.index === coreIndex ? 1 : UNRELATED;
}

// 그룹 천체에서 코어 Orb 로 흐르는 빛. 추적기 갱신(잔광)과 선·입자 그리기를 한 곳에서
// 한다 — 곡선을 선마다 한 번만 계산한다. 존재 추적기와 레이아웃이 정해진 뒤에 돈다.
export function FlowStreams() {
  const { cache, layout, presence, focus, hover } = useSceneContext();
  const tracker = useMemo(() => new FlowTracker(), []);
  // 선마다의 흐름 위상. 누적한다 (advanceFlowPhase 참조).
  const phases = useRef(new Map<string, number>());

  const lineGeometry = useMemo(() => {
    const g = new BufferGeometry();
    const vertices = MAX_EDGES * SEGMENTS * 2;
    g.setAttribute('position', new BufferAttribute(new Float32Array(vertices * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(vertices * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  const particleGeometry = useMemo(() => {
    const g = new BufferGeometry();
    const capacity = MAX_EDGES * MAX_PARTICLES_PER_EDGE;
    g.setAttribute('position', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  useEffect(() => () => lineGeometry.dispose(), [lineGeometry]);
  useEffect(() => () => particleGeometry.dispose(), [particleGeometry]);

  useFrame(() => {
    const snapshot = cache.snapshot;
    if (snapshot === null || cache.timeSec === null) {
      tracker.reset();
      phases.current.clear();
      lineGeometry.setDrawRange(0, 0);
      particleGeometry.setDrawRange(0, 0);
      return;
    }

    const live = new Set(presence.entries().map((entry) => entry.key));
    tracker.update(snapshot.flows, live, cache.dtSec);

    const edges = tracker.edges();
    // 추적기에서 사라진 선의 위상을 버린다.
    for (const key of phases.current.keys()) {
      if (!edges.some((edge) => edge.key === key)) {
        phases.current.delete(key);
      }
    }

    const cores = snapshot.cores;
    const indexById = coreIndexById(cores);
    const hovered = hover.current;
    const linePositions = lineGeometry.getAttribute('position') as BufferAttribute;
    const lineColors = lineGeometry.getAttribute('color') as BufferAttribute;
    const particlePositions = particleGeometry.getAttribute('position') as BufferAttribute;
    const particleColors = particleGeometry.getAttribute('color') as BufferAttribute;
    let lineVertex = 0;
    let particle = 0;

    for (const edge of edges) {
      const entry = presence.get(edge.group);
      const coreIndex = indexById.get(edge.coreId);
      const start =
        entry === undefined ? undefined : floatingPosition(layout, edge.group, cache.timeSec);
      // 코어가 목록에 없으면 이번 프레임에는 그리지 않는다. 추적기에는 남는다.
      if (entry === undefined || coreIndex === undefined || start === undefined) {
        continue;
      }
      from.x = start.x;
      from.y = start.y;
      from.z = start.z;
      const end = corePosition(coreIndex, cores.length);
      to.x = end.x;
      to.y = end.y;
      to.z = end.z;
      const control = flowControlPoint(from, to, edge.key);

      const hsl = colorFor(entry.value.account, edge.group);
      groupRgb.setHSL(hsl.h / 360, hsl.s, hsl.l);
      const [cr, cg, cb] = coreColor(coreLoad(cores[coreIndex].pct));
      // coreColor 는 sRGB 값이다. 버텍스 색은 선형으로 읽히므로 바꿔 넣는다 (M6 불꽃과 같다).
      coreRgb.setRGB(cr, cg, cb, SRGBColorSpace);

      const strength = edgeStrength(edge);
      const shade = dimFor(focus, edge.group) * highlightFor(hovered, edge, coreIndex);
      const lineAlpha = Math.min(1, CLARITY[edge.source] * edge.intensity * (0.25 + 1.6 * edge.weight)) * shade;

      for (let s = 0; s < SEGMENTS; s += 1) {
        const t0 = s / SEGMENTS;
        const t1 = (s + 1) / SEGMENTS;
        curvePoint(from, control, to, t0, a);
        curvePoint(from, control, to, t1, b);
        linePositions.setXYZ(lineVertex, a.x, a.y, a.z);
        lineColors.setXYZ(
          lineVertex,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t0) * lineAlpha,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t0) * lineAlpha,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t0) * lineAlpha,
        );
        lineVertex += 1;
        linePositions.setXYZ(lineVertex, b.x, b.y, b.z);
        lineColors.setXYZ(
          lineVertex,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t1) * lineAlpha,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t1) * lineAlpha,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t1) * lineAlpha,
        );
        lineVertex += 1;
      }

      const phase = advanceFlowPhase(phases.current.get(edge.key) ?? 0, edge.weight, cache.dtSec);
      phases.current.set(edge.key, phase);
      const n = particleCount(strength);
      const glow = (0.4 + 0.6 * edge.intensity) * shade;
      for (let i = 0; i < n; i += 1) {
        const t = particleT(phase, i, n);
        curvePoint(from, control, to, t, a);
        particlePositions.setXYZ(particle, a.x, a.y, a.z);
        particleColors.setXYZ(
          particle,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t) * glow,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t) * glow,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t) * glow,
        );
        particle += 1;
      }
    }

    lineGeometry.setDrawRange(0, lineVertex);
    particleGeometry.setDrawRange(0, particle);
    linePositions.needsUpdate = true;
    lineColors.needsUpdate = true;
    particlePositions.needsUpdate = true;
    particleColors.needsUpdate = true;
  }, FRAME_PRIORITY.particles);

  return (
    <>
      {/* 선과 입자는 호버·클릭 대상이 아니다. 경계 구가 바뀌므로 절두체 선별도 끈다. */}
      <lineSegments geometry={lineGeometry} frustumCulled={false} raycast={() => null}>
        <lineBasicMaterial vertexColors transparent blending={AdditiveBlending} depthWrite={false} />
      </lineSegments>
      <points geometry={particleGeometry} frustumCulled={false} raycast={() => null}>
        <pointsMaterial
          size={PARTICLE_SIZE}
          sizeAttenuation
          vertexColors
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </points>
    </>
  );
}
```

- [ ] **Step 6: 통과 확인 (GREEN)**

Run: `npx vitest run --project jsdom tests/focusDim.test.ts`
Expected: PASS, `Tests  4 passed (4)`.

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  263 passed (263)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 8: 커밋**

```
git add src/scene/interaction.ts src/scene/ProcessNode.tsx src/scene/FlowStreams.tsx tests/focusDim.test.ts
git commit -m "refactor(web): share the focus dim so flows cross-fade with bodies on A to B focus moves"
```

---

## Task 3: 장면 — 후처리 파이프라인, Orb 선형 HDR 색

**Files:**
- Modify: `web/package.json`, `web/package-lock.json` (npm install 로)
- Create: `web/src/scene/PostEffects.tsx`
- Modify (전체 교체): `web/src/scene/framePriority.ts`, `web/src/scene/CoreOrb.tsx`, `web/src/scene/coreShader.ts`, `web/src/scene/SceneRoot.tsx`

**Interfaces:**
- Consumes: Task 1 의 `BLOOM_*`, `FOCUS_RANGE`, `bokehFor`, `orbGain`. `FocusFrame.weight` (`useSceneContext().focus`). drei `OrbitControls makeDefault` 가 등록한 `state.controls.target`.
- Produces: `PostEffects()` — SceneRoot 가 `<FlowStreams />` 다음에 마운트한다. `FRAME_PRIORITY.postfx = -0.1`.

이 Task 는 R3F 장면 코드라 jsdom 에서 자동 테스트하지 않는다(계약서 10절). 컨트롤러가 실제 엔진에 붙여 확인한다.

R3F·postprocessing 사항 (코드가 이미 반영하고 있다):
- `EffectComposer` 는 양수 우선순위의 useFrame 으로 렌더를 맡는다. 기존 useFrame 은 전부 음수이므로 먼저 돈다.
- composer 는 장면을 렌더 타깃에 그린다. three 는 렌더 타깃에 그릴 때 재질에서 톤 매핑을 하지 않으므로 `ToneMapping` 효과가 유일한 톤 매핑이다.
- `DepthOfFieldEffect.target` 에 Vector3 를 주면 효과가 매 렌더에 카메라와의 거리로 초점 거리를 계산한다. `focusRange` 는 월드 단위다.

- [ ] **Step 1: 의존성 추가**

Run: `npm install postprocessing@^6.39.5 @react-three/postprocessing@^3.1.3`
Expected: `web/package.json` 의 `dependencies` 에 정확히 두 줄이 알파벳 순서 자리에 더해진다:

```json
    "@react-three/postprocessing": "^3.1.3",
```

(`@react-three/fiber` 다음) 그리고

```json
    "postprocessing": "^6.39.5",
```

(`gsap` 다음). `package-lock.json` 이 함께 바뀐다. 설치된 버전은 `postprocessing` 6.39.5, `@react-three/postprocessing` 3.1.3 이어야 한다 (`npm ls postprocessing @react-three/postprocessing` 로 확인해 보고서에 적는다). 다른 버전이 잡히면 BLOCKED.

- [ ] **Step 2: `web/src/scene/framePriority.ts` 전체 교체**

```ts
// useFrame 우선순위. 작은 값이 먼저 돈다. 양수는 R3F 의 자동 렌더를 끄므로
// 전부 음수이고, 노드·위성 메시와 drei Html 은 기본값 0 에서 돈다.
//
//   scene       보간 → 존재 추적 → lifecycle → 레이아웃 → 버스트 생성
//   camera      Focus 전환 진행도로 카메라 자세 (노드 위치가 정해진 뒤)
//   satellites  위성 위치 (Focus 진행도가 정해진 뒤)
//   tooltip     툴팁 anchor (위성 위치가 정해진 뒤, Html 이 투영하기 전)
//   particles   입자 버퍼 (모든 기준점이 정해진 뒤)
//   postfx      심도 초점·세기 (카메라 target 과 Focus 강도가 정해진 뒤). 렌더는
//               EffectComposer 가 양수 우선순위에서 맡는다.
export const FRAME_PRIORITY = {
  scene: -1,
  camera: -0.8,
  satellites: -0.6,
  tooltip: -0.4,
  particles: -0.2,
  postfx: -0.1,
} as const;
```

- [ ] **Step 3: `web/src/scene/PostEffects.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { Bloom, DepthOfField, EffectComposer, ToneMapping } from '@react-three/postprocessing';
import type { DepthOfFieldEffect } from 'postprocessing';
import { ToneMappingMode } from 'postprocessing';
import { useRef } from 'react';
import { HalfFloatType, type Vector3 } from 'three';

import {
  BLOOM_INTENSITY,
  BLOOM_RADIUS,
  BLOOM_SMOOTHING,
  BLOOM_THRESHOLD,
  FOCUS_RANGE,
  bokehFor,
} from '../visual/postfx';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// drei OrbitControls(makeDefault)가 등록하는 controls 에서 쓰는 부분만.
interface Controls {
  target: Vector3;
}

// M8 스펙 4절. 장면을 선형 HDR 버퍼에 그린 뒤 심도 → Bloom → ACES 를 한 번씩 거친다.
// 톤 매핑은 여기 한 곳에서만 한다 — 렌더 타깃에 그릴 때 three 는 재질에서 톤 매핑을
// 하지 않는다.
export function PostEffects() {
  const { focus } = useSceneContext();
  const depthOfField = useRef<DepthOfFieldEffect>(null);

  // 초점은 OrbitControls 의 target 이다. Focus 중에는 카메라 연출이 target 을 초점
  // 천체로 옮기므로 초점 대상의 좌표를 따로 구하지 않는다 (스펙 7절).
  useFrame((state) => {
    const effect = depthOfField.current;
    if (effect === null) {
      return;
    }
    const controls = state.controls as unknown as Controls | null;
    effect.target = controls === null ? null : controls.target;
    effect.bokehScale = bokehFor(focus.weight);
  }, FRAME_PRIORITY.postfx);

  return (
    <EffectComposer frameBufferType={HalfFloatType}>
      <DepthOfField ref={depthOfField} focusRange={FOCUS_RANGE} bokehScale={0} />
      <Bloom
        mipmapBlur
        luminanceThreshold={BLOOM_THRESHOLD}
        luminanceSmoothing={BLOOM_SMOOTHING}
        intensity={BLOOM_INTENSITY}
        radius={BLOOM_RADIUS}
      />
      <ToneMapping mode={ToneMappingMode.ACES_FILMIC} />
    </EffectComposer>
  );
}
```

- [ ] **Step 4: `web/src/scene/coreShader.ts` 전체 교체**

```ts
// M6 스펙 5절. 코어 Orb 의 셰이더. 매핑(크기·진폭·색·발광)은 visual/coreMapping 의
// 순수 함수가 계산하고, 여기서는 uniform 으로 받아 그리기만 한다.
//
// uOffset    노이즈 시간 오프셋 (JS 에서 누적, advanceNoiseOffset)
// uAmplitude 정점 변위 진폭 (distortion)
// uColor     부하 색 (coreColor 를 선형으로 바꾼 값 × orbGain, 선형 HDR)
// uRim       가장자리 발광 세기 (rimIntensity)
// uOpacity   Focus 로 어두워질 때의 불투명도

// 3D simplex noise — Ian McEwan, Ashima Arts (webgl-noise), MIT License.
// https://github.com/ashima/webgl-noise
const SIMPLEX_NOISE_3D = /* glsl */ `
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 10.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v) {
  const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
  const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

  vec3 i = floor(v + dot(v, C.yyy));
  vec3 x0 = v - i + dot(i, C.xxx);

  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min(g.xyz, l.zxy);
  vec3 i2 = max(g.xyz, l.zxy);

  vec3 x1 = x0 - i1 + C.xxx;
  vec3 x2 = x0 - i2 + C.yyy;
  vec3 x3 = x0 - D.yyy;

  i = mod289(i);
  vec4 p = permute(permute(permute(
            i.z + vec4(0.0, i1.z, i2.z, 1.0))
          + i.y + vec4(0.0, i1.y, i2.y, 1.0))
          + i.x + vec4(0.0, i1.x, i2.x, 1.0));

  float n_ = 0.142857142857;
  vec3 ns = n_ * D.wyz - D.xzx;

  vec4 j = p - 49.0 * floor(p * ns.z * ns.z);

  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_);

  vec4 x = x_ * ns.x + ns.yyyy;
  vec4 y = y_ * ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);

  vec4 b0 = vec4(x.xy, y.xy);
  vec4 b1 = vec4(x.zw, y.zw);

  vec4 s0 = floor(b0) * 2.0 + 1.0;
  vec4 s1 = floor(b1) * 2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));

  vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
  vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

  vec3 p0 = vec3(a0.xy, h.x);
  vec3 p1 = vec3(a0.zw, h.y);
  vec3 p2 = vec3(a1.xy, h.z);
  vec3 p3 = vec3(a1.zw, h.w);

  vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;

  vec4 m = max(0.5 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
  m = m * m;
  return 105.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}
`;

// 노이즈 공간에서 법선을 이만큼 늘여 읽는다. 클수록 표면의 울퉁불퉁함이 잘다.
const NOISE_SCALE = '1.8';

export const coreVertexShader = /* glsl */ `
uniform float uOffset;
uniform float uAmplitude;
varying vec3 vNormalView;
varying vec3 vViewDir;
varying float vNoise;
${SIMPLEX_NOISE_3D}
void main() {
  float n = snoise(normal * ${NOISE_SCALE} + vec3(uOffset));
  vNoise = n;
  vec3 displaced = position + normal * n * uAmplitude;
  vec4 mvPosition = modelViewMatrix * vec4(displaced, 1.0);
  vNormalView = normalize(normalMatrix * normal);
  vViewDir = normalize(-mvPosition.xyz);
  gl_Position = projectionMatrix * mvPosition;
}
`;

export const coreFragmentShader = /* glsl */ `
uniform vec3 uColor;
uniform float uRim;
uniform float uOpacity;
varying vec3 vNormalView;
varying vec3 vViewDir;
varying float vNoise;
void main() {
  // 가장자리일수록 밝다 (fresnel). 노이즈로 표면에 얼룩을 조금 준다.
  float fresnel = pow(1.0 - max(dot(normalize(vNormalView), normalize(vViewDir)), 0.0), 2.0);
  vec3 base = uColor * (0.55 + 0.25 * vNoise);
  gl_FragColor = vec4(base + uColor * fresnel * uRim, uOpacity);
}
`;
```

- [ ] **Step 5: `web/src/scene/CoreOrb.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Mesh,
  type MeshBasicMaterial,
  SRGBColorSpace,
  type ShaderMaterial,
} from 'three';

import {
  CORE_HALO_SCALE,
  NOISE_PERIOD,
  advanceNoiseOffset,
  coreColor,
  coreHaloOpacity,
  coreLoad,
  distortion,
  orbRadius,
  rimIntensity,
} from '../visual/coreMapping';
import { corePosition } from '../visual/coreRing';
import { orbGain } from '../visual/postfx';
import { coreFragmentShader, coreVertexShader } from './coreShader';
import { CLICK_SLOP, DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

interface Props {
  index: number;
  count: number;
}

// M6 스펙 5절. 코어 하나. 고리 위 고정 자리에서 부하에 따라 커지고, 달아오르고,
// 일그러진다. 프레임 값은 useFrame 에서 uniform·scale 로만 바뀐다.
export function CoreOrb({ index, count }: Props) {
  const { cache, focus, setHovered } = useSceneContext();
  const body = useRef<Mesh>(null);
  const material = useRef<ShaderMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 초기 오프셋도 노이즈 주기 안에 둔다 (advanceNoiseOffset 참조).
  const noiseOffset = useRef((index * 7.3) % NOISE_PERIOD);

  const position = useMemo(() => corePosition(index, count), [index, count]);
  // 재질마다 제 uniform 객체를 가진다. 같은 셰이더 프로그램을 28 개가 나눠 쓴다.
  const uniforms = useMemo(
    () => ({
      uOffset: { value: 0 },
      uAmplitude: { value: 0 },
      uColor: { value: new Color() },
      uRim: { value: 0 },
      uOpacity: { value: 1 },
    }),
    [],
  );

  // R3F 는 언마운트된 메시를 onPointerOut 없이 호버 목록에서 뺀다. 코어 수가 줄거나
  // 스토어가 비면 호버가 남아 툴팁이 엉뚱한 곳에서 다시 뜨므로 정리 때 직접 푼다.
  useEffect(
    () => () =>
      setHovered((current) =>
        current?.kind === 'core' && current.index === index ? null : current,
      ),
    [index, setHovered],
  );

  useFrame(() => {
    if (
      body.current === null ||
      material.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const core = cache.snapshot?.cores[index];
    if (core === undefined) {
      body.current.visible = false;
      halo.current.visible = false;
      return;
    }
    body.current.visible = true;
    halo.current.visible = true;

    const load = coreLoad(core.pct);
    const radius = orbRadius(load);
    // 초점과 무관하므로 초점이 잡히면 다른 천체처럼 어두워진다.
    const dim = 1 - DIM_DEPTH * focus.weight;
    noiseOffset.current = advanceNoiseOffset(noiseOffset.current, load, cache.dtSec);

    body.current.scale.setScalar(radius);
    halo.current.scale.setScalar(radius * CORE_HALO_SCALE);

    const u = material.current.uniforms;
    const [r, g, b] = coreColor(load);
    u.uOffset.value = noiseOffset.current;
    u.uAmplitude.value = distortion(load);
    // 장면은 선형 HDR 버퍼에 그려진다. sRGB 값을 선형으로 바꾸고, 뜨거운 코어만
    // Bloom 임계값을 넘도록 부하에 비례해 밝힌다 (M8 D47).
    (u.uColor.value as Color).setRGB(r, g, b, SRGBColorSpace).multiplyScalar(orbGain(load));
    u.uRim.value = rimIntensity(load) * dim;
    u.uOpacity.value = dim;
    // 어둡지 않을 때는 불투명 패스에 둔다. 불투명 패스가 깊이를 먼저 써야 불꽃·먼지가
    // 깊이 검사로 Orb 뒤에서만 가려진다 (투명 패스에서는 정렬 순서에 따라 Orb 가 덮어쓴다).
    const transparent = dim < 1;
    if (material.current.transparent !== transparent) {
      material.current.transparent = transparent;
      material.current.depthWrite = !transparent;
      material.current.needsUpdate = true;
    }

    // coreColor 는 sRGB 값이다. 본체와 같이 선형으로 바꿔 넣는다.
    haloMaterial.current.color.setRGB(r, g, b, SRGBColorSpace);
    haloMaterial.current.opacity = coreHaloOpacity(load) * dim;
  });

  return (
    <group position={[position.x, position.y, position.z]}>
      <mesh
        ref={body}
        visible={false}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'core', index }));
        }}
        onPointerOut={() =>
          setHovered((current) =>
            current?.kind === 'core' && current.index === index ? null : current,
          )
        }
        onClick={(event) => {
          // 코어 클릭은 아무 동작이 없다. 뒤쪽 천체로 새어 초점이 바뀌지 않게만 막는다.
          if (event.delta <= CLICK_SLOP) {
            event.stopPropagation();
          }
        }}
      >
        <sphereGeometry args={[1, 48, 48]} />
        <shaderMaterial
          ref={material}
          vertexShader={coreVertexShader}
          fragmentShader={coreFragmentShader}
          uniforms={uniforms}
        />
      </mesh>
      <mesh ref={halo} visible={false} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 6: `web/src/scene/SceneRoot.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
import { Color } from 'three';

import { sample, type InterpolatedGroup } from '../state/interpolator';
import { useSnapshotStore } from '../state/snapshotStore';
import {
  CHILD_BORN,
  CHILD_DIED,
  GROUP_COLLAPSE,
  GROUP_FORM,
  BurstPool,
  type BurstAnchor,
  type BurstSpec,
} from '../visual/bursts';
import { createFrameCache, layoutNodesFrom, updateFrameCache } from '../visual/frameCache';
import { hash01 } from '../visual/hash';
import { LayoutSim } from '../visual/layout';
import { LifecycleConsumer, type LifecycleEvent } from '../visual/lifecycleEvents';
import { colorFor, radiusFor } from '../visual/mapping';
import { PresenceTracker } from '../visual/presence';
import { AmbientDust } from './AmbientDust';
import { CameraRig } from './CameraRig';
import { CoreRing } from './CoreRing';
import { CoreSparks } from './CoreSparks';
import { FlowStreams } from './FlowStreams';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { nodeIdsOf, parseNodeId } from './nodeList';
import { Particles } from './Particles';
import { PostEffects } from './PostEffects';
import { ProcessNode } from './ProcessNode';
import { Satellites } from './Satellites';
import {
  FocusFrame,
  HoverFrame,
  FrameEvents,
  SceneContext,
  type Hovered,
  type SceneContextValue,
} from './sceneContext';
import { Tooltip } from './Tooltip';

// 자식 버스트는 부모 천체 안쪽에서 일어나 보이도록 부모 반지름보다 작게 잡는다.
const CHILD_BURST_RADIUS = 0.5;

function rgbOf(group: Pick<InterpolatedGroup, 'account' | 'key'>): [number, number, number] {
  const hsl = colorFor(group.account, group.key);
  const color = new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  return [color.r, color.g, color.b];
}

// 이벤트 하나를 버스트 명세로. 기준 그룹을 모르면(이미 추적기에서도 사라짐) null.
function burstFor(
  event: LifecycleEvent,
  group: InterpolatedGroup | undefined,
  focusedKey: string | null,
  seq: number,
): BurstSpec | null {
  if (group === undefined) {
    return null;
  }
  const radius = radiusFor(group.mem_mb);
  const color = rgbOf(group);
  const seed = Math.floor(hash01(`${event.kind}:${event.pid}`, seq) * 0x100000000);
  const groupAnchor: BurstAnchor = { kind: 'group', key: event.key, groupKey: event.key };
  // Focus 중인 그룹의 자식이면 그 위성 자리에서 재생한다 (M5 스펙 D30).
  // 위성은 focus 프레임 값의 key 를 따르므로 스토어가 아니라 그것으로 판단한다.
  const childAnchor: BurstAnchor =
    focusedKey === event.key
      ? { kind: 'satellite', key: String(event.pid), groupKey: event.key }
      : groupAnchor;

  switch (event.kind) {
    case 'group-born':
      return { ...GROUP_FORM, anchor: groupAnchor, radius, color, seed };
    case 'group-died':
      return { ...GROUP_COLLAPSE, anchor: groupAnchor, radius, color, seed };
    case 'child-born':
      return { ...CHILD_BORN, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
    case 'child-died':
      return { ...CHILD_DIED, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
  }
}

// 스펙 4절(M4)과 M5 스펙 5~7절. 프레임당 한 번 보간하고, 존재 추적기와
// lifecycle 소비기를 갱신하고, 레이아웃을 한 스텝 진행하고, 이벤트를 버스트로
// 바꾼다. 노드 목록은 스토어가 아니라 존재 추적기의 항목이다 — 떠나는 천체도
// 연출이 끝날 때까지 그려야 한다.
export function SceneRoot() {
  const [nodeIds, setNodeIds] = useState<string[]>([]);
  const [hovered, setHovered] = useState<Hovered>(null);
  // 마지막으로 React 에 넘긴 존재 추적기 version.
  const shownVersion = useRef(-1);
  // 마지막으로 본 세션. null 프레임을 못 보고 세션이 바뀌어도 장면을 비우기 위해.
  const lastSession = useRef<string | null>(useSnapshotStore.getState().session);

  const context = useMemo<SceneContextValue>(
    () => ({
      cache: createFrameCache(),
      layout: new LayoutSim(),
      presence: new PresenceTracker<InterpolatedGroup>(),
      events: new FrameEvents(),
      focus: new FocusFrame(),
      hover: new HoverFrame(),
      satellites: new Map(),
      setHovered,
    }),
    [],
  );
  const consumer = useMemo(() => new LifecycleConsumer(), []);

  const pool = useMemo(() => new BurstPool(), []);

  useFrame(() => {
    // 시계는 performance.now() 다 — arrivedAt 이 같은 시계로 찍힌다.
    const now = performance.now();
    const state = useSnapshotStore.getState();
    const frame = sample(
      {
        previous: state.previous,
        current: state.current,
        arrivedAt: state.arrivedAt,
        intervalMs: state.intervalMs,
      },
      now,
    );
    const { cache, layout, presence } = context;
    updateFrameCache(cache, frame, now);
    const nowSec = now / 1000;

    const sessionChanged = state.session !== lastSession.current;
    lastSession.current = state.session;

    if (frame === null) {
      // 세션이 바뀌었거나 버전 불일치로 스토어가 비었다. 다음 스냅샷은 다시
      // "이미 있던 것" 으로 시작한다.
      presence.reset();
      consumer.reset();
      pool.clear();
      context.events.replace([]);
    } else {
      if (sessionChanged) {
        // 두 프레임 사이에 hello 와 다음 스냅샷이 함께 도착해 null 프레임을 못 봤다.
        // 이전 세션의 장면·초점을 비우고 이 스냅샷을 기준선으로 삼는다.
        // 레이아웃은 일부러 비우지 않는다. 엔진이 재시작해도 대부분의 name:pid key 가
        // 살아남으므로 천체가 있던 자리에 그대로 있다 (null 프레임 경로와 다른 점).
        presence.reset();
        consumer.reset();
        pool.clear();
        useFocusStore.getState().clear();
      }
      const events = consumer.consume(frame);
      context.events.replace(events);
      const born = new Set(events.filter((e) => e.kind === 'group-born').map((e) => e.key));
      const died = new Set(events.filter((e) => e.kind === 'group-died').map((e) => e.key));
      presence.update(
        frame.groups.map((group) => ({ key: group.key, value: group })),
        born,
        died,
        nowSec,
      );

      const focusedKey = context.focus.key;
      for (const event of events) {
        const spec = burstFor(event, presence.get(event.key)?.value, focusedKey, frame.seq);
        if (spec !== null) {
          pool.add(spec, nowSec);
        }
      }
    }

    const entries = presence.entries();
    layout.step(layoutNodesFrom(entries.map((entry) => entry.value)), cache.dtSec);

    // 초점 그룹이 떠나기 시작하면 초점을 푼다 (M5 스펙 9.1).
    const focus = useFocusStore.getState();
    if (focus.focusedKey !== null) {
      const phase = presence.get(focus.focusedKey)?.phase;
      if (phase === undefined || phase === 'fading-out' || phase === 'collapsing') {
        focus.clear();
      }
    }

    // 노드 목록은 항목이 추가·삭제됐을 때만 다시 만든다 (M6 스펙 10절).
    if (presence.version !== shownVersion.current) {
      shownVersion.current = presence.version;
      setNodeIds(nodeIdsOf(entries));
    }
  }, FRAME_PRIORITY.scene);

  const keys = nodeIds.map((id) => parseNodeId(id).key);
  // 호버 중이던 천체가 사라지면 R3F 가 onPointerOut 없이 오브젝트를 지운다.
  // 지금도 있는 그룹일 때만 그룹 툴팁을 그린다. 위성·코어 툴팁은 대상이 없으면
  // 스스로 숨는다.
  const liveHovered =
    hovered !== null && (hovered.kind !== 'group' || keys.includes(hovered.key)) ? hovered : null;

  // 호버는 React 상태다. flow 강조가 프레임 루프에서 읽을 수 있게, 아직 마운트된
  // (살아 있는) 호버만 비춘다.
  useEffect(() => {
    context.hover.set(liveHovered);
  }, [context, liveHovered]);

  return (
    <SceneContext.Provider value={context}>
      <CameraRig />
      {nodeIds.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      <Satellites />
      <CoreRing />
      <CoreSparks />
      <AmbientDust />
      <FlowStreams />
      <PostEffects />
      <Particles pool={pool} />
      {liveHovered !== null && <Tooltip target={liveHovered} />}
    </SceneContext.Provider>
  );
}
```

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  263 passed (263)` (이 Task 는 테스트를 더하지 않는다), `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐
- build 성공, `index-*.js` 약 1.47 MB

- [ ] **Step 8: 브라우저 확인은 하지 않는다**

`pulse-engine` 도 dev 서버도 띄우지 않는다. 컨트롤러가 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 9: 커밋**

```
git add package.json package-lock.json src/scene/framePriority.ts src/scene/PostEffects.tsx src/scene/coreShader.ts src/scene/CoreOrb.tsx src/scene/SceneRoot.tsx
git commit -m "feat(web): render through bloom, focus-only depth of field and one ACES tone mapping pass"
```

---

## M8 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`(263), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `src/visual/` 이 `react`·`three`·`postprocessing`·`@react-three/*`·`zustand`·`gsap` 을 import 하지 않는다.
- [ ] 브라우저 (dev 서버 + 엔진):
  - 대기 중에는 천체·Orb 가 번지지 않는다(기존 후광만). `node -e "const e=Date.now()+60000;while(Date.now()<e){}"` 4개로 부하를 걸면 뜨거운 코어, 바쁜 천체, 그 사이의 flow 가 번진다.
  - Orb 색이 M6 와 같은 계열(파랑 → 보라 → 자홍 → 주황 → 흰색)이다.
  - 천체를 클릭하면 배경이 흐려지고 초점 천체·위성은 선명하다. Esc 로 다시 선명해진다.
  - Focus A→B 전환에서 flow 선의 dim 이 천체와 함께 바뀐다.
  - 1920×1080 에서 프레임 시간 중앙값 8 ms 이하 (시제품 4.5 ms).
  - 콘솔 오류 없음 (R3F `THREE.Clock` 경고는 예상된 것).
- [ ] `npm run build` 후 `pulse-engine --serve --web-root ..\web\dist` 에서도 같다.

## 다음 단계

M9 가 인스턴싱, 오브젝트 풀, 프레임당 할당 제거(sparkPosition·coreColor·FlowStreams), 버퍼 부분 업로드, 카메라 가까이의 먼지 크기 상한, 성능 측정을 다룬다.
