# M9 — 최적화 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 측정으로 찾은 병목(고해상도 GPU 픽셀 채우기)을 줄이고(dpr 2 에서 8.6~9.9 ms → 2.8 ms), 느린 GPU 를 위한 자동 해상도와 `P` 키 성능 표시를 더한다.

**Architecture:** 계산(`dprFor`, `FrameStats`, `formatPerf`)은 `web/src/visual/perf.ts` 의 순수 모듈이다. 심도 패스는 Focus 중(과 해제 뒤 1초)에만 composer 에 들어가고, MSAA 는 4, 캔버스 안티에일리어싱은 끈다. drei `PerformanceMonitor` 가 dpr 을 조정하고, `PerfMeter` 가 React 상태 없이 DOM 글자를 바꾼다. 단축키 판정은 `shell/shortcut.ts` 로 모은다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / drei 10.7 / postprocessing 6.39.5 / @react-three/postprocessing 3.1.3 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-29-m9-optimization-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`.
- 작업 브랜치는 `feature/m9-optimization` 다. `engine/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `postprocessing`, `@react-three/*`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다.
- 프레임 값은 React 상태에 넣지 않는다. useFrame 우선순위는 전부 음수다.
- oxlint 에 새 경고가 생기면 안 된다 (특히 `react(immutability)`).
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 테스트·타입체크·린트·빌드가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
명령은 C:\dev\pulse-uni\web 에서 실행한다.
```

기준선(작업 전 `main` + 스펙 커밋): 웹 263 tests 통과.

## File Structure

```
web/src/visual/perf.ts           MAX_DPR, DPR_STEP, dprFor, FrameSummary, FrameStats, RenderCounts, formatPerf
web/src/visual/flowTracker.ts    (전체 교체) update() 의 순회 복사 제거
web/src/scene/FlowStreams.tsx    (전체 교체) 위상 정리를 Set 으로
web/src/shell/shortcut.ts        isShortcut (Shell 에서 이동)
web/src/shell/Shell.tsx          (전체 교체) isShortcut 사용
web/src/scene/renderSupport.ts   renderSupport { webgl, halfFloat } (Universe 에서 이동)
web/src/scene/PerfMeter.tsx      성능 표시
web/src/scene/PostEffects.tsx    (전체 교체) 심도 조건부, MSAA 4, 버퍼 타입
web/src/scene/Universe.tsx       (전체 교체) antialias 끔, AdaptiveResolution, P 키, PerfMeter
web/src/scene/framePriority.ts   (전체 교체) meter 우선순위
web/src/shell/shell.css          (전체 교체) .perf-meter
web/tests/visual/perf.test.ts
web/tests/shortcut.test.ts
```

---

## Task 1: 성능 계산

**Files:**
- Create: `web/src/visual/perf.ts`
- Test: `web/tests/visual/perf.test.ts`

**Interfaces:**
- Produces:
  - `MAX_DPR = 2`, `DPR_STEP = 0.25`, `dprFor(factor: number, deviceDpr: number): number`
  - `interface FrameSummary { avgMs: number; fps: number; maxMs: number }`
  - `class FrameStats { add(deltaSec: number): void; get elapsedSec(): number; take(): FrameSummary | null; reset(): void }`
  - `interface RenderCounts { calls: number; triangles: number }`, `formatPerf(summary: FrameSummary | null, dpr: number, counts: RenderCounts): string`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/perf.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { FrameStats, MAX_DPR, dprFor, formatPerf } from '../../src/visual/perf';

describe('dprFor', () => {
  it('maps factor 0 to 1 and factor 1 to the device dpr', () => {
    expect(dprFor(0, 2)).toBe(1);
    expect(dprFor(1, 2)).toBe(2);
    expect(dprFor(1, 1.5)).toBe(1.5);
  });

  it('caps the device dpr at MAX_DPR and never goes below 1', () => {
    expect(dprFor(1, 3)).toBe(MAX_DPR);
    expect(dprFor(1, 0.5)).toBe(1);
    expect(dprFor(-1, 2)).toBe(1);
    expect(dprFor(2, 2)).toBe(2);
  });

  it('rounds down to quarter steps', () => {
    expect(dprFor(0.5, 2)).toBe(1.5);
    expect(dprFor(0.4, 2)).toBe(1.25);
    expect(dprFor(0.2, 2)).toBe(1);
    expect(dprFor(0.9, 1.5)).toBe(1.25);
  });

  it('treats NaN as the lowest resolution', () => {
    expect(dprFor(Number.NaN, 2)).toBe(1);
    expect(dprFor(1, Number.NaN)).toBe(1);
  });
});

describe('FrameStats', () => {
  it('summarises the average, fps and longest frame, then empties the window', () => {
    const stats = new FrameStats();
    stats.add(0.01);
    stats.add(0.02);
    stats.add(0.03);
    expect(stats.elapsedSec).toBeCloseTo(0.06, 9);
    const summary = stats.take();
    expect(summary).not.toBeNull();
    expect(summary!.avgMs).toBeCloseTo(20, 9);
    expect(summary!.fps).toBeCloseTo(50, 9);
    expect(summary!.maxMs).toBeCloseTo(30, 9);
    expect(stats.elapsedSec).toBe(0);
    expect(stats.take()).toBeNull();
  });

  it('ignores negative and non-finite deltas', () => {
    const stats = new FrameStats();
    stats.add(-1);
    stats.add(Number.NaN);
    stats.add(Number.POSITIVE_INFINITY);
    expect(stats.take()).toBeNull();
    stats.add(0.016);
    expect(stats.take()!.maxMs).toBeCloseTo(16, 9);
  });
});

describe('formatPerf', () => {
  it('prints dpr rounded to two decimals without trailing zeros', () => {
    expect(formatPerf(null, 1.100000023841858, { calls: 1, triangles: 1 })).toContain('dpr 1.1 ·');
  });

  it('shows timing, dpr and per-frame render counts on two lines', () => {
    const text = formatPerf({ avgMs: 3.456, fps: 289.4, maxMs: 7.04 }, 1.5, {
      calls: 152,
      triangles: 230252,
    });
    expect(text).toBe('3.5 ms · 289 fps · max 7.0 ms\ndpr 1.5 · 152 calls · 230k tris');
  });

  it('shows dashes before the first summary and small triangle counts as they are', () => {
    expect(formatPerf(null, 1, { calls: 3, triangles: 12 })).toBe(
      '— ms · — fps\ndpr 1 · 3 calls · 12 tris',
    );
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/perf.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/perf"`.

