# M7 — Thread Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 엔진의 `flows[]`(그룹 → 코어) 를 프로세스 천체에서 코어 Orb 로 솟았다 내려앉는 빛의 아치와 그 위를 흐르는 입자로 그린다. 선은 사라져도 2.5초 잔광으로 흐려지고, 호버한 그룹·코어의 선만 밝게 강조된다.

**Architecture:** 잔광 추적(`FlowTracker`), 베지어 아치, 입자 위상, 코어 id→index 는 `web/src/visual/` 의 순수 모듈이다(node 환경 테스트). 장면은 `FlowStreams` 컴포넌트 하나가 매 프레임 추적기를 갱신하고 선마다 곡선을 한 번 계산해 `LineSegments` 와 `Points` 버퍼를 함께 채운다. 호버 상태는 `HoverFrame` 으로 프레임 루프에 비춘다. 엔진·계약은 바꾸지 않는다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / zustand 5 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-29-m7-thread-flow-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`.
- 작업 브랜치는 `feature/m7-thread-flow` 다. `engine/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `@react-three/*`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다. `protocol/` 의 타입은 쓸 수 있다.
- 프레임 값은 React 상태에 넣지 않는다. 시간에 따라 도는 값(흐름 위상)은 누적한다. `flows[].core` 는 코어 **id** 다 — index 와 같다고 가정하지 않는다.
- oxlint 에 새 경고가 생기면 안 된다 (특히 `react(immutability)`). context 의 공유 상태는 메서드로만 바꾼다.
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 테스트·타입체크·린트·빌드가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
명령은 C:\dev\pulse-uni\web 에서 실행한다.
```

기준선(작업 전 `main` + 스펙 커밋): 웹 234 tests 통과.

## File Structure

```
web/src/visual/flowTracker.ts    FADE_IN_SEC, AFTERGLOW_SEC, MAX_EDGES, FlowEdge, edgeKey, edgeStrength, FlowTracker
web/src/visual/flowCurve.ts      ARCH, SPREAD, flowControlPoint, curvePoint
web/src/visual/flowParticles.ts  MAX_PARTICLES_PER_EDGE, particleCount, flowSpeed, advanceFlowPhase, particleT
web/src/visual/coreRing.ts       (전체 교체) coreIndexById 추가
web/src/scene/sceneContext.ts    (전체 교체) HoverFrame, context.hover
web/src/scene/FlowStreams.tsx    추적기 갱신 + 선 + 입자
web/src/scene/SceneRoot.tsx      (전체 교체) HoverFrame 생성·비추기, FlowStreams 마운트
web/tests/visual/flowTracker.test.ts
web/tests/visual/flowCurve.test.ts  (flowCurve + flowParticles + coreIndexById)
```

---

## Task 1: 선 추적기 (잔광)

**Files:**
- Create: `web/src/visual/flowTracker.ts`
- Test: `web/tests/visual/flowTracker.test.ts`

**Interfaces:**
- Consumes: `Flow` 타입 (`web/src/protocol/schema.ts`: `{ group: string; core: number; weight: number; source: 'estimated' | 'measured' }`).
- Produces:
  - `FADE_IN_SEC = 0.4`, `AFTERGLOW_SEC = 2.5`, `MAX_EDGES = 96`
  - `interface FlowEdge { key: string; group: string; coreId: number; weight: number; source: Flow['source']; intensity: number; present: boolean }`
  - `edgeKey(group: string, coreId: number): string` (`"group>coreId"`), `edgeStrength(edge: Pick<FlowEdge, 'weight' | 'intensity'>): number`
  - `class FlowTracker { update(flows: readonly Flow[], liveGroups: ReadonlySet<string>, dtSec: number): void; edges(): FlowEdge[]; get size(): number; reset(): void }`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/flowTracker.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import type { Flow } from '../../src/protocol/schema';
import {
  AFTERGLOW_SEC,
  FADE_IN_SEC,
  FlowTracker,
  MAX_EDGES,
  edgeKey,
  edgeStrength,
} from '../../src/visual/flowTracker';

function flow(group: string, core: number, weight = 0.2): Flow {
  return { group, core, weight, source: 'estimated' };
}

const LIVE = new Set(['a.exe:1', 'b.exe:2']);

function edge(tracker: FlowTracker, group: string, core: number) {
  return tracker.edges().find((e) => e.key === edgeKey(group, core));
}

describe('FlowTracker', () => {
  it('brightens a new edge over FADE_IN_SEC', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, 0);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBe(0);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC / 2);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.5, 9);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBe(1);
  });

  it('lets a vanished edge glow on for AFTERGLOW_SEC, keeping its last weight', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3, 0.3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, AFTERGLOW_SEC / 2);

    const fading = edge(tracker, 'a.exe:1', 3)!;
    expect(fading.present).toBe(false);
    expect(fading.intensity).toBeCloseTo(0.5, 9);
    expect(fading.weight).toBe(0.3);
    expect(edgeStrength(fading)).toBeCloseTo(0.15, 9);

    tracker.update([], LIVE, AFTERGLOW_SEC / 2);
    expect(edge(tracker, 'a.exe:1', 3)).toBeUndefined();
  });

  it('brightens a returning edge from the intensity it had faded to', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, AFTERGLOW_SEC * 0.4);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.6, 9);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC * 0.25);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.85, 9);
    expect(edge(tracker, 'a.exe:1', 3)?.present).toBe(true);
  });

  it('updates weight and source of a present edge', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3, 0.1)], LIVE, 0.1);
    tracker.update([{ group: 'a.exe:1', core: 3, weight: 0.4, source: 'measured' }], LIVE, 0.1);
    const e = edge(tracker, 'a.exe:1', 3)!;
    expect(e.weight).toBe(0.4);
    expect(e.source).toBe('measured');
  });

  it('drops edges of groups that are no longer drawn at once', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3), flow('b.exe:2', 4)], LIVE, FADE_IN_SEC);
    tracker.update([flow('a.exe:1', 3)], new Set(['a.exe:1']), 0.01);
    expect(edge(tracker, 'b.exe:2', 4)).toBeUndefined();
    expect(tracker.size).toBe(1);
  });

  it('keeps at most MAX_EDGES, dropping the weakest', () => {
    const tracker = new FlowTracker();
    const groups = new Set<string>();
    const flows: Flow[] = [];
    for (let i = 0; i < MAX_EDGES + 10; i += 1) {
      groups.add(`g${i}.exe:${i}`);
      flows.push(flow(`g${i}.exe:${i}`, 0, 0.01 + i * 0.001));
    }
    tracker.update(flows, groups, FADE_IN_SEC);
    expect(tracker.size).toBe(MAX_EDGES);
    // 가장 약한 10 개(i = 0..9)가 빠졌다.
    expect(edge(tracker, 'g0.exe:0', 0)).toBeUndefined();
    expect(edge(tracker, 'g9.exe:9', 0)).toBeUndefined();
    expect(edge(tracker, 'g10.exe:10', 0)).toBeDefined();
  });

  it('ends the afterglow in one step after a long pause (tab return)', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, 30);
    expect(tracker.size).toBe(0);
  });

  it('treats a NaN or negative dt as no time', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC / 2);
    tracker.update([flow('a.exe:1', 3)], LIVE, Number.NaN);
    tracker.update([flow('a.exe:1', 3)], LIVE, -1);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.5, 9);
  });

  it('forgets everything on reset', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, 0.1);
    tracker.reset();
    expect(tracker.size).toBe(0);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/flowTracker.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/flowTracker"`.

