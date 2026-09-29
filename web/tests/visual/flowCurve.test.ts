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