- [ ] **Step 3: `web/src/visual/perf.ts`**

```ts
// M9 스펙 5·6절. 자동 해상도와 성능 표시의 계산.

// 기기 dpr 이 더 커도 2 를 넘기지 않는다. 그 위는 픽셀 비용만 늘고 차이가 보이지 않는다.
export const MAX_DPR = 2;
export const DPR_STEP = 0.25;

// PerformanceMonitor 의 factor(0~1)를 dpr 로 바꾼다. 0 이면 1, 1 이면 기기 dpr(최대 2).
// 0.25 단위로 내려 자잘한 크기 변경(버퍼 재할당)을 줄인다.
export function dprFor(factor: number, deviceDpr: number): number {
  const maxDpr = Number.isFinite(deviceDpr) ? Math.min(MAX_DPR, Math.max(1, deviceDpr)) : 1;
  const f = Number.isFinite(factor) ? Math.min(1, Math.max(0, factor)) : 0;
  const raw = 1 + (maxDpr - 1) * f;
  return Math.max(1, Math.floor(raw / DPR_STEP + 1e-9) * DPR_STEP);
}

export interface FrameSummary {
  // 창 안의 평균 프레임 시간 (ms)
  avgMs: number;
  // 평균에서 구한 fps
  fps: number;
  // 창 안의 가장 긴 프레임 (ms)
  maxMs: number;
}

// 최근 프레임 시간을 모아 요약한다. 성능 표시가 초당 두 번 읽는다.
export class FrameStats {
  private totalSec = 0;
  private maxSec = 0;
  private count = 0;

  add(deltaSec: number): void {
    if (!Number.isFinite(deltaSec) || deltaSec < 0) {
      return;
    }
    this.totalSec += deltaSec;
    this.maxSec = Math.max(this.maxSec, deltaSec);
    this.count += 1;
  }

  // 모은 시간의 합(초). 표시 주기를 정하는 데 쓴다.
  get elapsedSec(): number {
    return this.totalSec;
  }

  // 요약을 돌려주고 창을 비운다. 모은 프레임이 없으면 null.
  take(): FrameSummary | null {
    if (this.count === 0 || this.totalSec <= 0) {
      this.reset();
      return null;
    }
    const avgSec = this.totalSec / this.count;
    const summary = { avgMs: avgSec * 1000, fps: 1 / avgSec, maxMs: this.maxSec * 1000 };
    this.reset();
    return summary;
  }

  reset(): void {
    this.totalSec = 0;
    this.maxSec = 0;
    this.count = 0;
  }
}

export interface RenderCounts {
  calls: number;
  triangles: number;
}

// 성능 표시의 두 줄. 프레임 요약이 없으면(아직 모으는 중) 첫 줄은 대시로 둔다.
export function formatPerf(summary: FrameSummary | null, dpr: number, counts: RenderCounts): string {
  const timing =
    summary === null
      ? '— ms · — fps'
      : `${summary.avgMs.toFixed(1)} ms · ${Math.round(summary.fps)} fps · max ${summary.maxMs.toFixed(1)} ms`;
  const triangles =
    counts.triangles >= 1000 ? `${Math.round(counts.triangles / 1000)}k` : `${counts.triangles}`;
  return `${timing}
dpr ${Number(dpr.toFixed(2))} · ${counts.calls} calls · ${triangles} tris`;
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/perf.test.ts`
Expected: PASS, `Tests  9 passed (9)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  272 passed (272)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 6: 커밋**

```
git add src/visual/perf.ts tests/visual/perf.test.ts
git commit -m "feat(web): add adaptive dpr mapping, frame statistics and the perf readout format"
```

---

## Task 2: flow 정리

**Files:**
- Modify (전체 교체): `web/src/visual/flowTracker.ts`, `web/src/scene/FlowStreams.tsx`

**Interfaces:**
- 공개 API 는 바뀌지 않는다. `FlowTracker.update()` 는 Map 을 복사하지 않고 순회하고(순회 중 삭제는 JS Map 에서 안전하다), `FlowStreams` 는 사라진 선의 위상을 이번 프레임 edge key 의 `Set` 으로 정리한다 (O(P·E) → O(P + E)).

이 Task 는 새 테스트를 더하지 않는다. 동작이 같다는 증거는 기존 `tests/visual/flowTracker.test.ts` 10개다.

- [ ] **Step 1: 기존 테스트 통과 확인 (변경 전)**

Run: `npx vitest run --project node tests/visual/flowTracker.test.ts`
Expected: PASS, `Tests  10 passed (10)`.

- [ ] **Step 2: `web/src/visual/flowTracker.ts` 전체 교체**

```ts
import type { Flow } from '../protocol/schema';

