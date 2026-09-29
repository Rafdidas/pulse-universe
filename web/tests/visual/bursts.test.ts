import { describe, expect, it } from 'vitest';

import {
  BurstPool,
  CHILD_DIED,
  CONVERGE_START,
  GROUP_COLLAPSE,
  GROUP_FORM,
  SCATTER_DISTANCE,
  particleAt,
  type BurstSpec,
} from '../../src/visual/bursts';

const ORIGIN = { x: 0, y: 0, z: 0 };

function spec(overrides: Partial<BurstSpec> = {}): BurstSpec {
  return {
    ...GROUP_FORM,
    anchor: { kind: 'group', key: 'a.exe:1', groupKey: 'a.exe:1' },
    radius: 2,
    color: [1, 1, 1],
    seed: 7,
    ...overrides,
  };
}

function distance(p: { x: number; y: number; z: number }, c = ORIGIN): number {
  return Math.hypot(p.x - c.x, p.y - c.y, p.z - c.z);
}

describe('particleAt', () => {
  it('starts a converge particle far out and brings it to the center', () => {
    const burst = new BurstPool().add(spec(), 10);
    const start = particleAt(burst, 0, 10, ORIGIN)!;
    const almostEnd = particleAt(burst, 0, 10 + burst.durationSec * 0.999, ORIGIN)!;

    // 출발 거리 = 반지름 × 3.5 × 흩뜨림(0.7~1.3).
    expect(distance(start)).toBeGreaterThanOrEqual(2 * CONVERGE_START * 0.7 - 1e-6);
    expect(distance(start)).toBeLessThanOrEqual(2 * CONVERGE_START * 1.3 + 1e-6);
    expect(distance(almostEnd)).toBeLessThan(0.05);
  });

  it('pulls an implode particle from the surface into the center while it fades', () => {
    const burst = new BurstPool().add(spec({ ...CHILD_DIED }), 0);
    const start = particleAt(burst, 3, 0, ORIGIN)!;
    const late = particleAt(burst, 3, burst.durationSec * 0.9, ORIGIN)!;

    expect(distance(start)).toBeLessThanOrEqual(2 * 1.3 + 1e-6);
    expect(distance(late)).toBeLessThan(distance(start));
    expect(start.alpha).toBeCloseTo(1, 6);
    expect(late.alpha).toBeCloseTo(0.1, 6);
  });

  it('throws a scatter particle outward from the surface', () => {
    const burst = new BurstPool().add(spec({ ...GROUP_COLLAPSE }), 0);
    const start = particleAt(burst, 5, 0, ORIGIN)!;
    const late = particleAt(burst, 5, burst.durationSec * 0.9, ORIGIN)!;

    expect(distance(start)).toBeCloseTo(2, 6);
    expect(distance(late)).toBeGreaterThan(distance(start));
    expect(distance(late)).toBeLessThanOrEqual(2 * (1 + SCATTER_DISTANCE * 1.3) + 1e-6);
  });

  it('follows the center it is given', () => {
    const burst = new BurstPool().add(spec(), 0);
    const here = particleAt(burst, 2, 0.3, ORIGIN)!;
    const there = particleAt(burst, 2, 0.3, { x: 10, y: -4, z: 1 })!;
    expect(there.x - here.x).toBeCloseTo(10, 6);
    expect(there.y - here.y).toBeCloseTo(-4, 6);
    expect(there.z - here.z).toBeCloseTo(1, 6);
  });

  it('is inactive before its start and from its end on', () => {
    const burst = new BurstPool().add(spec(), 5);
    expect(particleAt(burst, 0, 4.99, ORIGIN)).toBeNull();
    expect(particleAt(burst, 0, 5 + burst.durationSec, ORIGIN)).toBeNull();
    expect(particleAt(burst, burst.count, 5.1, ORIGIN)).toBeNull();
  });

  it('places the same particles for the same seed', () => {
    const a = new BurstPool().add(spec({ seed: 3 }), 0);
    const b = new BurstPool().add(spec({ seed: 3 }), 0);
    const c = new BurstPool().add(spec({ seed: 4 }), 0);
    expect(particleAt(a, 9, 0.2, ORIGIN)).toEqual(particleAt(b, 9, 0.2, ORIGIN));
    expect(particleAt(a, 9, 0.2, ORIGIN)).not.toEqual(particleAt(c, 9, 0.2, ORIGIN));
  });
});

describe('BurstPool', () => {
  it('drops finished bursts', () => {
    const pool = new BurstPool();
    pool.add(spec({ durationSec: 1 }), 0);
    pool.add(spec({ durationSec: 3 }), 0);
    expect(pool.active(2)).toHaveLength(1);
    expect(pool.used()).toBe(160);
  });

  it('drops the oldest bursts when a new one would overflow the capacity', () => {
    const pool = new BurstPool(400);
    const first = pool.add(spec({ count: 200 }), 0);
    const second = pool.add(spec({ count: 150 }), 0.1);
    const third = pool.add(spec({ count: 100 }), 0.2);

    const ids = pool.active(0.3).map((b) => b.id);
    expect(ids).toEqual([second.id, third.id]);
    expect(ids).not.toContain(first.id);
    expect(pool.used()).toBeLessThanOrEqual(400);
  });

  it('clamps a single burst larger than the whole pool', () => {
    const pool = new BurstPool(100);
    expect(pool.add(spec({ count: 500 }), 0).count).toBe(100);
  });

  it('empties on clear', () => {
    const pool = new BurstPool();
    pool.add(spec(), 0);
    pool.clear();
    expect(pool.active(0)).toEqual([]);
  });
});
