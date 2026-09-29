import { describe, expect, it } from 'vitest';

import {
  AMBIENT_INNER,
  AMBIENT_MAX,
  AMBIENT_OUTER,
  ambientCount,
  ambientPoint,
} from '../../src/visual/ambient';
import {
  SPARKS_PER_CORE,
  activeSparks,
  advanceSparkPhase,
  sparkAngularSpeed,
  sparkPosition,
} from '../../src/visual/coreSparks';

const ORIGIN = { x: 0, y: 0, z: 0 };

function distance(a: { x: number; y: number; z: number }, b = ORIGIN): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

describe('coreSparks', () => {
  it('lights sparks in proportion to the load', () => {
    expect(activeSparks(0)).toBe(0);
    expect(activeSparks(0.5)).toBe(12);
    expect(activeSparks(1)).toBe(SPARKS_PER_CORE);
    expect(activeSparks(2)).toBe(SPARKS_PER_CORE);
  });

  it('keeps each spark between 1.3 and 2.2 orb radii from its core', () => {
    const center = { x: 28, y: 0, z: 0 };
    for (let i = 0; i < SPARKS_PER_CORE; i += 1) {
      for (let phase = 0; phase < 6.3; phase += 0.7) {
        const d = distance(sparkPosition(3, i, center, 1.2, phase), center);
        expect(d).toBeGreaterThanOrEqual(1.2 * 1.3 - 1e-9);
        expect(d).toBeLessThanOrEqual(1.2 * 2.2 + 1e-9);
      }
    }
  });

  it('is deterministic and moves with the orbit phase', () => {
    const a = sparkPosition(1, 2, ORIGIN, 1, 0.3);
    expect(sparkPosition(1, 2, ORIGIN, 1, 0.3)).toEqual(a);
    expect(sparkPosition(1, 2, ORIGIN, 1, 0.8)).not.toEqual(a);
  });

  it('differs between cores for the same spark index', () => {
    expect(sparkPosition(1, 0, ORIGIN, 1, 0)).not.toEqual(sparkPosition(2, 0, ORIGIN, 1, 0));
  });

  it('accumulates the orbit phase by speed × dt', () => {
    expect(advanceSparkPhase(0, 0.5, 0.5)).toBeCloseTo(sparkAngularSpeed(0.5) * 0.5, 9);
    expect(advanceSparkPhase(1, 1, -3)).toBe(1);
    const wrapped = advanceSparkPhase(6, 1, 1);
    expect(wrapped).toBeGreaterThanOrEqual(0);
    expect(wrapped).toBeLessThan(2 * Math.PI);
  });

  it('does not jump when the load changes between frames', () => {
    // 각도를 speed × 시각 으로 계산했다면 시각이 1000 초일 때 부하 0.01 변화로
    // 20 rad 이 튄다. 누적 위상은 한 프레임 분량만 움직인다.
    let phase = 2;
    phase = advanceSparkPhase(phase, 0.3, 1 / 60);
    const before = sparkPosition(0, 0, ORIGIN, 1, phase);
    phase = advanceSparkPhase(phase, 0.31, 1 / 60);
    const after = sparkPosition(0, 0, ORIGIN, 1, phase);
    expect(distance(after, before)).toBeLessThan(0.1);
  });
});

describe('ambient', () => {
  it('scales with the service count and stops at the maximum', () => {
    expect(ambientCount(0)).toBe(0);
    expect(ambientCount(83)).toBe(498);
    expect(ambientCount(1000)).toBe(AMBIENT_MAX);
    expect(ambientCount(-3)).toBe(0);
  });

  it('keeps every point inside the 40–90 shell', () => {
    for (let i = 0; i < AMBIENT_MAX; i += 1) {
      const d = distance(ambientPoint(i));
      expect(d).toBeGreaterThanOrEqual(AMBIENT_INNER - 1e-9);
      expect(d).toBeLessThanOrEqual(AMBIENT_OUTER + 1e-9);
    }
  });

  it('gives the same point for the same index', () => {
    expect(ambientPoint(17)).toEqual(ambientPoint(17));
    expect(ambientPoint(17)).not.toEqual(ambientPoint(18));
  });
});