// M7 스펙 4절. flow 선마다 밝기(0~1)를 관리한다. 있는 선은 짧게 밝아지고, 사라진
// 선은 잔광으로 천천히 흐려진다 — 엔진의 추정 flow 는 뜨거운 코어 순위가 초마다
// 바뀌어 선이 매초 1/3~1/2 씩 교체된다(스펙 2절). 흐려지는 동안의 값은 마지막으로
// 본 옛 값이며, 밝기가 그것을 드러낸다.

export const FADE_IN_SEC = 0.4;
export const AFTERGLOW_SEC = 2.5;
export const MAX_EDGES = 96;

export interface FlowEdge {
  key: string;
  group: string;
  coreId: number;
  weight: number;
  source: Flow['source'];
  // 0~1.
  intensity: number;
  // 이번 스냅샷의 flows 에 있는가.
  present: boolean;
}

export function edgeKey(group: string, coreId: number): string {
  return `${group}>${coreId}`;
}

// 그리는 세기. 흐려지는 선은 약해진다.
export function edgeStrength(edge: Pick<FlowEdge, 'weight' | 'intensity'>): number {
  return edge.weight * edge.intensity;
}

export class FlowTracker {
  private readonly items = new Map<string, FlowEdge>();

  // flows: 이번 프레임의 스냅샷 flows. liveGroups: 존재 추적기에 있는 그룹 key.
  // dtSec: 프레임 간격(자르지 않는다 — 탭 복귀 뒤에는 잔광이 끝나 있는 것이 맞다).
  update(flows: readonly Flow[], liveGroups: ReadonlySet<string>, dtSec: number): void {
    const dt = Number.isFinite(dtSec) ? Math.max(0, dtSec) : 0;

    for (const edge of this.items.values()) {
      edge.present = false;
    }
    for (const flow of flows) {
      const key = edgeKey(flow.group, flow.core);
      const existing = this.items.get(key);
      if (existing === undefined) {
        this.items.set(key, {
          key,
          group: flow.group,
          coreId: flow.core,
          weight: flow.weight,
          source: flow.source,
          intensity: 0,
          present: true,
        });
      } else {
        existing.weight = flow.weight;
        existing.source = flow.source;
        existing.present = true;
      }
    }

    // Map 은 순회 중 삭제해도 안전하다. 복사하지 않고 그대로 돈다.
    for (const edge of this.items.values()) {
      // 그 천체가 더 이상 그려지지 않는다. 선도 즉시 지운다.
      if (!liveGroups.has(edge.group)) {
        this.items.delete(edge.key);
        continue;
      }
      if (edge.present) {
        edge.intensity = Math.min(1, edge.intensity + dt / FADE_IN_SEC);
      } else {
        edge.intensity = Math.max(0, edge.intensity - dt / AFTERGLOW_SEC);
        if (edge.intensity <= 0) {
          this.items.delete(edge.key);
        }
      }
    }

    // 넘치면 잔광 중인 선부터, 그 안에서 가장 약한 선부터 버린다.
    // (막 생긴 선은 세기가 작아 곧바로 버려지는 일을 막는다.)
    if (this.items.size > MAX_EDGES) {
      const weakest = [...this.items.values()]
        .sort((a, b) => Number(a.present) - Number(b.present) || edgeStrength(a) - edgeStrength(b))
        .slice(0, this.items.size - MAX_EDGES);
      for (const edge of weakest) {
        this.items.delete(edge.key);
      }
    }
  }

  edges(): FlowEdge[] {
    return [...this.items.values()];
  }

  get size(): number {
    return this.items.size;
  }

  reset(): void {
    this.items.clear();
  }
}
```

- [ ] **Step 3: `web/src/scene/FlowStreams.tsx` 전체 교체**

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
  // 이번 프레임에 남아 있는 선의 key. 위상 정리에 쓴다 — 프레임마다 비우고 다시 채운다.
  const edgeKeys = useRef(new Set<string>());

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
    edgeKeys.current.clear();
    for (const edge of edges) {
      edgeKeys.current.add(edge.key);
    }
    for (const key of phases.current.keys()) {
      if (!edgeKeys.current.has(key)) {
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

- [ ] **Step 4: 통과 확인**

Run: `npx vitest run --project node tests/visual/flowTracker.test.ts`
Expected: PASS, `Tests  10 passed (10)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  272 passed (272)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 6: 커밋**

```
git add src/visual/flowTracker.ts src/scene/FlowStreams.tsx
git commit -m "perf(web): iterate the flow tracker without copying and prune flow phases with a key set"
```

