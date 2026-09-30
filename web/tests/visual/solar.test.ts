import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import type { LayoutNode } from '../../src/visual/layout';
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
    const order = plan.rings.flatMap((ring) => ring.keys.map((key) => radiusOf.get(key)!));
    for (let i = 1; i < order.length; i += 1) {
      expect(order[i]).toBeLessThanOrEqual(order[i - 1]);
    }
    expect(order).toHaveLength(nodes.length);
  });

  it('breaks radius ties by key so the order never flickers', () => {
    const plan = planOrbits([
      { key: 'b.exe:2', radius: 1 },
      { key: 'a.exe:1', radius: 1 },
      { key: 'c.exe:3', radius: 1 },
    ]);
    expect(plan.rings[0].keys).toEqual(['a.exe:1', 'b.exe:2', 'c.exe:3']);
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
    // 코어 고리(RING_MIN_RADIUS 34)의 Orb 와 닿지 않는다.
    expect(plan.outerRadius).toBeLessThan(32);
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
