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