---

## Task 3: 단축키 판정 공용화

**Files:**
- Create: `web/src/shell/shortcut.ts`
- Modify (전체 교체): `web/src/shell/Shell.tsx`
- Test: `web/tests/shortcut.test.ts`

**Interfaces:**
- Produces: `isShortcut(event: KeyboardEvent, letter: string): boolean` — 물리 키(`Key` + 대문자) 또는 글자(소문자·대문자)가 맞으면 true. Ctrl·Meta·Alt, 입력창(INPUT·TEXTAREA·SELECT·contentEditable), 조합 중, 자동 반복이면 false. Shell 의 `D` 동작은 그대로다 (기존 `tests/shell.test.tsx` 가 증거).

- [ ] **Step 1: 실패하는 테스트 — `web/tests/shortcut.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { isShortcut } from '../src/shell/shortcut';

function key(init: KeyboardEventInit, target?: EventTarget): KeyboardEvent {
  const event = new KeyboardEvent('keydown', init);
  if (target !== undefined) {
    Object.defineProperty(event, 'target', { value: target });
  }
  return event;
}

describe('isShortcut', () => {
  it('matches the physical key and the letter in either case', () => {
    expect(isShortcut(key({ code: 'KeyP', key: 'p' }), 'p')).toBe(true);
    expect(isShortcut(key({ code: 'KeyP', key: 'P' }), 'p')).toBe(true);
    // 한글 IME: 글자는 'ㅔ' 지만 물리 키는 P 다.
    expect(isShortcut(key({ code: 'KeyP', key: 'ㅔ' }), 'p')).toBe(true);
    // AZERTY·Dvorak: 물리 키는 다르지만 글자가 p 다.
    expect(isShortcut(key({ code: 'KeyR', key: 'p' }), 'p')).toBe(true);
    expect(isShortcut(key({ code: 'KeyD', key: 'd' }), 'p')).toBe(false);
  });

  it('ignores modifiers, repeats, composition and typing in a field', () => {
    expect(isShortcut(key({ code: 'KeyP', key: 'p', ctrlKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', altKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', metaKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', repeat: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', isComposing: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p' }, document.createElement('input')), 'p')).toBe(
      false,
    );
    const p = { code: 'KeyP', key: 'p' };
    expect(isShortcut(key(p, document.createElement('textarea')), 'p')).toBe(false);
    expect(isShortcut(key(p, document.createElement('select')), 'p')).toBe(false);
    const editable = document.createElement('div');
    Object.defineProperty(editable, 'isContentEditable', { value: true });
    expect(isShortcut(key(p, editable), 'p')).toBe(false);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project jsdom tests/shortcut.test.ts`
Expected: FAIL — `Failed to resolve import "../src/shell/shortcut"`.

- [ ] **Step 3: `web/src/shell/shortcut.ts`**

```ts
// 한 글자 단축키 판정. Shell(D: 뷰 전환)과 Universe(P: 성능 표시)가 함께 쓴다.

// 입력 중인 글자를 단축키로 가로채지 않는다.
function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))
  );
}

// 한글 IME 가 켜져 있으면 event.key 는 'ㅇ' 이나 조합 중 'Process' 로
// 읽혀 물리 키를 알 수 없다. 그래서 물리 키(event.code)로 맞춘다.
// 반대로 AZERTY·Dvorak 에서는 글자가 다른 물리 키에 있으므로 글자
// (event.key)도 받는다. 조합 중(isComposing)이거나 키를 누르고 있어
// 자동 반복(repeat)되는 입력, 수정 키와 함께 누른 입력은 무시한다.
export function isShortcut(event: KeyboardEvent, letter: string): boolean {
  if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
    return false;
  }
  if (event.isComposing || event.repeat) {
    return false;
  }
  const lower = letter.toLowerCase();
  return (
    event.code === `Key${lower.toUpperCase()}` ||
    event.key === lower ||
    event.key === lower.toUpperCase()
  );
}
```

- [ ] **Step 4: `web/src/shell/Shell.tsx` 전체 교체**

```tsx
import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { Universe } from '../scene/Universe';
import { SceneErrorBoundary } from './SceneErrorBoundary';
import { isShortcut } from './shortcut';
import { UniverseBadge } from './UniverseBadge';
import { toggledHash, viewFromHash } from './view';
import './shell.css';

function subscribeToHash(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

function currentHash(): string {
  return window.location.hash;
}

// 우주와 대시보드 중 하나만 마운트한다. 숨긴 쪽을 남겨 두면 대시보드의
// 100 ms 타이머와 WebGL 렌더 루프가 함께 돈다.
// Shell 은 스냅샷마다 바뀌는 값(status, seq)을 구독하지 않는다 — 구독하면
// <Universe/> 까지 매초 재렌더된다(스펙 4절 규칙 3). 배지는 UniverseBadge 가
// 따로 구독한다.
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      // 자동 반복을 무시하지 않으면 키를 누르고 있는 동안 초당 30번씩 뷰가
      // 뒤집히며 WebGL 컨텍스트를 매번 새로 만든다 (isShortcut 참조).
      if (isShortcut(event, 'd')) {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [toggle]);

  return (
    <>
      {view === 'dashboard' ? (
        <App />
      ) : (
        <>
          <SceneErrorBoundary>
            <Universe />
          </SceneErrorBoundary>
          <UniverseBadge />
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
```

