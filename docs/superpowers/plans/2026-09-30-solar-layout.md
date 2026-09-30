# 태양계형 배치 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 프로세스 그룹 천체의 배치를 힘 시뮬레이션에서 메모리 순위로 나눈 동심원 궤도로 바꾸고, 가운데에 시스템 별과 궤도선을 그린다.

**Architecture:** 궤도 계획(`planOrbits` — 순위는 궤도 선택에만 쓰고 궤도 안의 자리는 key 순으로 고정한다)·공전 속도·별 밝기는 `web/src/visual/solar.ts` 의 순수 함수다. `web/src/visual/layout.ts` 의 `LayoutSim`(힘 시뮬레이션)을 같은 API(`step`, `position`, `size`)를 가진 `OrbitLayout` 으로 바꾼다 — 천체는 궤도를 따라 공전하고, 궤도·자리가 바뀌면 극좌표에서 속도 상한을 두고 부드럽게 옮겨 간다. 장면에는 `SystemStar` 와 `OrbitRings` 를 더하고, 코어 고리를 넓히고, 카메라를 위에서 내려다보게 한다. 노드·툴팁·카메라 추적·위성·흐름·입자는 `layout.position(key)` 만 읽으므로 바뀌지 않는다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-30-solar-layout-design.md`.
- 작업 브랜치는 `feature/solar-layout` 다. `engine/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다.
- **`web/src/visual/orbits.ts` 는 Focus 위성 궤도 모듈이다. 건드리지 않는다.** 새 모듈의 이름은 `solar.ts` 다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `postprocessing`, `@react-three/*`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다.
- 프레임 값은 React 상태에 넣지 않는다. 시간에 따라 도는 값(공전각)은 누적한다.
- oxlint 에 새 경고가 생기면 안 된다 (특히 `react(immutability)`).
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 테스트·타입체크·린트·빌드가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
명령은 C:\dev\pulse-uni\web 에서 실행한다.
```

기준선(작업 전 `main` + 스펙 커밋): 웹 274 tests 통과.

## File Structure

```
web/src/visual/solar.ts          STAR_RADIUS, STAR_GAP, ARC_GAP, FILL, RING_GAP, OrbitRing, OrbitPlan, planOrbits,
                                 REF_RADIUS, SPIN_PERIOD_AT_REF, orbitSpeed, STAR_GAIN_IDLE, STAR_GAIN_BUSY, starGain
web/src/visual/layout.ts         (전체 교체) LayoutSim → OrbitLayout (step, position, size, plan). floatOffset·floatingPosition 은 그대로
web/src/scene/sceneContext.ts    (전체 교체) layout: OrbitLayout
web/src/visual/frameCache.ts     (전체 교체) 주석만: 배치의 입력이 궤도 배치(OrbitLayout)
web/src/scene/SceneRoot.tsx      (전체 교체) new OrbitLayout(), SystemStar·OrbitRings 마운트 (Task 2 와 Task 3 에서 두 번)
web/src/visual/coreRing.ts       (전체 교체) RING_MIN_RADIUS 28 → 34
web/src/visual/camera.ts         (전체 교체) OVERVIEW_POSE.position (0, 50, 66)
web/src/scene/SystemStar.tsx     가운데 별
web/src/scene/OrbitRings.tsx     궤도선
web/tests/visual/solar.test.ts
web/tests/visual/layout.test.ts  (전체 교체) OrbitLayout 테스트
web/tests/visual/coreRing.test.ts (전체 교체) 기대값 28 → RING_MIN_RADIUS
```

---

## Task 1: 궤도 계획

**Files:**
- Create: `web/src/visual/solar.ts`
- Test: `web/tests/visual/solar.test.ts`

**Interfaces:**
- Consumes: `LayoutNode { key: string; radius: number }` (타입만, `visual/layout.ts`), `radiusFor` (`visual/mapping.ts`, 테스트).
- Produces:
  - `STAR_RADIUS = 3.2`, `STAR_GAP = 3.0`, `ARC_GAP = 1.2`, `FILL = 0.75`, `RING_GAP = 3.0`
  - `interface OrbitRing { radius: number; keys: string[]; maxBodyRadius: number }`, `interface OrbitPlan { rings: OrbitRing[]; outerRadius: number }`
  - `planOrbits(nodes: readonly LayoutNode[]): OrbitPlan` — 빈 입력은 `{ rings: [], outerRadius: STAR_RADIUS }`
  - `REF_RADIUS = 20`, `SPIN_PERIOD_AT_REF = 180`, `orbitSpeed(radius: number): number` (rad/s)
  - `STAR_GAIN_IDLE = 0.9`, `STAR_GAIN_BUSY = 2.4`, `starGain(cpuPct: number | null): number`

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/solar.test.ts`**

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import { RING_MIN_RADIUS } from '../../src/visual/coreRing';
import { OUTSIDE_MARGIN, type LayoutNode } from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';
import {
  ARC_GAP,
  FILL,
  RING_GAP,
  STAR_GAIN_BUSY,
  STAR_GAIN_IDLE,
  STAR_GAP,
  STAR_RADIUS,
  orbitSpeed,
  planOrbits,
  starGain,
} from '../../src/visual/solar';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처의 40 개 그룹.
const nodes: LayoutNode[] = fixture.groups.map((group) => ({
  key: group.key,
  radius: radiusFor(group.mem_mb),
}));
const radiusOf = new Map(nodes.map((node) => [node.key, node.radius]));

