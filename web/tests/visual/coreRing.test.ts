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
    expect(first.x).toBeCloseTo(28, 6);
    expect(first.z).toBeCloseTo(0, 6);
    for (let i = 0; i < 28; i += 1) {
      const p = corePosition(i, 28);
      expect(p.y).toBe(0);
      expect(distance(p)).toBeCloseTo(28, 6);
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