- [ ] **Step 5: 통과 확인 (GREEN)**

Run: `npx vitest run --project jsdom tests/shortcut.test.ts tests/shell.test.tsx`
Expected: PASS, 두 파일 모두 통과 (`shortcut.test.ts` 2개).

- [ ] **Step 6: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  274 passed (274)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 7: 커밋**

```
git add src/shell/shortcut.ts src/shell/Shell.tsx tests/shortcut.test.ts
git commit -m "refactor(web): share single-letter shortcut matching between the view toggle and the perf meter"
```

---

## Task 4: 장면 — 픽셀 비용, 자동 해상도, 성능 표시

**Files:**
- Create: `web/src/scene/renderSupport.ts`, `web/src/scene/PerfMeter.tsx`
- Modify (전체 교체): `web/src/scene/framePriority.ts`, `web/src/scene/PostEffects.tsx`, `web/src/scene/Universe.tsx`, `web/src/shell/shell.css`

**Interfaces:**
- Consumes: Task 1 의 `dprFor`, `FrameStats`, `formatPerf`, `FrameSummary`. Task 3 의 `isShortcut`. 기존 `FOCUS_TRANSITION_SEC` (`scene/CameraRig.tsx`), `useFocusStore` (`scene/focusStore.ts`).
- Produces:
  - `renderSupport: { webgl: boolean; halfFloat: boolean }` (`scene/renderSupport.ts`) — Universe 의 기존 `probeWebgl()` 를 대신한다.
  - `PerfMeter()` — Universe 가 `P` 로 켜고 끈다. `FRAME_PRIORITY.meter = -2`.

이 Task 는 R3F 장면 코드라 jsdom 에서 자동 테스트하지 않는다(계약서 10절). jsdom 에는 WebGL 이 없어 Universe 는 안내문만 그린다. 컨트롤러가 실제 엔진에 붙여 확인한다.

R3F·postprocessing 사항 (코드가 이미 반영하고 있다):
- `DepthOfField` 가 빠지면 `@react-three/postprocessing` 이 EffectPass 를 다시 만든다. 초점을 잡을 때 셰이더 컴파일로 한 번 멈칫한다 (시제품 83 ms 간격).
- 성능 표시는 `gl.info.autoReset` 을 끄고 프레임 시작에 앞 프레임 합계를 읽는다. 언마운트 때 되돌린다.
- Canvas 의 `dpr` 은 Universe 의 React 상태로 들고 숫자로 넘긴다 (R3F 9.8 은 Canvas 가 렌더될 때마다 dpr 을 prop 값으로 되돌리므로 모듈 상수로는 막을 수 없다. 상태는 최대 약 2.5초에 한 번 바뀐다). `gl` 옵션은 모듈 상수다.

- [ ] **Step 1: `web/src/scene/renderSupport.ts`**

```ts
// 이 브라우저가 무엇을 그릴 수 있는지 모듈 로드 시 한 번만 판정한다. Universe 가 여러 번
// 마운트되어도(뷰 토글) 탐지용 컨텍스트를 반복해서 만들지 않는다.

interface RenderSupport {
  webgl: boolean;
  // 반정밀(half float) 렌더 타깃에 그릴 수 있는가 (M9 스펙 7절). 없으면 후처리는 8비트
  // 버퍼로 그린다.
  halfFloat: boolean;
}

function probe(): RenderSupport {
  try {
    const canvas = document.createElement('canvas');
    const gl2 = canvas.getContext('webgl2');
    // three 0.186 은 WebGL2 가 필요하다. WebGL1 만 되는 브라우저는 지원하지 않는 것으로 본다.
    if (gl2 === null) {
      return { webgl: false, halfFloat: false };
    }
    const halfFloat =
      gl2.getExtension('EXT_color_buffer_float') !== null ||
      gl2.getExtension('EXT_color_buffer_half_float') !== null;
    // 탐지용 컨텍스트를 그대로 두면 Universe 가 마운트될 때마다 하나씩
    // 새어 나간다. 판정이 끝나면 바로 반납한다.
    gl2.getExtension('WEBGL_lose_context')?.loseContext();
    return { webgl: true, halfFloat };
  } catch {
    return { webgl: false, halfFloat: false };
  }
}

export const renderSupport: RenderSupport = probe();
```

- [ ] **Step 2: `web/src/scene/framePriority.ts` 전체 교체**

```ts
// useFrame 우선순위. 작은 값이 먼저 돈다. 양수는 R3F 의 자동 렌더를 끄므로
// 전부 음수이고, 노드·위성 메시와 drei Html 은 기본값 0 에서 돈다.
//
//   meter       성능 표시: 앞 프레임의 렌더 합계를 읽고 비운다 (가장 먼저)
//   scene       보간 → 존재 추적 → lifecycle → 레이아웃 → 버스트 생성
//   camera      Focus 전환 진행도로 카메라 자세 (노드 위치가 정해진 뒤)
//   satellites  위성 위치 (Focus 진행도가 정해진 뒤)
//   tooltip     툴팁 anchor (위성 위치가 정해진 뒤, Html 이 투영하기 전)
//   particles   입자 버퍼 (모든 기준점이 정해진 뒤)
//   postfx      심도 초점·세기 (카메라 target 과 Focus 강도가 정해진 뒤). 렌더는
//               EffectComposer 가 양수 우선순위에서 맡는다.
export const FRAME_PRIORITY = {
  meter: -2,
  scene: -1,
  camera: -0.8,
  satellites: -0.6,
  tooltip: -0.4,
  particles: -0.2,
  postfx: -0.1,
} as const;
```