describe('planOrbits', () => {
  it('fills the inner orbits with the largest bodies first', () => {
    const plan = planOrbits(nodes);
    // 순위는 어느 궤도에 들어가는지만 정한다: 안쪽 궤도의 가장 작은 천체도 바깥 궤도의
    // 가장 큰 천체보다 작지 않다.
    for (let k = 1; k < plan.rings.length; k += 1) {
      const innerSmallest = Math.min(...plan.rings[k - 1].keys.map((key) => radiusOf.get(key)!));
      const outerLargest = Math.max(...plan.rings[k].keys.map((key) => radiusOf.get(key)!));
      expect(innerSmallest).toBeGreaterThanOrEqual(outerLargest);
    }
    expect(plan.rings.flatMap((ring) => ring.keys)).toHaveLength(nodes.length);
  });

  it('orders the bodies inside an orbit by key, not by radius', () => {
    // 이웃한 두 천체의 순위가 바뀌어도 궤도 안의 자리는 그대로여야 서로를 뚫지 않는다.
    const before = planOrbits([
      { key: 'b.exe:2', radius: 1.02 },
      { key: 'a.exe:1', radius: 1.0 },
      { key: 'c.exe:3', radius: 0.9 },
    ]);
    const swapped = planOrbits([
      { key: 'b.exe:2', radius: 1.0 },
      { key: 'a.exe:1', radius: 1.02 },
      { key: 'c.exe:3', radius: 0.9 },
    ]);
    expect(before.rings[0].keys).toEqual(['a.exe:1', 'b.exe:2', 'c.exe:3']);
    expect(swapped.rings[0].keys).toEqual(before.rings[0].keys);
  });

  it('puts the same set of bodies in the same orbits whatever the input order', () => {
    const plan = planOrbits(nodes);
    for (const ring of plan.rings) {
      expect(ring.keys).toEqual([...ring.keys].sort());
    }
  });

  it('breaks radius ties by key when deciding which orbit a body joins', () => {
    const many = Array.from({ length: 40 }, (_, i) => ({ key: `k${String(i).padStart(2, '0')}`, radius: 0.3 }));
    const plan = planOrbits(many);
    expect(plan.rings.length).toBeGreaterThan(1);
    // 같은 반지름이면 key 순으로 안쪽 궤도부터 채운다.
    expect(plan.rings[0].keys).toEqual(many.slice(0, plan.rings[0].keys.length).map((n) => n.key));
  });

  it('keeps each orbit within its share of the circumference', () => {
    for (const ring of planOrbits(nodes).rings) {
      const used = ring.keys.reduce((sum, key) => sum + 2 * radiusOf.get(key)! + ARC_GAP, 0);
      // 한 개뿐인 궤도는 넘칠 수 있다 (궤도에는 최소 한 개가 들어간다).
      if (ring.keys.length > 1) {
        expect(used).toBeLessThanOrEqual(2 * Math.PI * ring.radius * FILL + 1e-9);
      }
    }
  });

  it('keeps bodies on neighbouring orbits from touching and the first orbit clear of the star', () => {
    const plan = planOrbits(nodes);
    expect(plan.rings[0].radius - plan.rings[0].maxBodyRadius).toBeCloseTo(STAR_RADIUS + STAR_GAP, 9);
    for (let k = 1; k < plan.rings.length; k += 1) {
      const inner = plan.rings[k - 1];
      const outer = plan.rings[k];
      expect(outer.radius - outer.maxBodyRadius - (inner.radius + inner.maxBodyRadius)).toBeCloseTo(
        RING_GAP,
        9,
      );
    }
  });

  it('splits the 40 fixture groups into a few orbits that stay inside the core ring', () => {
    const plan = planOrbits(nodes);
    expect(plan.rings.length).toBeGreaterThanOrEqual(2);
    expect(plan.rings.length).toBeLessThanOrEqual(5);
    const last = plan.rings[plan.rings.length - 1];
    expect(plan.outerRadius).toBeCloseTo(last.radius + last.maxBodyRadius, 9);
    // 코어 고리의 Orb 와 닿지 않는다. 새 천체가 나타나는 자리(가장 바깥 궤도 + OUTSIDE_MARGIN)도
    // 고리 안쪽이다.
    expect(plan.outerRadius + OUTSIDE_MARGIN).toBeLessThan(RING_MIN_RADIUS);
  });

  it('is deterministic and ignores the input order', () => {
    const reversed = [...nodes].reverse();
    expect(planOrbits(reversed)).toEqual(planOrbits(nodes));
  });

  it('plans nothing for no bodies', () => {
    expect(planOrbits([])).toEqual({ rings: [], outerRadius: STAR_RADIUS });
  });
});

describe('orbitSpeed', () => {
  it('turns inner orbits faster than outer ones', () => {
    expect(orbitSpeed(10)).toBeGreaterThan(orbitSpeed(20));
    expect(orbitSpeed(20)).toBeCloseTo((2 * Math.PI) / 180, 9);
    expect(orbitSpeed(10) / orbitSpeed(20)).toBeCloseTo(2 ** 1.5, 9);
  });
});

