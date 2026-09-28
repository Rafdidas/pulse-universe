import { describe, expect, it } from 'vitest';

import { hash01 } from '../../src/visual/hash';
import {
  HUE_JITTER,
  HUE_SYSTEM,
  HUE_USER,
  MIN_RADIUS,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../../src/visual/mapping';

describe('hash01', () => {
  it('returns the same value for the same key and salt', () => {
    expect(hash01('whale.exe:1234', 3)).toBe(hash01('whale.exe:1234', 3));
  });

  it('stays in [0, 1)', () => {
    for (let i = 0; i < 2000; i += 1) {
      const value = hash01(`app.exe:${i}`, i % 7);
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThan(1);
    }
  });

  it('gives different values for different salts of the same key', () => {
    expect(hash01('app.exe:100', 11)).not.toBe(hash01('app.exe:100', 12));
  });

  it('spreads keys that differ only in their last digit', () => {
    // FNV-1a 만 쓰면 이런 key 들이 좁은 구간에 뭉친다.
    const values = Array.from({ length: 1000 }, (_, i) => hash01(`app.exe:${1000 + i}`));
    const mean = values.reduce((sum, v) => sum + v, 0) / values.length;
    const buckets = new Array<number>(10).fill(0);
    for (const v of values) {
      buckets[Math.floor(v * 10)] += 1;
    }

    expect(mean).toBeGreaterThan(0.45);
    expect(mean).toBeLessThan(0.55);
    for (const count of buckets) {
      expect(count).toBeGreaterThan(60);
    }
  });
});

describe('radiusFor', () => {
  it('matches the spec table (r = 0.25 · ∛mem)', () => {
    expect(radiusFor(4237)).toBeCloseTo(4.05, 2);
    expect(radiusFor(1221)).toBeCloseTo(2.67, 2);
    expect(radiusFor(92)).toBeCloseTo(1.13, 2);
    expect(radiusFor(45)).toBeCloseTo(0.89, 2);
  });

  it('never goes below the minimum radius', () => {
    expect(radiusFor(0)).toBe(MIN_RADIUS);
    expect(radiusFor(0.5)).toBe(MIN_RADIUS);
    expect(radiusFor(-10)).toBe(MIN_RADIUS);
  });

  it('is absolute: doubling memory doubles volume no matter what else is on screen', () => {
    const ratio = radiusFor(2000) / radiusFor(1000);
    expect(ratio ** 3).toBeCloseTo(2, 6);
  });
});

describe('activityFor', () => {
  it('matches the spec table on a 28-core machine', () => {
    expect(activityFor(0.1, 28)).toBeCloseTo(0.017, 3);
    // 한 코어 꽉 참 = 100 / 28 %.
    expect(activityFor(100 / 28, 28)).toBeCloseTo(0.43, 2);
    expect(activityFor(200 / 28, 28)).toBeCloseTo(0.68, 2);
    expect(activityFor(400 / 28, 28)).toBeCloseTo(1, 6);
  });

  it('saturates at 1', () => {
    expect(activityFor(100, 28)).toBe(1);
  });

  it('keeps null as null instead of turning it into 0', () => {
    expect(activityFor(null, 28)).toBeNull();
  });

  it('keeps a measured 0 as 0', () => {
    expect(activityFor(0, 28)).toBe(0);
  });

  it('treats a zero core count as one core rather than dividing it away', () => {
    expect(activityFor(100, 0)).toBeCloseTo(activityFor(100, 1)!, 6);
  });
});

describe('pulseFor', () => {
  it('breathes slowly at rest', () => {
    expect(pulseFor(0)).toEqual({ freqHz: 0.15, amplitude: 0.02 });
  });

  it('draws null with the resting breath', () => {
    expect(pulseFor(null)).toEqual(pulseFor(0));
  });

  it('beats faster and deeper at full activity', () => {
    const full = pulseFor(1);
    expect(full.freqHz).toBeCloseTo(1.35, 6);
    expect(full.amplitude).toBeCloseTo(0.08, 6);
  });
});

describe('advancePhase', () => {
  it('accumulates 2π·freq·dt', () => {
    expect(advancePhase(0, 0.5, 0.5)).toBeCloseTo(Math.PI / 2, 6);
  });

  it('continues from the current phase when the frequency changes', () => {
    // 주파수가 바뀌어도 위상은 이어진다 — sin(2π·f·t) 방식이라면 튄다.
    const before = advancePhase(1, 0.2, 0.1);
    const after = advancePhase(before, 1.2, 0);
    expect(after).toBe(before);
  });

  it('wraps into [0, 2π)', () => {
    const phase = advancePhase(6, 1, 1);
    expect(phase).toBeGreaterThanOrEqual(0);
    expect(phase).toBeLessThan(2 * Math.PI);
  });

  it('ignores negative time', () => {
    expect(advancePhase(1, 1, -5)).toBe(1);
  });
});

describe('glowFor', () => {
  it('matches the spec at rest and at full activity', () => {
    expect(glowFor(0)).toEqual({ emissiveIntensity: 0.15, haloOpacity: 0.05 });
    const full = glowFor(1);
    expect(full.emissiveIntensity).toBeCloseTo(1.75, 6);
    expect(full.haloOpacity).toBeCloseTo(0.4, 6);
  });

  it('draws null with the resting glow', () => {
    expect(glowFor(null)).toEqual(glowFor(0));
  });
});

describe('colorFor', () => {
  it('keeps user and system hues in their own families', () => {
    for (let i = 0; i < 200; i += 1) {
      const key = `app.exe:${i}`;
      expect(Math.abs(colorFor('user', key).h - HUE_USER)).toBeLessThanOrEqual(HUE_JITTER);
      expect(Math.abs(colorFor('system', key).h - HUE_SYSTEM)).toBeLessThanOrEqual(HUE_JITTER);
    }
  });

  it('gives the same key the same color every time', () => {
    expect(colorFor('user', 'Code.exe:4321')).toEqual(colorFor('user', 'Code.exe:4321'));
  });

  it('varies the hue between keys of the same account', () => {
    const hues = new Set(
      Array.from({ length: 20 }, (_, i) => colorFor('user', `app.exe:${i}`).h.toFixed(3)),
    );
    expect(hues.size).toBeGreaterThan(15);
  });
});