- [ ] **Step 3: `web/src/scene/PerfMeter.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import type { WebGLRenderer } from 'three';

import { FrameStats, formatPerf, type FrameSummary } from '../visual/perf';
import { FRAME_PRIORITY } from './framePriority';

// 표시를 이 간격(초)마다 갱신한다. 매 프레임 바꾸면 숫자를 읽을 수 없다.
const REFRESH_SEC = 0.5;

// M9 스펙 6절. 성능 표시. 켜져 있을 때만 마운트된다 (P 키, Universe).
// 값은 React 상태를 거치지 않고 body 에 붙인 요소의 글자를 직접 바꾼다.
// draw call·삼각형은 composer 가 한 프레임에 여러 번 렌더하므로, 자동 초기화를 끄고
// 프레임 시작에 앞 프레임의 합계를 읽은 뒤 비운다.
export function PerfMeter() {
  const stats = useMemo(() => new FrameStats(), []);
  const element = useRef<HTMLDivElement | null>(null);
  const renderer = useRef<WebGLRenderer | null>(null);
  const last = useRef<FrameSummary | null>(null);

  useEffect(() => {
    const div = document.createElement('div');
    div.className = 'perf-meter';
    div.textContent = formatPerf(null, 1, { calls: 0, triangles: 0 });
    document.body.appendChild(div);
    element.current = div;
    return () => {
      div.remove();
      element.current = null;
      // 표시를 끄면 three 의 기본 동작(렌더마다 초기화)으로 되돌린다.
      const gl = renderer.current;
      if (gl !== null) {
        gl.info.autoReset = true;
        gl.info.reset();
      }
    };
  }, []);

  useFrame((state, delta) => {
    const gl = state.gl;
    renderer.current = gl;
    const info = gl.info;
    if (info.autoReset) {
      // 첫 프레임: 이번에는 합계가 없다. 다음 프레임부터 센다.
      info.autoReset = false;
      info.reset();
      return;
    }
    const counts = { calls: info.render.calls, triangles: info.render.triangles };
    info.reset();
    stats.add(delta);
    if (stats.elapsedSec < REFRESH_SEC) {
      return;
    }
    last.current = stats.take() ?? last.current;
    if (element.current !== null) {
      element.current.textContent = formatPerf(last.current, gl.getPixelRatio(), counts);
    }
  }, FRAME_PRIORITY.meter);

  return null;
}
```

- [ ] **Step 4: `web/src/scene/PostEffects.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { Bloom, DepthOfField, EffectComposer, ToneMapping } from '@react-three/postprocessing';
import type { DepthOfFieldEffect } from 'postprocessing';
import { ToneMappingMode } from 'postprocessing';
import { useEffect, useRef, useState } from 'react';
import { HalfFloatType, UnsignedByteType, type Vector3 } from 'three';

import {
  BLOOM_INTENSITY,
  BLOOM_RADIUS,
  BLOOM_SMOOTHING,
  BLOOM_THRESHOLD,
  FOCUS_RANGE,
  bokehFor,
} from '../visual/postfx';
import { FOCUS_TRANSITION_SEC } from './CameraRig';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { renderSupport } from './renderSupport';
import { useSceneContext } from './sceneContext';

// M9 스펙 4절. MSAA 샘플 수. 8 에서 4 로 줄여도 가장자리 차이는 작고, dpr 2 에서 약 1 ms 를 던다.
const MULTISAMPLING = 4;
// 반정밀 렌더 타깃을 못 쓰면 8비트로 그린다 (M9 스펙 7절). Bloom 이 덜 밝아진다.
const FRAME_BUFFER_TYPE = renderSupport.halfFloat ? HalfFloatType : UnsignedByteType;

// drei OrbitControls(makeDefault)가 등록하는 controls 에서 쓰는 부분만.
interface Controls {
  target: Vector3;
}

// M8 스펙 4절. 장면을 선형 HDR 버퍼에 그린 뒤 Bloom → ACES 를 거친다. 심도는 초점이
// 잡혀 있는 동안과 풀린 뒤 전환이 끝나기 전에만 composer 에 들어간다 (M9 스펙 4절).
// 심도가 있으면 래퍼가 세 효과를 하나의 EffectPass 로 합친다. Bloom 은 심도를 거치지
// 않은 선명한 장면 입력을 읽고, 그 결과가 심도 결과 뒤에 더해진다. 심도가 없으면
// 패스는 Bloom + 톤 매핑이다.
// 톤 매핑은 여기 한 곳에서만 한다. composer 의 장면은 렌더 타깃에 그려지고, 렌더 타깃에
// 그릴 때 three 는 재질에 톤 매핑을 적용하지 않는다. 또 @react-three/postprocessing 은
// 마운트되어 있는 동안 gl.toneMapping 을 NoToneMapping 으로 강제한다. 따라서 ToneMapping
// 효과가 유일한 톤 매핑이며, composer 밖에서 그리는 것은 톤 매핑을 받지 못한다.
export function PostEffects() {
  const { focus } = useSceneContext();
  const depthOfField = useRef<DepthOfFieldEffect>(null);
  const depthActive = useDepthOfFieldActive();

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
    <EffectComposer frameBufferType={FRAME_BUFFER_TYPE} multisampling={MULTISAMPLING}>
      {depthActive ? (
        <DepthOfField ref={depthOfField} focusRange={FOCUS_RANGE} bokehScale={0} />
      ) : (
        <></>
      )}
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

// 심도 패스는 흐림 세기가 0 이어도 모든 패스를 돈다 (M9 스펙 2.2절, dpr 2 에서 약 3.3 ms).
// 초점이 잡혀 있거나, 풀린 뒤 카메라 전환이 끝나기 전(흐림이 걷히는 중)에만 composer 에 넣는다.
function useDepthOfFieldActive(): boolean {
  const focused = useFocusStore((state) => state.focusedKey !== null);
  // 초점이 풀릴 때마다 1 씩 늘어나는 번호. 전환이 끝나면 null. 번호가 바뀌면 타이머를 새로
  // 건다 — 풀고 곧바로 다시 잡았다 푸는 경우에도 마지막 해제부터 전환 시간을 잰다.
  const [release, setRelease] = useState<number | null>(null);
  const [wasFocused, setWasFocused] = useState(focused);
  // 초점이 풀리는 순간을 렌더 중에 잡는다 (이전 값과 비교하는 React 권장 방식).
  if (focused !== wasFocused) {
    setWasFocused(focused);
    if (!focused) {
      setRelease((count) => (count ?? 0) + 1);
    }
  }
  useEffect(() => {
    if (release === null) {
      return;
    }
    const timer = window.setTimeout(() => setRelease(null), FOCUS_TRANSITION_SEC * 1000);
    return () => window.clearTimeout(timer);
  }, [release]);
  return focused || release !== null;
}
```