- [ ] **Step 3: `web/src/visual/flowTracker.ts`**

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

    for (const edge of [...this.items.values()]) {
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

    // 넘치면 가장 약한 선부터 버린다.
    if (this.items.size > MAX_EDGES) {
      const weakest = [...this.items.values()]
        .sort((a, b) => edgeStrength(a) - edgeStrength(b))
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

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/flowTracker.test.ts`
Expected: PASS, `Tests  9 passed (9)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  243 passed (243)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 6: 커밋**

```
git add src/visual/flowTracker.ts tests/visual/flowTracker.test.ts
git commit -m "feat(web): track flow edges with a fade-in and a 2.5 s afterglow"
```

---

## Task 2: 아치 곡선, 흐르는 입자, 코어 id → index

**Files:**
- Create: `web/src/visual/flowCurve.ts`, `web/src/visual/flowParticles.ts`
- Modify (전체 교체): `web/src/visual/coreRing.ts` (`coreIndexById` 추가 — 기존 내용은 그대로)
- Test: `web/tests/visual/flowCurve.test.ts`

**Interfaces:**
- Consumes: `hash01` (`visual/hash.ts`), `Vec3` (`visual/layout.ts`).
- Produces:
  - `ARCH = 0.35`, `SPREAD = 0.3`, `flowControlPoint(from: Vec3, to: Vec3, key: string): Vec3`, `curvePoint(from: Vec3, control: Vec3, to: Vec3, t: number, out: Vec3): Vec3` (out 에 쓰고 out 을 돌려준다)
  - `MAX_PARTICLES_PER_EDGE = 12`, `particleCount(strength: number): number`, `flowSpeed(weight: number): number`, `advanceFlowPhase(phase: number, weight: number, dtSec: number): number` ([0,1) 에서 감음), `particleT(phase: number, i: number, n: number): number`
  - `coreIndexById(cores: readonly { id: number }[]): Map<number, number>`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/flowCurve.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { coreIndexById } from '../../src/visual/coreRing';
import { ARCH, SPREAD, curvePoint, flowControlPoint } from '../../src/visual/flowCurve';
import {
  MAX_PARTICLES_PER_EDGE,
  advanceFlowPhase,
  flowSpeed,
  particleCount,
  particleT,
} from '../../src/visual/flowParticles';

const FROM = { x: 0, y: 2, z: 0 };
const TO = { x: 28, y: 0, z: 0 };

function distance(a: { x: number; y: number; z: number }, b: { x: number; y: number; z: number }) {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

describe('flowCurve', () => {
  it('starts at the group and ends at the core', () => {
    const c = flowControlPoint(FROM, TO, 'a.exe:1>3');
    expect(curvePoint(FROM, c, TO, 0, { x: 0, y: 0, z: 0 })).toEqual(FROM);
    expect(curvePoint(FROM, c, TO, 1, { x: 0, y: 0, z: 0 })).toEqual(TO);
  });

  it('lifts the control point ARCH × distance above the midpoint', () => {
    const c = flowControlPoint(FROM, TO, 'a.exe:1>3');
    expect(c.y).toBeCloseTo((FROM.y + TO.y) / 2 + ARCH * distance(FROM, TO), 9);
  });

  it('spreads keys sideways within ±SPREAD/2 × distance, perpendicular to the line', () => {
    const d = distance(FROM, TO);
    for (let i = 0; i < 50; i += 1) {
      const c = flowControlPoint(FROM, TO, `g${i}.exe:${i}>3`);
      // 선이 x 축을 따라가므로 옆 방향은 z 다. x 는 중간점 그대로.
      expect(c.x).toBeCloseTo(14, 9);
      expect(Math.abs(c.z)).toBeLessThanOrEqual((SPREAD / 2) * d + 1e-9);
    }
  });

  it('is deterministic, and differs between keys with the same endpoints', () => {
    expect(flowControlPoint(FROM, TO, 'k1')).toEqual(flowControlPoint(FROM, TO, 'k1'));
    expect(flowControlPoint(FROM, TO, 'k1')).not.toEqual(flowControlPoint(FROM, TO, 'k2'));
  });

  it('writes into the given vector instead of allocating', () => {
    const out = { x: 0, y: 0, z: 0 };
    const c = flowControlPoint(FROM, TO, 'k');
    expect(curvePoint(FROM, c, TO, 0.5, out)).toBe(out);
  });
});

describe('flowParticles', () => {
  it('maps strength to a particle count with a cap', () => {
    expect(particleCount(0)).toBe(0);
    expect(particleCount(-1)).toBe(0);
    expect(particleCount(Number.NaN)).toBe(0);
    expect(particleCount(0.1)).toBe(3);
    expect(particleCount(0.2)).toBe(6);
    expect(particleCount(1)).toBe(MAX_PARTICLES_PER_EDGE);
  });

  it('accumulates the phase and wraps it into [0, 1)', () => {
    expect(advanceFlowPhase(0, 0.2, 1)).toBeCloseTo(flowSpeed(0.2), 9);
    const wrapped = advanceFlowPhase(0.9, 1, 1);
    expect(wrapped).toBeGreaterThanOrEqual(0);
    expect(wrapped).toBeLessThan(1);
    expect(advanceFlowPhase(0.3, 1, -2)).toBe(0.3);
    expect(advanceFlowPhase(0.3, 1, Number.NaN)).toBe(0.3);
  });

  it('spaces particles evenly along the curve', () => {
    expect(particleT(0, 0, 4)).toBe(0);
    expect(particleT(0, 1, 4)).toBe(0.25);
    expect(particleT(0.9, 1, 4)).toBeCloseTo(0.15, 9);
    for (let i = 0; i < 12; i += 1) {
      const t = particleT(0.77, i, 12);
      expect(t).toBeGreaterThanOrEqual(0);
      expect(t).toBeLessThan(1);
    }
  });
});

describe('coreIndexById', () => {
  it('maps non-contiguous ids to array positions', () => {
    const map = coreIndexById([{ id: 0 }, { id: 2 }, { id: 64 }, { id: 65 }]);
    expect(map.get(0)).toBe(0);
    expect(map.get(2)).toBe(1);
    expect(map.get(64)).toBe(2);
    expect(map.get(1)).toBeUndefined();
  });

  it('is empty for no cores', () => {
    expect(coreIndexById([]).size).toBe(0);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/flowCurve.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/flowCurve"` (또는 `coreIndexById` 가 export 되지 않음).

- [ ] **Step 3: `web/src/visual/flowCurve.ts`**

```ts
import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M7 스펙 6절. 그룹 천체에서 코어로 가는 2차 베지어 아치. 제어점을 중간점 위로
// 올려 무리 위로 솟았다 고리로 내려앉게 하고, key 해시로 옆으로 조금 흩어 같은
// 코어로 가는 선들이 한 줄로 겹치지 않게 한다.

export const ARCH = 0.35;
export const SPREAD = 0.3;
const SALT_SIDE = 61;

export function flowControlPoint(from: Vec3, to: Vec3, key: string): Vec3 {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const dz = to.z - from.z;
  const dist = Math.hypot(dx, dy, dz);
  // (to − from) × (0,1,0) = (−dz, 0, dx) — 수평 법선.
  const sideLength = Math.hypot(dz, dx);
  const sx = sideLength > 1e-9 ? -dz / sideLength : 1;
  const sz = sideLength > 1e-9 ? dx / sideLength : 0;
  const side = SPREAD * dist * (hash01(key, SALT_SIDE) - 0.5);
  return {
    x: (from.x + to.x) / 2 + sx * side,
    y: (from.y + to.y) / 2 + ARCH * dist,
    z: (from.z + to.z) / 2 + sz * side,
  };
}

// 곡선 위의 점. out 에 써서 돌려준다 — 매 프레임 수천 번 불리므로 할당하지 않는다.
export function curvePoint(from: Vec3, control: Vec3, to: Vec3, t: number, out: Vec3): Vec3 {
  const u = 1 - t;
  const a = u * u;
  const b = 2 * u * t;
  const c = t * t;
  out.x = a * from.x + b * control.x + c * to.x;
  out.y = a * from.y + b * control.y + c * to.y;
  out.z = a * from.z + b * control.z + c * to.z;
  return out;
}
```

- [ ] **Step 4: `web/src/visual/flowParticles.ts`**

```ts
// M7 스펙 7.2절. 선 위를 그룹에서 코어 쪽으로 흐르는 빛 입자.

export const MAX_PARTICLES_PER_EDGE = 12;
const PARTICLES_PER_STRENGTH = 30;

export function particleCount(strength: number): number {
  if (!(strength > 0)) {
    return 0;
  }
  return Math.min(MAX_PARTICLES_PER_EDGE, Math.ceil(strength * PARTICLES_PER_STRENGTH));
}

// 곡선 매개변수/초.
export function flowSpeed(weight: number): number {
  return 0.25 + 0.6 * Math.max(0, weight);
}

// 선의 흐름 위상을 누적한다(0~1 에서 감는다). 속도 × 시각 으로 계산하면 weight 가
// 바뀌는 순간 입자가 튄다 (M6 불꽃 위상과 같은 이유).
export function advanceFlowPhase(phase: number, weight: number, dtSec: number): number {
  const dt = Number.isFinite(dtSec) ? Math.max(0, dtSec) : 0;
  const next = phase + flowSpeed(weight) * dt;
  return next - Math.floor(next);
}

// 입자 i (n 개 중)의 곡선 매개변수. [0, 1).
export function particleT(phase: number, i: number, n: number): number {
  const t = phase + i / Math.max(1, n);
  return t - Math.floor(t);
}
```

- [ ] **Step 5: `web/src/visual/coreRing.ts` 전체 교체**

```ts
import type { Vec3 } from './layout';

// M6 스펙 4절. CPU 코어는 프로세스 무리를 두르는 XZ 평면 고리 위에 id 순서대로
// 같은 간격으로 놓인다. 위치는 고정이다 — M7 의 flow 가 이 자리를 끝점으로 쓴다.

export const RING_MIN_RADIUS = 28;
// 고리 둘레 위 이웃 코어 사이의 간격. 코어가 많으면 고리가 넓어져 Orb 가 겹치지 않는다.
export const ORB_SPACING = 4.5;

export function ringRadius(coreCount: number): number {
  return Math.max(RING_MIN_RADIUS, (Math.max(0, coreCount) * ORB_SPACING) / (2 * Math.PI));
}

// index 는 cores 배열에서의 순서다(엔진이 id 순으로 보낸다). 첫 코어는 +x 에 놓인다.
export function corePosition(index: number, coreCount: number): Vec3 {
  const r = ringRadius(coreCount);
  const theta = (2 * Math.PI * index) / Math.max(1, coreCount);
  return { x: r * Math.cos(theta), y: 0, z: r * Math.sin(theta) };
}

// flows[].core 는 코어 id 다. cores 배열에서의 순서(index)와 다를 수 있다 — 64 코어를
// 넘는 머신 등 (M7 스펙 5절). 같은 프레임의 cores 로 id → index 를 만든다.
export function coreIndexById(cores: readonly { id: number }[]): Map<number, number> {
  const map = new Map<number, number>();
  cores.forEach((core, index) => map.set(core.id, index));
  return map;
}
```

- [ ] **Step 6: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/flowCurve.test.ts`
Expected: PASS, `Tests  10 passed (10)`.

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  253 passed (253)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 8: 커밋**

```
git add src/visual/flowCurve.ts src/visual/flowParticles.ts src/visual/coreRing.ts tests/visual/flowCurve.test.ts
git commit -m "feat(web): compute flow arcs, travelling particles and the core id to index map"
```

---

## Task 3: 장면 — FlowStreams, 호버 강조

**Files:**
- Create: `web/src/scene/FlowStreams.tsx`
- Modify (전체 교체): `web/src/scene/sceneContext.ts`, `web/src/scene/SceneRoot.tsx`

**Interfaces:**
- Consumes: Task 1·2 의 모든 export. `colorFor` (`visual/mapping`), `coreColor`·`coreLoad` (`visual/coreMapping`), `corePosition` (`visual/coreRing`), `floatingPosition` (`visual/layout`), `FRAME_PRIORITY` (`scene/framePriority`), `DIM_DEPTH` (`scene/interaction`).
- Produces:
  - `class HoverFrame { current: Hovered; set(hovered: Hovered): void }`, `SceneContextValue.hover: HoverFrame`
  - `FlowStreams()` — SceneRoot 가 `<AmbientDust />` 다음에 마운트한다.

이 Task 는 R3F 장면 코드라 jsdom 에서 자동 테스트하지 않는다(계약서 10절). 컨트롤러가 실제 엔진에 붙여 확인한다.

R3F 사항 (코드가 이미 반영하고 있다):
- `FlowStreams` 는 `FRAME_PRIORITY.particles` 에서 돈다 — SceneRoot(-1)가 존재 추적·레이아웃을 정한 뒤다.
- 곡선 계산은 모듈 수준 임시 벡터(`from`, `to`, `a`, `b`)와 `curvePoint` 의 out 인자로 할당 없이 한다.
- 호버는 React 상태이므로 SceneRoot 가 `useEffect` 로 `context.hover.set(hovered)` 를 부른다.

- [ ] **Step 1: `web/src/scene/sceneContext.ts` 전체 교체**

```ts
import { createContext, useContext } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import type { InterpolatedGroup } from '../state/interpolator';
import type { FrameCache } from '../visual/frameCache';
import type { LayoutSim, Vec3 } from '../visual/layout';
import type { LifecycleEvent } from '../visual/lifecycleEvents';
import type { PresenceEntry, PresenceTracker } from '../visual/presence';

export type Hovered =
  | { kind: 'group'; key: string }
  | { kind: 'satellite'; pid: number }
  | { kind: 'core'; index: number }
  | null;

// CameraRig 가 매 프레임 채운다. 노드는 이것으로 어두워지고, 위성은 펼쳐진다.
// 장면 곳곳이 같은 인스턴스를 읽으므로 값은 메서드로만 바꾼다.
export class FocusFrame {
  // 지금 초점 그룹. 없으면 null.
  key: string | null = null;
  // 직전 초점 그룹. 초점을 푸는 동안 위성이 이 그룹으로 접혀 들어간다.
  previousKey: string | null = null;
  // 카메라 전환 진행도 0~1.
  t = 1;
  // 초점 강도 0~1. 초점이 잡힐수록 1, 풀릴수록 0.
  weight = 0;

  begin(key: string | null): void {
    this.previousKey = this.key;
    this.key = key;
  }

  sync(t: number, weight: number): void {
    this.t = t;
    this.weight = weight;
  }
}

// 이번 프레임에 소비한 lifecycle 이벤트. 대부분의 프레임에서 비어 있다.
export class FrameEvents {
  list: readonly LifecycleEvent[] = [];

  replace(events: readonly LifecycleEvent[]): void {
    this.list = events;
  }
}

// 지금 호버 중인 대상. SceneRoot 의 React 상태를 프레임 루프가 읽을 수 있게 비춘다
// (M7 flow 강조). 값은 메서드로만 바꾼다.
export class HoverFrame {
  current: Hovered = null;

  set(hovered: Hovered): void {
    this.current = hovered;
  }
}

// Satellites 가 매 프레임 채운다. 툴팁과 입자가 위성 위치를 읽는다.
export interface SatelliteView {
  position: Vec3;
  radius: number;
  child: ChildProcess;
  entry: PresenceEntry<ChildProcess>;
  account: ProcessGroup['account'];
}

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: LayoutSim;
  presence: PresenceTracker<InterpolatedGroup>;
  events: FrameEvents;
  focus: FocusFrame;
  hover: HoverFrame;
  satellites: Map<number, SatelliteView>;
  setHovered: (update: (current: Hovered) => Hovered) => void;
}

export const SceneContext = createContext<SceneContextValue | null>(null);

export function useSceneContext(): SceneContextValue {
  const value = useContext(SceneContext);
  if (value === null) {
    throw new Error('useSceneContext must be used inside <SceneRoot>');
  }
  return value;
}
```

- [ ] **Step 2: `web/src/scene/FlowStreams.tsx`**

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
import { DIM_DEPTH } from './interaction';
import { useSceneContext, type FocusFrame, type Hovered } from './sceneContext';

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

// 초점과 무관한 선은 다른 천체와 같은 비율로 어두워진다.
function dimFor(focus: FocusFrame, group: string): number {
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === group) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}

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
      const lineAlpha = Math.min(1, CLARITY[edge.source] * (0.25 + 1.6 * strength)) * shade;

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

- [ ] **Step 3: `web/src/scene/SceneRoot.tsx` 전체 교체**

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

  // 호버는 React 상태다. flow 강조가 프레임 루프에서 읽을 수 있게 비춘다.
  useEffect(() => {
    context.hover.set(hovered);
  }, [context, hovered]);
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
      <Particles pool={pool} />
      {liveHovered !== null && <Tooltip target={liveHovered} />}
    </SceneContext.Provider>
  );
}
```

- [ ] **Step 4: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  253 passed (253)` (이 Task 는 테스트를 더하지 않는다), `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐 — 새 `react(immutability)` 경고가 있으면 코드가 계획과 다른 것이다
- build 성공, `index-*.js` 약 1.36 MB, 청크 크기 경고 없음

- [ ] **Step 5: 브라우저 확인은 하지 않는다**

`pulse-engine` 도 dev 서버도 띄우지 않는다. 컨트롤러가 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 6: 커밋**

```
git add src/scene/sceneContext.ts src/scene/FlowStreams.tsx src/scene/SceneRoot.tsx
git commit -m "feat(web): draw flows as glowing arcs with travelling particles and hover highlighting"
```

---

## M7 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`(253), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `src/visual/` 이 `react`·`three`·`zustand`·`@react-three/*`·`gsap` 을 import 하지 않는다.
- [ ] 브라우저 (dev 서버 + 엔진):
  - 대기 중 바쁜 그룹에서 뜨거운 코어로 아치형 선과 흐르는 입자가 보인다. 선은 그룹 색에서 코어 열 색으로 번진다.
  - `node -e "const e=Date.now()+30000;while(Date.now()<e){}"` 4개로 부하를 걸면 선이 늘고, 선이 1초마다 번쩍이며 교체되지 않는다(잔광).
  - 그룹이나 코어에 호버하면 이어진 선만 밝고 나머지는 흐려진다. Focus 중 초점 그룹의 선만 밝다.
  - 콘솔 오류 없음 (R3F `THREE.Clock` 경고와 StrictMode 이중 연결 경고는 예상된 것).
- [ ] `npm run build` 후 `pulse-engine --serve --web-root ..\web\dist` 에서도 같다.

## 다음 단계

M8 이 Bloom·심도·최종 룩을 올린다. M6·M7 에서 넘긴 톤 매핑 차이(Orb 본체 셰이더는 톤 매핑을 거치지 않고 후광·불꽃·flow 선은 거친다)를 Bloom 의 선형 HDR 작업과 함께 정리한다.
