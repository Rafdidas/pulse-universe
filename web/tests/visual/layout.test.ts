import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import {
  FIXED_STEP,
  LayoutSim,
  MAX_DT,
  floatOffset,
  floatingPosition,
  type LayoutNode,
  type Vec3,
} from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처의 40 개 그룹. 반지름 0.89 ~ 4.05.
const nodes: LayoutNode[] = fixture.groups.map((group) => ({
  key: group.key,
  radius: radiusFor(group.mem_mb),
}));

function length(v: Vec3): number {
  return Math.hypot(v.x, v.y, v.z);
}

function positionsOf(sim: LayoutSim, list: readonly LayoutNode[]): Vec3[] {
  return list.map((node) => ({ ...sim.position(node.key)! }));
}

// 첫 호출의 사전 수렴 뒤에 10 초를 더 돌린다.
function settled(list: readonly LayoutNode[]): LayoutSim {
  const sim = new LayoutSim();
  for (let i = 0; i < 600; i += 1) {
    sim.step(list, FIXED_STEP);
  }
  return sim;
}

// 어떤 두 구도 서로를 파고들지 않는다. COLLISION_GAP 은 여유분이다.
function expectApart(sim: LayoutSim, list: readonly LayoutNode[]): void {
  const positions = positionsOf(sim, list);
  for (let i = 0; i < list.length; i += 1) {
    for (let j = i + 1; j < list.length; j += 1) {
      const d = length({
        x: positions[i].x - positions[j].x,
        y: positions[i].y - positions[j].y,
        z: positions[i].z - positions[j].z,
      });
      expect(d).toBeGreaterThan(list[i].radius + list[j].radius);
    }
  }
}

describe('LayoutSim', () => {
  it('keeps every pair of spheres apart once settled', () => {
    expectApart(settled(nodes), nodes);
  });

  it('keeps the centroid near the origin', () => {
    const positions = positionsOf(settled(nodes), nodes);
    const centroid = positions.reduce(
      (sum, p) => ({
        x: sum.x + p.x / positions.length,
        y: sum.y + p.y / positions.length,
        z: sum.z + p.z / positions.length,
      }),
      { x: 0, y: 0, z: 0 },
    );
    expect(length(centroid)).toBeLessThan(2);
  });

  it('pulls the ten largest bodies closer to the center than the ten smallest', () => {
    const sim = settled(nodes);
    const bySize = [...nodes].sort((a, b) => b.radius - a.radius);
    const meanDistance = (list: LayoutNode[]) =>
      list.reduce((sum, node) => sum + length(sim.position(node.key)!), 0) / list.length;

    expect(meanDistance(bySize.slice(0, 10))).toBeLessThan(meanDistance(bySize.slice(-10)));
  });

  it('is already settled after the very first step', () => {
    // 첫 화면이 한 점에서 퍼져 나오지 않는다 — 첫 호출이 SETTLE_STEPS 만큼 미리 돈다.
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expectApart(sim, nodes);
  });

  it('settles to the same shape on the first step regardless of the frame time', () => {
    // 새로고침해도 같은 모양 — 첫 호출은 dt 와 무관하다.
    const a = new LayoutSim();
    const b = new LayoutSim();
    a.step(nodes, 1 / 144);
    b.step(nodes, 1 / 24);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('advances at most MAX_DT per call', () => {
    // 백그라운드 탭에서 돌아온 5 초짜리 프레임을 한 번에 따라잡지 않는다.
    const a = settled(nodes);
    const b = settled(nodes);
    a.step(nodes, 5);
    b.step(nodes, MAX_DT);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('treats a NaN or negative frame time as no time at all', () => {
    const reference = settled(nodes);
    const sim = settled(nodes);
    sim.step(nodes, Number.NaN);
    sim.step(nodes, -1);
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));

    // 잘못된 dt 가 누적기를 오염시키면 이후 정상 프레임에서도 멈춰 버린다.
    for (let i = 0; i < 10; i += 1) {
      reference.step(nodes, FIXED_STEP);
      sim.step(nodes, FIXED_STEP);
    }
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));
  });

  it('produces identical positions for identical inputs', () => {
    expect(positionsOf(settled(nodes), nodes)).toEqual(positionsOf(settled(nodes), nodes));
  });

  it('adds bodies for new keys and forgets keys that disappear', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expect(sim.size).toBe(40);

    const fewer = nodes.slice(0, 30);
    sim.step(fewer, FIXED_STEP);
    expect(sim.size).toBe(30);
    expect(sim.position(nodes[35].key)).toBeUndefined();

    const newcomer: LayoutNode = { key: 'newcomer.exe:9999', radius: 1 };
    sim.step([...fewer, newcomer], FIXED_STEP);
    expect(sim.size).toBe(31);
    expect(sim.position(newcomer.key)).toBeDefined();
  });

  it('stays finite and bounded under a long run of huge frame times', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    for (let i = 0; i < 200; i += 1) {
      sim.step(nodes, 5);
    }

    for (const p of positionsOf(sim, nodes)) {
      expect(Number.isFinite(p.x) && Number.isFinite(p.y) && Number.isFinite(p.z)).toBe(true);
      expect(length(p)).toBeLessThan(40);
    }
  });

  it('keeps drawing a lone body toward the center', () => {
    const lone: LayoutNode[] = [{ key: 'solo.exe:1', radius: 1 }];
    const sim = new LayoutSim();
    sim.step(lone, FIXED_STEP);
    const before = length(sim.position('solo.exe:1')!);
    for (let i = 0; i < 600; i += 1) {
      sim.step(lone, FIXED_STEP);
    }
    expect(length(sim.position('solo.exe:1')!)).toBeLessThan(before);
  });

  it('does nothing with an empty node list', () => {
    const sim = new LayoutSim();
    sim.step([], FIXED_STEP);
    expect(sim.size).toBe(0);
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
  it('adds the float offset to the simulated position', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    const key = nodes[0].key;
    const base = sim.position(key)!;
    const offset = floatOffset(key, 4);

    expect(floatingPosition(sim, key, 4)).toEqual({
      x: base.x + offset.x,
      y: base.y + offset.y,
      z: base.z + offset.z,
    });
  });

  it('returns undefined for an unknown key', () => {
    expect(floatingPosition(new LayoutSim(), 'nope', 0)).toBeUndefined();
  });
});