- [ ] **Step 5: `web/src/scene/Universe.tsx` 전체 교체**

```tsx
import { OrbitControls, PerformanceMonitor, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useEffect, useState } from 'react';

import { isShortcut } from '../shell/shortcut';
import { useSnapshotStore } from '../state/snapshotStore';
import { OVERVIEW_POSE } from '../visual/camera';
import { dprFor } from '../visual/perf';
import { FocusPanel } from './FocusPanel';
import { PerfMeter } from './PerfMeter';
import { useFocusStore } from './focusStore';
import { renderSupport } from './renderSupport';
import { SceneRoot } from './SceneRoot';

// 화면에 직접 그리는 것은 후처리의 전체화면 사각형 하나뿐이다. 캔버스 자체
// 안티에일리어싱은 효과가 없고 비용만 든다 (M9 스펙 2.2절). MSAA 는 composer 가 한다.
const GL_OPTIONS = { antialias: false };

// M9 스펙 5절. 실제 fps 를 보고 dpr 을 [1, 기기 dpr(최대 2)] 사이에서 조정한다.
// 느린 GPU 의 안전장치다. 최대 해상도에서 시작한다. 정한 dpr 은 onDpr 로 Universe 의
// 상태에 올린다 (Canvas 에 직접 setDpr 하면 다음 렌더에서 dpr prop 으로 되돌아간다).
function AdaptiveResolution({ onDpr }: { onDpr: (dpr: number) => void }) {
  return (
    <PerformanceMonitor
      factor={1}
      onChange={({ factor }) => onDpr(dprFor(factor, window.devicePixelRatio))}
    />
  );
}

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);
  const focused = useFocusStore((state) => state.focusedKey !== null);
  // 자동 해상도가 정한 dpr. Canvas 는 렌더될 때마다 dpr prop 으로 되돌리므로 (R3F 9.8) 값은 여기서 들고 있다. 바뀌는 것은 PerformanceMonitor 가 factor 를 바꿀 때뿐이라 초당 한 번도 안 된다.
  const [dpr, setDpr] = useState(() => dprFor(1, window.devicePixelRatio));
  // 성능 표시 (M9 스펙 6절). 켜고 끄는 것만 React 상태다.
  const [showPerf, setShowPerf] = useState(false);

  // Esc 로 초점을 푼다 (M5 스펙 9.1). P 로 성능 표시를 켜고 끈다 (M9 스펙 6절).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        useFocusStore.getState().clear();
      }
      if (isShortcut(event, 'p')) {
        setShowPerf((shown) => !shown);
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (!renderSupport.webgl) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas
        dpr={dpr}
        gl={GL_OPTIONS}
        camera={{
          fov: 50,
          position: [OVERVIEW_POSE.position.x, OVERVIEW_POSE.position.y, OVERVIEW_POSE.position.z],
        }}
        // 빈 곳을 클릭하면 초점을 푼다. R3F 는 2px 넘게 끌면 이것을 부르지 않으므로
        // 궤도 조작은 초점을 풀지 않는다.
        onPointerMissed={() => useFocusStore.getState().clear()}
      >
        <color attach="background" args={['#03040a']} />
        <ambientLight intensity={0.2} />
        {/* 중심 점광원은 가운데의 큰 천체 안에 묻힌다. 방향광을 쓴다. */}
        <directionalLight position={[20, 30, 25]} intensity={1.1} />
        <Stars radius={120} depth={60} count={4000} factor={4} fade />
        <SceneRoot />
        <AdaptiveResolution onDpr={setDpr} />
        {showPerf && <PerfMeter />}
        <OrbitControls
          makeDefault
          enableDamping
          // 초점 중에는 자동 회전을 끈다. 카메라가 천체를 따라가는 중이다.
          autoRotate={autoRotate && !focused}
          autoRotateSpeed={0.3}
          onStart={() => setAutoRotate(false)}
        />
      </Canvas>
      <FocusPanel />
      {!hasSnapshot && <p className="universe-message">waiting for the first snapshot…</p>}
    </div>
  );
}
```