describe('starGain', () => {
  it('glows at idle and brightens with the system load', () => {
    expect(starGain(0)).toBe(STAR_GAIN_IDLE);
    expect(starGain(100)).toBe(STAR_GAIN_BUSY);
    expect(starGain(50)).toBeCloseTo((STAR_GAIN_IDLE + STAR_GAIN_BUSY) / 2, 9);
  });

  it('treats unknown load as idle and clamps out-of-range values', () => {
    expect(starGain(null)).toBe(STAR_GAIN_IDLE);
    expect(starGain(Number.NaN)).toBe(STAR_GAIN_IDLE);
    expect(starGain(250)).toBe(STAR_GAIN_BUSY);
    expect(starGain(-5)).toBe(STAR_GAIN_IDLE);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/solar.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/solar"`.

- [ ] **Step 3: `web/src/visual/solar.ts`**

```ts
import type { LayoutNode } from './layout';

// 태양계형 배치 스펙 4절 (파일 이름이 solar 인 것은 위성 궤도의 visual/orbits.ts 와 구별하려는 것). 그룹을 반지름(= 메모리) 내림차순으로 안쪽 궤도부터 채운다.
// 모든 궤도는 XZ 평면의 동심원이다. 순수 함수 — 같은 입력이면 같은 계획이다.

// 가운데 시스템 별의 반지름.
export const STAR_RADIUS = 3.2;
// 별 표면과 첫 궤도 천체 표면 사이의 틈.
export const STAR_GAP = 3.0;
// 같은 궤도에서 이웃한 천체 표면 사이에 두는 호 길이.
export const ARC_GAP = 1.2;
// 궤도 둘레 가운데 천체가 차지해도 되는 비율. 나머지는 빈틈이다.
export const FILL = 0.75;
// 이웃 궤도의 천체 표면 사이 틈.
export const RING_GAP = 3.0;

export interface OrbitRing {
  radius: number;
  // 안쪽 궤도부터, 궤도 안에서는 반지름 내림차순(같으면 key 오름차순)이다.
  keys: string[];
  // 이 궤도에서 가장 큰 천체의 반지름 (= 첫 천체).
  maxBodyRadius: number;
}

export interface OrbitPlan {
  rings: OrbitRing[];
  // 가장 바깥 궤도 반지름 + 그 궤도의 가장 큰 천체 반지름. 새 천체가 나타나는 자리의 기준이다.
  outerRadius: number;
}

// 반지름 내림차순, 같으면 key 오름차순. 순위가 흔들리지 않게 순서를 완전히 정한다.
function byRadiusThenKey(a: LayoutNode, b: LayoutNode): number {
  if (b.radius !== a.radius) {
    return b.radius - a.radius;
  }
  return a.key < b.key ? -1 : a.key > b.key ? 1 : 0;
}

export function planOrbits(nodes: readonly LayoutNode[]): OrbitPlan {
  const sorted = [...nodes].sort(byRadiusThenKey);
  const rings: OrbitRing[] = [];

  let index = 0;
  let previous: OrbitRing | null = null;
  while (index < sorted.length) {
    const first = sorted[index];
    const radius: number =
      previous === null
        ? STAR_RADIUS + STAR_GAP + first.radius
        : previous.radius + previous.maxBodyRadius + RING_GAP + first.radius;
    const capacity = 2 * Math.PI * radius * FILL;

    const ring: OrbitRing = { radius, keys: [], maxBodyRadius: first.radius };
    let used = 0;
    while (index < sorted.length) {
      const need = 2 * sorted[index].radius + ARC_GAP;
      // 궤도에는 최소 한 개가 들어간다.
      if (ring.keys.length > 0 && used + need > capacity) {
        break;
      }
      ring.keys.push(sorted[index].key);
      used += need;
      index += 1;
    }
    // 순위는 어느 궤도에 들어가는지만 정한다. 궤도 안의 자리는 key 순으로 고정한다 —
    // 메모리가 조금 흔들려 이웃한 두 천체의 순위가 바뀔 때마다 자리를 맞바꾸면 둘이 같은
    // 궤도에서 서로를 뚫고 지나간다 (검토에서 실제 40 개 그룹으로 측정: 메모리가 초당
    // 0.2% 흔들리면 프레임의 23% 에서 겹쳤다).
    ring.keys.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
    rings.push(ring);
    previous = ring;
  }

  const last = rings[rings.length - 1];
  return { rings, outerRadius: last === undefined ? STAR_RADIUS : last.radius + last.maxBodyRadius };
}

// 공전 각속도 (rad/s). 케플러처럼 안쪽일수록 빠르다 (ω ∝ r^-1.5).
// REF_RADIUS 궤도가 SPIN_PERIOD_AT_REF 초에 한 바퀴 돈다.
export const REF_RADIUS = 20;
export const SPIN_PERIOD_AT_REF = 180;

export function orbitSpeed(radius: number): number {
  const r = Math.max(1, radius);
  return ((2 * Math.PI) / SPIN_PERIOD_AT_REF) * (REF_RADIUS / r) ** 1.5;
}

// 가운데 별의 밝기 (선형 HDR 배율). 시스템 전체 CPU 사용률(0~100)을 따른다.
// 가장 한가해도 Bloom 임계값(0.5)을 넘어 은은히 빛나고, 바쁠수록 밝아진다.
export const STAR_GAIN_IDLE = 0.9;
export const STAR_GAIN_BUSY = 2.4;

export function starGain(cpuPct: number | null): number {
  const load = cpuPct === null || !Number.isFinite(cpuPct) ? 0 : Math.min(1, Math.max(0, cpuPct / 100));
  return STAR_GAIN_IDLE + (STAR_GAIN_BUSY - STAR_GAIN_IDLE) * load;
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/solar.test.ts`
Expected: PASS, `Tests  12 passed (12)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  286 passed (286)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 6: 커밋**

```
git add src/visual/solar.ts tests/visual/solar.test.ts
git commit -m "feat(web): plan concentric orbits by memory rank for a solar layout"
```

---

## Task 2: 궤도 배치기

**Files:**
- Modify (전체 교체): `web/src/visual/layout.ts`, `web/src/visual/frameCache.ts`, `web/src/scene/sceneContext.ts`, `web/src/scene/SceneRoot.tsx`, `web/tests/visual/layout.test.ts`

**Interfaces:**
- Consumes: Task 1 의 `planOrbits`, `orbitSpeed`, `OrbitPlan`.
- Produces:
  - `class OrbitLayout { get size(): number; position(key: string): Vec3 | undefined; plan(): OrbitPlan; step(nodes: readonly LayoutNode[], dtSec: number): void }` — `LayoutSim` 을 대신한다.
  - `MAX_DT = 1 / 30`, `SETTLE_TAU = 0.5`, `MAX_GLIDE_SPEED = 24`, `OUTSIDE_MARGIN = 4`, `RING_PHASE_STEP`
  - `floatOffset`, `floatingPosition(sim: OrbitLayout, key, timeSec)` 은 동작이 같다.
  - `SceneContextValue.layout: OrbitLayout`. 이 Task 의 `SceneRoot.tsx` 는 `new OrbitLayout()` 만 바뀐 중간 버전이다 (별과 궤도선은 Task 3).

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/layout.test.ts` 전체 교체**

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import {
  MAX_DT,
  OUTSIDE_MARGIN,
  OrbitLayout,
  SETTLE_TAU,
  floatOffset,
  floatingPosition,
  type LayoutNode,
  type Vec3,
} from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';
import { orbitSpeed, planOrbits } from '../../src/visual/solar';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처의 40 개 그룹.
const nodes: LayoutNode[] = fixture.groups.map((group) => ({
  key: group.key,
  radius: radiusFor(group.mem_mb),
}));

const FRAME = 1 / 60;

function planar(v: Vec3): number {
  return Math.hypot(v.x, v.z);
}

function angleOf(v: Vec3): number {
  return Math.atan2(v.z, v.x);
}

// from 에서 to 로 돈 각 (−π, π].
function turned(from: number, to: number): number {
  const d = to - from;
  return Math.atan2(Math.sin(d), Math.cos(d));
}

function distance(a: Vec3, b: Vec3): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function run(layout: OrbitLayout, list: readonly LayoutNode[], seconds: number): void {
  for (let t = 0; t < seconds; t += FRAME) {
    layout.step(list, FRAME);
  }
}

// 가장 작은 천체를 가장 크게 바꾼 목록. 그 천체는 가장 바깥 궤도에서 가장 안쪽 궤도로 옮겨 간다.
function withSmallestGrown(): { key: string; grown: LayoutNode[] } {
  const smallest = nodes.reduce((a, b) => (b.radius < a.radius ? b : a));
  return {
    key: smallest.key,
    grown: nodes.map((node) => (node.key === smallest.key ? { ...node, radius: 5 } : node)),
  };
}

describe('OrbitLayout', () => {
  it('places the very first bodies straight onto their orbits', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const plan = planOrbits(nodes);
    for (const ring of plan.rings) {
      for (const key of ring.keys) {
        const p = layout.position(key)!;
        expect(planar(p)).toBeCloseTo(ring.radius, 6);
        expect(p.y).toBe(0);
      }
    }
    expect(layout.plan()).toEqual(plan);
  });

  it('spreads the bodies of an orbit evenly around it', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const ring = planOrbits(nodes).rings[1];
    const angles = ring.keys.map((key) => angleOf(layout.position(key)!));
    const step = (2 * Math.PI) / ring.keys.length;
    for (let i = 1; i < angles.length; i += 1) {
      const gap = (((angles[i] - angles[i - 1]) % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);
      expect(gap).toBeCloseTo(step, 6);
    }
  });

  it('turns inner orbits faster than outer ones', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const plan = planOrbits(nodes);
    const inner = plan.rings[0].keys[0];
    const outer = plan.rings[plan.rings.length - 1].keys[0];
    const innerFrom = angleOf(layout.position(inner)!);
    const outerFrom = angleOf(layout.position(outer)!);

    run(layout, nodes, 2);

    const innerTurn = turned(innerFrom, angleOf(layout.position(inner)!));
    const outerTurn = turned(outerFrom, angleOf(layout.position(outer)!));
    expect(innerTurn).toBeGreaterThan(outerTurn);
    // 평활이 목표 각을 약간 늦게 따라가므로 소수 한 자리까지만 본다.
    expect(innerTurn).toBeCloseTo(orbitSpeed(plan.rings[0].radius) * 2, 1);
  });

  it('brings a body that arrives later in from outside every orbit', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const outer = layout.plan().outerRadius;

    const newcomer: LayoutNode = { key: 'late.exe:9999', radius: 1 };
    const grown = [...nodes, newcomer];
    layout.step(grown, FRAME);
    expect(planar(layout.position(newcomer.key)!)).toBeGreaterThanOrEqual(outer + OUTSIDE_MARGIN - 0.5);

    run(layout, grown, SETTLE_TAU * 10);
    const ring = layout.plan().rings.find((r) => r.keys.includes(newcomer.key))!;
    expect(planar(layout.position(newcomer.key)!)).toBeCloseTo(ring.radius, 2);
  });

  it('glides a body to its new orbit instead of jumping', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const { key, grown } = withSmallestGrown();

    let previous = { ...layout.position(key)! };
    let largestJump = 0;
    for (let t = 0; t < SETTLE_TAU * 10; t += FRAME) {
      layout.step(grown, FRAME);
      const now = layout.position(key)!;
      largestJump = Math.max(largestJump, distance(now, previous));
      previous = { ...now };
    }
    expect(largestJump).toBeLessThan(1);
    expect(planar(layout.position(key)!)).toBeCloseTo(layout.plan().rings[0].radius, 2);
  });

  it('never cuts across the star while changing orbits', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const { key, grown } = withSmallestGrown();
    const innermost = Math.min(layout.plan().rings[0].radius, planOrbits(grown).rings[0].radius);
    for (let t = 0; t < SETTLE_TAU * 10; t += FRAME) {
      layout.step(grown, FRAME);
      expect(planar(layout.position(key)!)).toBeGreaterThanOrEqual(innermost - 1e-6);
    }
  });

  it('does not let two bodies pass through each other when their memory ranks swap', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    // 가장 큰 두 천체(같은 궤도)의 반지름을 서로 바꾼다 — 순위만 뒤집힌다.
    const ranked = [...nodes].sort((a, b) => b.radius - a.radius);
    const [first, second] = ranked;
    const swapped = nodes.map((node) =>
      node.key === first.key
        ? { ...node, radius: second.radius }
        : node.key === second.key
          ? { ...node, radius: first.radius }
          : node,
    );
    const before = { a: { ...layout.position(first.key)! }, b: { ...layout.position(second.key)! } };
    let closest = Infinity;
    for (let t = 0; t < SETTLE_TAU * 10; t += FRAME) {
      layout.step(swapped, FRAME);
      closest = Math.min(closest, distance(layout.position(first.key)!, layout.position(second.key)!));
    }
    // 두 천체는 자리를 맞바꾸지 않고, 서로 겹치는 데까지 다가가지도 않는다.
    expect(closest).toBeGreaterThan(first.radius + second.radius - 0.5);
    expect(distance(layout.position(first.key)!, before.a)).toBeLessThan(first.radius + second.radius + 30);
  });

  it('forgets keys that disappear', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const rest = nodes.slice(1);
    layout.step(rest, FRAME);
    expect(layout.size).toBe(rest.length);
    expect(layout.position(nodes[0].key)).toBeUndefined();
  });

  it('advances at most MAX_DT per call', () => {
    const a = new OrbitLayout();
    const b = new OrbitLayout();
    a.step(nodes, FRAME);
    b.step(nodes, FRAME);
    a.step(nodes, 10);
    b.step(nodes, MAX_DT);
    expect(a.position(nodes[0].key)).toEqual(b.position(nodes[0].key));
  });

  it('treats a NaN or negative frame time as no time at all', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const before = { ...layout.position(nodes[0].key)! };
    layout.step(nodes, Number.NaN);
    layout.step(nodes, -1);
    expect(layout.position(nodes[0].key)).toEqual(before);
  });

  it('produces identical positions for identical inputs', () => {
    const a = new OrbitLayout();
    const b = new OrbitLayout();
    run(a, nodes, 1);
    run(b, nodes, 1);
    for (const node of nodes) {
      expect(a.position(node.key)).toEqual(b.position(node.key));
    }
  });

  it('does nothing with an empty node list', () => {
    const layout = new OrbitLayout();
    layout.step([], FRAME);
    expect(layout.size).toBe(0);
    expect(layout.plan().rings).toEqual([]);
  });
});

