import { describe, expect, it } from 'vitest';

import { FrameStats, MAX_DPR, dprFor, formatPerf } from '../../src/visual/perf';

describe('dprFor', () => {
  it('maps factor 0 to 1 and factor 1 to the device dpr', () => {
    expect(dprFor(0, 2)).toBe(1);
    expect(dprFor(1, 2)).toBe(2);
    expect(dprFor(1, 1.5)).toBe(1.5);
  });

  it('caps the device dpr at MAX_DPR and never goes below 1', () => {
    expect(dprFor(1, 3)).toBe(MAX_DPR);
    expect(dprFor(1, 0.5)).toBe(1);
    expect(dprFor(-1, 2)).toBe(1);
    expect(dprFor(2, 2)).toBe(2);
  });

  it('rounds down to quarter steps', () => {
    expect(dprFor(0.5, 2)).toBe(1.5);
    expect(dprFor(0.4, 2)).toBe(1.25);
    expect(dprFor(0.2, 2)).toBe(1);
    expect(dprFor(0.9, 1.5)).toBe(1.25);
  });

  it('treats NaN as the lowest resolution', () => {
    expect(dprFor(Number.NaN, 2)).toBe(1);
    expect(dprFor(1, Number.NaN)).toBe(1);
  });
});

describe('FrameStats', () => {
  it('summarises the average, fps and longest frame, then empties the window', () => {
    const stats = new FrameStats();
    stats.add(0.01);
    stats.add(0.02);
    stats.add(0.03);
    expect(stats.elapsedSec).toBeCloseTo(0.06, 9);
    const summary = stats.take();
    expect(summary).not.toBeNull();
    expect(summary!.avgMs).toBeCloseTo(20, 9);
    expect(summary!.fps).toBeCloseTo(50, 9);
    expect(summary!.maxMs).toBeCloseTo(30, 9);
    expect(stats.elapsedSec).toBe(0);
    expect(stats.take()).toBeNull();
  });

  it('ignores negative and non-finite deltas', () => {
    const stats = new FrameStats();
    stats.add(-1);
    stats.add(Number.NaN);
    stats.add(Number.POSITIVE_INFINITY);
    expect(stats.take()).toBeNull();
    stats.add(0.016);
    expect(stats.take()!.maxMs).toBeCloseTo(16, 9);
  });
});

describe('formatPerf', () => {
  it('shows timing, dpr and per-frame render counts on two lines', () => {
    const text = formatPerf({ avgMs: 3.456, fps: 289.4, maxMs: 7.04 }, 1.5, {
      calls: 152,
      triangles: 230252,
    });
    expect(text).toBe('3.5 ms · 289 fps · max 7.0 ms\ndpr 1.5 · 152 calls · 230k tris');
  });

  it('shows dashes before the first summary and small triangle counts as they are', () => {
    expect(formatPerf(null, 1, { calls: 3, triangles: 12 })).toBe(
      '— ms · — fps\ndpr 1 · 3 calls · 12 tris',
    );
  });
});