- [ ] **Step 6: `web/src/shell/shell.css` 전체 교체**

```css
.universe {
  position: fixed;
  inset: 0;
}

.universe-message {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0;
  color: #8f9aa8;
  pointer-events: none;
}

.universe-message a {
  pointer-events: auto;
  color: #6ee7a8;
  margin-left: 0.5ch;
}

.universe-tooltip {
  padding: 6px 9px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
  white-space: nowrap;
}

.universe-tooltip-name {
  color: #e8edf3;
}

.shell-badge {
  position: fixed;
  top: 12px;
  left: 12px;
  right: 160px;
  /* 배지에는 상호작용 요소가 없다. 캔버스 위 포인터 입력을 가로막지 않는다. */
  pointer-events: none;
}

.shell-toggle {
  position: fixed;
  top: 12px;
  right: 12px;
  padding: 6px 12px;
  font: inherit;
  color: #d7dde5;
  background: #14181d;
  border: 1px solid #2a313a;
  border-radius: 4px;
  cursor: pointer;
}

.shell-toggle:hover {
  border-color: #6ee7a8;
}

.focus-panel {
  position: fixed;
  top: 64px;
  right: 12px;
  width: 340px;
  max-height: calc(100vh - 88px);
  overflow-y: auto;
  padding: 10px 12px;
  background: rgba(20, 24, 29, 0.9);
  border: 1px solid #2a313a;
  border-radius: 4px;
}

.focus-panel header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
  color: #e8edf3;
}

.focus-panel header button {
  font: inherit;
  color: #d7dde5;
  background: none;
  border: none;
  cursor: pointer;
}

.focus-panel dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 2px 12px;
  margin: 0 0 8px;
}

.focus-panel dt {
  color: #8f9aa8;
}

.focus-panel dd {
  margin: 0;
}

.focus-panel-path {
  margin: 0 0 8px;
  color: #8f9aa8;
  word-break: break-all;
}

.focus-panel table {
  width: 100%;
  border-collapse: collapse;
}

.focus-panel th,
.focus-panel td {
  padding: 2px 4px;
  text-align: right;
}

.focus-panel th:first-child,
.focus-panel td:first-child {
  text-align: left;
}

/* M9 성능 표시 (P). 포인터 입력을 가로막지 않는다. */
.perf-meter {
  position: fixed;
  left: 12px;
  bottom: 12px;
  padding: 6px 9px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
  color: #c9d3de;
  font: 12px/1.5 ui-monospace, 'Cascadia Mono', Consolas, monospace;
  white-space: pre;
  pointer-events: none;
  z-index: 10;
}
```

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  274 passed (274)` (이 Task 는 테스트를 더하지 않는다), `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐
- build 성공, `index-*.js` 약 1.47 MB

- [ ] **Step 8: 브라우저 확인은 하지 않는다**

`pulse-engine` 도 dev 서버도 띄우지 않는다. 컨트롤러가 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 9: 커밋**

```
git add src/scene/renderSupport.ts src/scene/framePriority.ts src/scene/PerfMeter.tsx src/scene/PostEffects.tsx src/scene/Universe.tsx src/shell/shell.css
git commit -m "perf(web): run depth of field only while focused, MSAA 4, adaptive dpr and a P-key perf meter"
```

---

## M9 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`(274), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `src/visual/` 이 `react`·`three`·`postprocessing`·`@react-three/*`·`zustand`·`gsap` 을 import 하지 않는다.
- [ ] 브라우저 (dev 서버 + 엔진):
  - 1920×1080 dpr 2 에서 Focus 없는 프레임 시간 중앙값 4 ms 이하 (시제품 2.8 ms, 스펙 2절 방식).
  - `P` 로 성능 표시가 켜지고 꺼진다. draw call 이 프레임 합계(Focus 없음 약 141)로 보인다.
  - 천체를 클릭하면 배경이 흐려지고(심도 패스 들어감, draw call 증가), Esc 뒤 약 1초에 패스가 빠진다.
  - 화면이 M8 과 같게 보인다 (Bloom·Orb 색·flow).
  - 콘솔 오류 없음 (R3F `THREE.Clock` 경고는 예상된 것).
- [ ] `npm run build` 후 `pulse-engine --serve --web-root ..\web\dist` 에서도 같다.

## 다음 단계

계약서 8절의 마지막 단계다. 이후는 계약서가 독립 확장으로 둔 ETW 기반 실측 스레드-코어 매핑(`source: measured`)이다.