describe('floatOffset', () => {
  it('stays within the float amplitude on every axis', () => {
    for (let t = 0; t < 30; t += 0.37) {
      const offset = floatOffset('app.exe:100', t);
      for (const value of [offset.x, offset.y, offset.z]) {
        expect(Math.abs(value)).toBeLessThanOrEqual(0.2 + 1e-9);
      }
    }
  });

  it('differs between keys at the same moment', () => {
    expect(floatOffset('a.exe:1', 3)).not.toEqual(floatOffset('b.exe:2', 3));
  });
});

describe('floatingPosition', () => {
  it('adds the float offset to the orbit position', () => {
    const layout = new OrbitLayout();
    layout.step(nodes, FRAME);
    const key = nodes[0].key;
    const base = layout.position(key)!;
    const offset = floatOffset(key, 4);

    expect(floatingPosition(layout, key, 4)).toEqual({
      x: base.x + offset.x,
      y: base.y + offset.y,
      z: base.z + offset.z,
    });
  });

  it('returns undefined for an unknown key', () => {
    expect(floatingPosition(new OrbitLayout(), 'nope', 0)).toBeUndefined();
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/layout.test.ts`
Expected: FAIL — `OrbitLayout` 이 export 되지 않아 테스트가 실패한다 (`OrbitLayout is not a constructor` 또는 동등한 오류).

- [ ] **Step 3: `web/src/visual/layout.ts` 전체 교체**

```ts
import { hash01 } from './hash';
import { orbitSpeed, planOrbits, type OrbitPlan } from './solar';

// 스펙 6절. three 를 모르는 순수 모듈이다. 위치는 평범한 객체로 다룬다.
export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface LayoutNode {
  key: string;
  radius: number;
}

// 태양계형 배치 스펙 5절. 그룹은 메모리 순위로 정한 동심원 궤도(visual/solar.ts 의 planOrbits)를
// 따라 공전한다. 궤도나 자리가 바뀌면 극좌표(반지름, 각)에서 부드럽게 옮겨 간다 —
// 순간이동하지 않고, 가운데를 가로지르지도 않는다.

// 탭이 백그라운드에 있다 돌아오면 수 초짜리 dt 가 들어온다. 공전이 한 번에 크게
// 건너뛰지 않게 자른다.
export const MAX_DT = 1 / 30;
// 목표 자리로 따라가는 지수 평활의 시간 상수(초). 약 3 배(1.5 초)면 거의 도착한다.
export const SETTLE_TAU = 0.5;
// 옮겨 가는 속도의 상한 (장면 단위/초). 평활은 처음 몇 프레임이 가장 빨라서, 궤도 반대편
// 자리로 옮겨 가면 순간적으로 초당 100 단위 넘게 휩쓸고 지나간다. 공전 자체(바깥 궤도에서
// 초당 1 단위 안팎)는 이 상한에 걸리지 않는다.
export const MAX_GLIDE_SPEED = 24;
// 자리 잡은 뒤 들어오는 key 는 가장 바깥 궤도의 천체보다 이만큼 바깥에서 나타난다.
// 무리 안쪽에서 생기면 형성 연출이 다른 천체에 가려진다 (M5 스펙 8절).
export const OUTSIDE_MARGIN = 4;
// 궤도마다 첫 자리를 이만큼씩 돌려 둔다 (황금각). 궤도의 첫 천체들이 한 줄로 서지 않는다.
export const RING_PHASE_STEP = Math.PI * (3 - Math.sqrt(5));

interface Body {
  // 현재 극좌표. XZ 평면이다.
  radius: number;
  angle: number;
  position: Vec3;
}

// a 에서 b 로 가는 가장 짧은 각 차이 (−π, π].
function shortestTurn(from: number, to: number): number {
  const turn = (to - from) % (2 * Math.PI);
  if (turn > Math.PI) {
    return turn - 2 * Math.PI;
  }
  if (turn <= -Math.PI) {
    return turn + 2 * Math.PI;
  }
  return turn;
}

export class OrbitLayout {
  private readonly bodies = new Map<string, Body>();
  // 궤도 순번마다 누적한 공전각. 궤도 반지름이 바뀌어도 순번의 각은 이어진다.
  private readonly spins: number[] = [];
  private current: OrbitPlan = { rings: [], outerRadius: 0 };

  get size(): number {
    return this.bodies.size;
  }

  position(key: string): Vec3 | undefined {
    return this.bodies.get(key)?.position;
  }

  // 지금의 궤도 계획. 궤도선을 그리는 쪽이 읽는다.
  plan(): OrbitPlan {
    return this.current;
  }

  step(nodes: readonly LayoutNode[], dtSec: number): void {
    // NaN 이 들어가면 이후 모든 각이 NaN 이 된다.
    const dt = Number.isFinite(dtSec) ? Math.min(Math.max(dtSec, 0), MAX_DT) : 0;
    const wasEmpty = this.bodies.size === 0;
    const previousOuter = this.current.outerRadius;
    const plan = planOrbits(nodes);
    this.current = plan;

    const follow = 1 - Math.exp(-dt / SETTLE_TAU);
    const present = new Set<string>();
    plan.rings.forEach((ring, k) => {
      const spin = (this.spins[k] ?? 0) + orbitSpeed(ring.radius) * dt;
      this.spins[k] = spin % (2 * Math.PI);
      ring.keys.forEach((key, i) => {
        present.add(key);
        const targetAngle = this.spins[k] + k * RING_PHASE_STEP + (2 * Math.PI * i) / ring.keys.length;
        let body = this.bodies.get(key);
        if (body === undefined) {
          // 처음 화면은 이미 정리된 모양으로 뜬다. 그 뒤의 새 천체는 바깥에서 들어온다.
          const startRadius = wasEmpty
            ? ring.radius
            : Math.max(previousOuter, plan.outerRadius) + OUTSIDE_MARGIN;
          body = { radius: startRadius, angle: targetAngle, position: { x: 0, y: 0, z: 0 } };
          this.bodies.set(key, body);
        } else {
          const reach = MAX_GLIDE_SPEED * dt;
          const dr = (ring.radius - body.radius) * follow;
          body.radius += Math.max(-reach, Math.min(reach, dr));
          // 각은 호 길이로 제한한다: 같은 각이라도 바깥 궤도에서는 더 먼 거리다.
          const maxTurn = reach / Math.max(1, body.radius);
          const da = shortestTurn(body.angle, targetAngle) * follow;
          body.angle += Math.max(-maxTurn, Math.min(maxTurn, da));
        }
        body.position.x = body.radius * Math.cos(body.angle);
        body.position.y = 0;
        body.position.z = body.radius * Math.sin(body.angle);
      });
    });

    for (const key of this.bodies.keys()) {
      if (!present.has(key)) {
        this.bodies.delete(key);
      }
    }
  }
}

const FLOAT_AMPLITUDE = 0.2;
const SALT_FLOAT_PERIOD = 21;
const SALT_FLOAT_PHASE = 24;

// 부유. 배치 상태에는 들어가지 않고 그릴 때만 더한다 — 계약서 7.1 절.
// 축마다 주기(6~10 초)와 위상이 key 해시로 다르다.
export function floatOffset(key: string, timeSec: number): Vec3 {
  const axis = (i: number) => {
    const period = 6 + 4 * hash01(key, SALT_FLOAT_PERIOD + i);
    const phase = 2 * Math.PI * hash01(key, SALT_FLOAT_PHASE + i);
    return FLOAT_AMPLITUDE * Math.sin((2 * Math.PI * timeSec) / period + phase);
  };
  return { x: axis(0), y: axis(1), z: axis(2) };
}

// 노드와 툴팁이 같은 자리를 가리키도록 둘 다 이 함수로 위치를 구한다.
export function floatingPosition(
  sim: OrbitLayout,
  key: string,
  timeSec: number,
): Vec3 | undefined {
  const base = sim.position(key);
  if (base === undefined) {
    return undefined;
  }
  const offset = floatOffset(key, timeSec);
  return { x: base.x + offset.x, y: base.y + offset.y, z: base.z + offset.z };
}
```

- [ ] **Step 4a: `web/src/visual/frameCache.ts` 전체 교체 (주석만 바뀐다)**

```ts
import type { InterpolatedGroup, InterpolatedSnapshot } from '../state/interpolator';
import type { LayoutNode } from './layout';
import { radiusFor } from './mapping';

// 스펙 4절. 장면 루트가 프레임마다 한 번 채우고, 노드들은 key 로 읽는다.
// React 상태가 아니라 가변 객체다 — 매 프레임 새로 만들지 않는다.
export interface FrameCache {
  snapshot: InterpolatedSnapshot | null;
  byKey: Map<string, InterpolatedGroup>;
  // activityFor 의 코어 수. 현재 스냅샷의 cores.length, 최소 1.
  coreCount: number;
  // performance.now() 기준 초. 노드의 부유와 툴팁이 같은 시각을 쓰게 한다.
  // 한 번도 갱신되지 않았으면 null.
  timeSec: number | null;
  // 직전 갱신과의 간격(초). 첫 갱신은 0.
  dtSec: number;
}

export function createFrameCache(): FrameCache {
  return { snapshot: null, byKey: new Map(), coreCount: 1, timeSec: null, dtSec: 0 };
}

export function updateFrameCache(
  cache: FrameCache,
  snapshot: InterpolatedSnapshot | null,
  nowMs: number,
): void {
  const timeSec = nowMs / 1000;
  cache.dtSec = cache.timeSec === null ? 0 : Math.max(0, timeSec - cache.timeSec);
  cache.timeSec = timeSec;

  cache.snapshot = snapshot;
  cache.byKey.clear();
  if (snapshot === null) {
    return;
  }
  for (const group of snapshot.groups) {
    cache.byKey.set(group.key, group);
  }
  cache.coreCount = Math.max(1, snapshot.cores.length);
}

// 궤도 배치(OrbitLayout)의 입력. M5 부터는 스냅샷의 그룹이 아니라 존재 추적기의
// 항목(떠나는 중인 그룹 포함)을 넘긴다 — 사라지는 천체도 연출이 끝날 때까지
// 제자리를 지켜야 한다.
export function layoutNodesFrom(
  groups: Iterable<Pick<InterpolatedGroup, 'key' | 'mem_mb'>>,
): LayoutNode[] {
  const nodes: LayoutNode[] = [];
  for (const group of groups) {
    nodes.push({ key: group.key, radius: radiusFor(group.mem_mb) });
  }
  return nodes;
}
```

- [ ] **Step 4: `web/src/scene/sceneContext.ts` 전체 교체**

```ts
import { createContext, useContext } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import type { InterpolatedGroup } from '../state/interpolator';
import type { FrameCache } from '../visual/frameCache';
import type { OrbitLayout, Vec3 } from '../visual/layout';
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
  layout: OrbitLayout;
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

- [ ] **Step 5: `web/src/scene/SceneRoot.tsx` 전체 교체 (중간 버전)**

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
import { OrbitLayout } from '../visual/layout';
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
      layout: new OrbitLayout(),
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

- [ ] **Step 6: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/layout.test.ts`
Expected: PASS, `Tests  16 passed (16)`.

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  284 passed (284)` (힘 시뮬레이션 전용 테스트 18 개가 궤도 테스트 16 개로 바뀐다), typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 8: 커밋**

```
git add src/visual/layout.ts src/visual/frameCache.ts src/scene/sceneContext.ts src/scene/SceneRoot.tsx tests/visual/layout.test.ts
git commit -m "feat(web): replace the force layout with orbiting bodies that glide between orbits"
```

---

## Task 3: 장면 — 별, 궤도선, 코어 고리, 카메라

**Files:**
- Create: `web/src/scene/SystemStar.tsx`, `web/src/scene/OrbitRings.tsx`
- Modify (전체 교체): `web/src/scene/SceneRoot.tsx`, `web/src/visual/coreRing.ts`, `web/src/visual/camera.ts`, `web/tests/visual/coreRing.test.ts`

**Interfaces:**
- Consumes: Task 1 의 `STAR_RADIUS`, `starGain`. Task 2 의 `OrbitLayout.plan()`, `SETTLE_TAU`. 기존 `DIM_DEPTH` (`scene/interaction.ts`), `useSceneContext` (`cache.snapshot?.system.cpu_pct`, `cache.dtSec`, `focus.weight`, `layout`).
- Produces: `SystemStar()`, `OrbitRings()` — `SceneRoot` 가 `<CameraRig />` 다음에 마운트한다. `RING_MIN_RADIUS = 34`. `OVERVIEW_POSE.position = { x: 0, y: 50, z: 66 }`.

이 Task 는 R3F 장면 코드라 jsdom 에서 자동 테스트하지 않는다(계약서 10절). 컨트롤러가 실제 엔진에 붙여 확인한다.

- [ ] **Step 1: `web/tests/visual/coreRing.test.ts` 전체 교체**

```ts
import { describe, expect, it } from 'vitest';

import {
  COLD,
  advanceNoiseOffset,
  HOT,
  NOISE_PERIOD,
  WARM,
  coreColor,
  coreHsl,
  coreHaloOpacity,
  coreLoad,
  distortion,
  hslToRgb,
  noiseSpeed,
  orbRadius,
  rimIntensity,
} from '../../src/visual/coreMapping';
import { ORB_SPACING, RING_MIN_RADIUS, corePosition, ringRadius } from '../../src/visual/coreRing';

const ORIGIN = { x: 0, y: 0, z: 0 };

function distance(a: { x: number; y: number; z: number }, b = ORIGIN): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

describe('coreRing', () => {
  it('uses the minimum radius for 28 cores and widens for many cores', () => {
    expect(ringRadius(28)).toBe(RING_MIN_RADIUS);
    expect(ringRadius(64)).toBeCloseTo((64 * ORB_SPACING) / (2 * Math.PI), 6);
    expect(ringRadius(64)).toBeCloseTo(45.84, 2);
  });

  it('places cores evenly on a flat ring, the first one on +x', () => {
    const first = corePosition(0, 28);
    expect(first.x).toBeCloseTo(RING_MIN_RADIUS, 6);
    expect(first.z).toBeCloseTo(0, 6);
    for (let i = 0; i < 28; i += 1) {
      const p = corePosition(i, 28);
      expect(p.y).toBe(0);
      expect(distance(p)).toBeCloseTo(RING_MIN_RADIUS, 6);
    }
    const gap = distance(corePosition(0, 28), corePosition(1, 28));
    expect(distance(corePosition(13, 28), corePosition(14, 28))).toBeCloseTo(gap, 6);
  });

  it('keeps neighbours at least ORB_SPACING apart even with many cores', () => {
    expect(distance(corePosition(0, 64), corePosition(1, 64))).toBeGreaterThanOrEqual(
      ORB_SPACING * 0.99,
    );
  });

  it('is deterministic', () => {
    expect(corePosition(5, 28)).toEqual(corePosition(5, 28));
  });
});

describe('coreMapping', () => {
  it('clamps the load to [0, 1]', () => {
    expect(coreLoad(-5)).toBe(0);
    expect(coreLoad(50)).toBe(0.5);
    expect(coreLoad(130)).toBe(1);
  });

  it('matches the spec table', () => {
    expect(orbRadius(0)).toBeCloseTo(0.7, 6);
    expect(orbRadius(1)).toBeCloseTo(1.6, 6);
    expect(distortion(0.05)).toBeCloseTo(0.0407, 4);
    expect(distortion(0.5)).toBeCloseTo(0.11, 6);
    expect(distortion(0.9)).toBeCloseTo(0.2668, 4);
    expect(noiseSpeed(0)).toBeCloseTo(0.25, 6);
    expect(noiseSpeed(1)).toBeCloseTo(2.0, 6);
    expect(rimIntensity(0)).toBeCloseTo(0.3, 6);
    expect(rimIntensity(1)).toBeCloseTo(1.5, 6);
    expect(coreHaloOpacity(0)).toBeCloseTo(0.04, 6);
    expect(coreHaloOpacity(1)).toBeCloseTo(0.34, 6);
  });

  it('runs the colour from cold through warm to hot', () => {
    expect(coreColor(0)).toEqual(hslToRgb(COLD.h, COLD.s, COLD.l));
    expect(coreColor(0.5)).toEqual(hslToRgb(WARM.h, WARM.s, WARM.l));
    expect(coreColor(1)).toEqual(hslToRgb(HOT.h, HOT.s, HOT.l));
    // 파랑 → 보라 → 자홍 → 주황: 색조는 증가 방향으로만 돈다.
    expect(coreHsl(0.25).h).toBeCloseTo((COLD.h + WARM.h) / 2, 6);
  });

  it('never passes through grey on the way from cold to warm', () => {
    // RGB 로 섞으면 파랑과 주황 사이가 회색이 된다. 채도(최대−최소 채널)가 남아 있어야 한다.
    for (let load = 0; load <= 1.0001; load += 0.05) {
      const [r, g, b] = coreColor(load);
      expect(Math.max(r, g, b) - Math.min(r, g, b)).toBeGreaterThan(0.3);
    }
  });

  it('converts well-known HSL colours to RGB', () => {
    const red = hslToRgb(0, 1, 0.5);
    expect(red[0]).toBeCloseTo(1, 6);
    expect(red[1]).toBeCloseTo(0, 6);
    expect(red[2]).toBeCloseTo(0, 6);
    const blue = hslToRgb(240, 1, 0.5);
    expect(blue[2]).toBeCloseTo(1, 6);
    expect(hslToRgb(360 + 120, 1, 0.5)).toEqual(hslToRgb(120, 1, 0.5));
    expect(hslToRgb(0, 0, 0.3)).toEqual([0.3, 0.3, 0.3]);
  });

  it('accumulates the noise offset by speed × dt and ignores negative time', () => {
    expect(advanceNoiseOffset(10, 1, 0.5)).toBeCloseTo(11, 9);
    expect(advanceNoiseOffset(10, 0, 2)).toBeCloseTo(10.5, 9);
    expect(advanceNoiseOffset(10, 1, -1)).toBe(10);
  });

  it('wraps the noise offset at NOISE_PERIOD so it stays inside [0, NOISE_PERIOD)', () => {
    // 부하 0 의 속도는 0.25 이므로 dt 2 초면 정확히 0.5 만큼 나아간다.
    expect(advanceNoiseOffset(NOISE_PERIOD - 0.1, 0, 2)).toBeCloseTo(
      NOISE_PERIOD - 0.1 + 0.5 - NOISE_PERIOD,
      9,
    );
    let offset = 0;
    for (let i = 0; i < 5000; i += 1) {
      offset = advanceNoiseOffset(offset, 1, 0.1);
      expect(offset).toBeGreaterThanOrEqual(0);
      expect(offset).toBeLessThan(NOISE_PERIOD);
    }
  });

  it('keeps an idle core nearly smooth', () => {
    // 제곱 매핑: 5% 부하의 일그러짐은 최소값과 거의 같다.
    expect(distortion(0.05) - distortion(0)).toBeLessThan(0.001);
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/coreRing.test.ts`
Expected: FAIL — `expected 28 to be close to 34` (반지름이 아직 28 이다).

- [ ] **Step 3: `web/src/visual/coreRing.ts` 전체 교체**

```ts
import type { Vec3 } from './layout';

// M6 스펙 4절. CPU 코어는 프로세스 무리를 두르는 XZ 평면 고리 위에 id 순서대로
// 같은 간격으로 놓인다. 위치는 고정이다 — M7 의 flow 가 이 자리를 끝점으로 쓴다.

// 태양계형 배치(스펙 D63): 가장 바깥 궤도의 천체(기본 40 그룹에서 끝 약 27)보다 바깥이다.
export const RING_MIN_RADIUS = 34;
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

- [ ] **Step 4: `web/src/visual/camera.ts` 전체 교체**

```ts
import type { Vec3 } from './layout';

// M5 스펙 9.2. GSAP 은 전환 진행도 하나만 움직인다. 카메라 자세는 매 프레임
// 이 함수들로 계산한다 — 그래서 떠다니는 천체를 따라갈 수 있다.

export interface Pose {
  position: Vec3;
  target: Vec3;
}

// 태양계형 배치 스펙 D64: 궤도가 한 평면에 있으므로 위에서 비스듬히 내려다본다.
export const OVERVIEW_POSE: Pose = {
  position: { x: 0, y: 50, z: 66 },
  target: { x: 0, y: 0, z: 0 },
};

export const FOCUS_DISTANCE_PER_RADIUS = 7;
export const FOCUS_DISTANCE_BASE = 6;

export function focusDistance(radius: number): number {
  return radius * FOCUS_DISTANCE_PER_RADIUS + FOCUS_DISTANCE_BASE;
}

// 노드에서 카메라 쪽을 가리키는 단위 벡터. 전환 시작 때 한 번 정해 두면
// 카메라가 지금 보던 쪽에서 곧장 다가간다. 두 점이 겹치면 +z.
export function focusDirection(camera: Vec3, node: Vec3): Vec3 {
  const dx = camera.x - node.x;
  const dy = camera.y - node.y;
  const dz = camera.z - node.z;
  const length = Math.hypot(dx, dy, dz);
  if (length < 1e-6) {
    return { x: 0, y: 0, z: 1 };
  }
  return { x: dx / length, y: dy / length, z: dz / length };
}

export function focusPose(node: Vec3, radius: number, direction: Vec3): Pose {
  const d = focusDistance(radius);
  return {
    position: {
      x: node.x + direction.x * d,
      y: node.y + direction.y * d,
      z: node.z + direction.z * d,
    },
    target: { ...node },
  };
}

function lerpVec(a: Vec3, b: Vec3, t: number): Vec3 {
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t };
}

export function blendPose(from: Pose, to: Pose, t: number): Pose {
  return {
    position: lerpVec(from.position, to.position, t),
    target: lerpVec(from.target, to.target, t),
  };
}
```

- [ ] **Step 5: `web/src/scene/SystemStar.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import { AdditiveBlending, Color, SRGBColorSpace, type MeshBasicMaterial } from 'three';

import { STAR_RADIUS, starGain } from '../visual/solar';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 따뜻한 흰색 (sRGB).
const STAR_RGB: [number, number, number] = [1.0, 0.86, 0.62];
const HALO_SCALE = 1.6;
const HALO_OPACITY = 0.18;

// 태양계형 배치 스펙 6절. 궤도의 중심. 크기는 고정이고 밝기는 시스템 전체 CPU 사용률을
// 따른다. 초점이 잡히면 다른 천체처럼 어두워진다. 포인터 이벤트는 받지 않는다.
export function SystemStar() {
  const { cache, focus } = useSceneContext();
  const body = useRef<MeshBasicMaterial>(null);
  const halo = useRef<MeshBasicMaterial>(null);

  useFrame(() => {
    if (body.current === null || halo.current === null) {
      return;
    }
    const dim = 1 - DIM_DEPTH * focus.weight;
    const gain = starGain(cache.snapshot?.system.cpu_pct ?? null) * dim;
    body.current.color.setRGB(STAR_RGB[0], STAR_RGB[1], STAR_RGB[2], SRGBColorSpace).multiplyScalar(gain);
    halo.current.opacity = HALO_OPACITY * dim;
  });

  return (
    <group raycast={() => null}>
      <mesh raycast={() => null}>
        <sphereGeometry args={[STAR_RADIUS, 48, 48]} />
        <meshBasicMaterial ref={body} color={new Color(1, 1, 1)} />
      </mesh>
      <mesh raycast={() => null} scale={HALO_SCALE}>
        <sphereGeometry args={[STAR_RADIUS, 32, 32]} />
        <meshBasicMaterial
          ref={halo}
          color="#ffd9a0"
          transparent
          opacity={HALO_OPACITY}
          depthWrite={false}
          blending={AdditiveBlending}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 6: `web/src/scene/OrbitRings.tsx`**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry } from 'three';

import { SETTLE_TAU } from '../visual/layout';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 궤도 하나를 이만큼의 선분으로 그린다.
const SEGMENTS = 128;
// 이보다 많은 궤도는 그리지 않는다 (기본 40 그룹에서 3~4 개).
const MAX_RINGS = 16;
const RING_OPACITY = 0.22;
const RING_COLOR = '#7d93bd';

// 태양계형 배치 스펙 6절. 궤도 계획의 궤도마다 가는 원. 그룹이 들고 나며 궤도 반지름이
// 바뀌면 천체와 같은 속도로 따라가 옮겨 간다. 초점이 잡히면 더 흐려진다.
export function OrbitRings() {
  const { cache, layout, focus } = useSceneContext();
  // 궤도마다 지금 그리고 있는 반지름. 계획의 반지름으로 지수 평활한다.
  const shown = useRef<number[]>([]);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(new Float32Array(MAX_RINGS * SEGMENTS * 2 * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);
  useEffect(() => () => geometry.dispose(), [geometry]);
  const material = useRef<{ opacity: number } | null>(null);

  useFrame(() => {
    const rings = layout.plan().rings.slice(0, MAX_RINGS);
    const radii = shown.current;
    const follow = 1 - Math.exp(-cache.dtSec / SETTLE_TAU);
    radii.length = rings.length;
    rings.forEach((ring, k) => {
      const current = radii[k];
      radii[k] = current === undefined ? ring.radius : current + (ring.radius - current) * follow;
    });

    const positions = geometry.getAttribute('position') as BufferAttribute;
    let vertex = 0;
    for (const r of radii) {
      for (let s = 0; s < SEGMENTS; s += 1) {
        const a0 = (2 * Math.PI * s) / SEGMENTS;
        const a1 = (2 * Math.PI * (s + 1)) / SEGMENTS;
        positions.setXYZ(vertex, r * Math.cos(a0), 0, r * Math.sin(a0));
        positions.setXYZ(vertex + 1, r * Math.cos(a1), 0, r * Math.sin(a1));
        vertex += 2;
      }
    }
    geometry.setDrawRange(0, vertex);
    positions.needsUpdate = true;

    if (material.current !== null) {
      material.current.opacity = RING_OPACITY * (1 - DIM_DEPTH * focus.weight);
    }
  });

  return (
    <lineSegments geometry={geometry} frustumCulled={false} raycast={() => null}>
      <lineBasicMaterial
        ref={material}
        color={RING_COLOR}
        transparent
        opacity={RING_OPACITY}
        blending={AdditiveBlending}
        depthWrite={false}
      />
    </lineSegments>
  );
}
```

- [ ] **Step 7: `web/src/scene/SceneRoot.tsx` 전체 교체 (최종)**

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
import { OrbitLayout } from '../visual/layout';
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
import { OrbitRings } from './OrbitRings';
import { Particles } from './Particles';
import { PostEffects } from './PostEffects';
import { ProcessNode } from './ProcessNode';
import { Satellites } from './Satellites';
import { SystemStar } from './SystemStar';
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
      layout: new OrbitLayout(),
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
      <SystemStar />
      <OrbitRings />
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

- [ ] **Step 8: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  284 passed (284)`, `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐
- build 성공, `index-*.js` 약 1.48 MB

- [ ] **Step 9: 브라우저 확인은 하지 않는다**

`pulse-engine` 도 dev 서버도 띄우지 않는다. 컨트롤러가 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 10: 커밋**

```
git add src/scene/SystemStar.tsx src/scene/OrbitRings.tsx src/scene/SceneRoot.tsx src/visual/coreRing.ts src/visual/camera.ts tests/visual/coreRing.test.ts
git commit -m "feat(web): draw the system star and orbit lines, widen the core ring and look down on the orbits"
```

---

## 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`(284), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `web/src/visual/orbits.ts` 가 바뀌지 않았다 (`git diff main -- web/src/visual/orbits.ts` 비어 있음).
- [ ] 브라우저 (엔진 + dev 서버 또는 `run.bat`):
  - 가운데 별, 동심원 궤도 몇 개, 큰 천체가 안쪽 궤도에 겹치지 않고 놓임, 바깥에 코어 고리. 이전 스크린샷과 비교.
  - 천체가 천천히 공전한다. 새 프로세스가 바깥에서 들어와 자리를 잡는다.
  - 천체를 클릭하면 카메라가 다가가 공전하는 천체를 따라간다. Esc 로 돌아온다. 툴팁·흐름·위성이 전처럼 동작한다.
  - 콘솔 오류 없음.
