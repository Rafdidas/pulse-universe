import { describe, expect, it } from 'vitest';

import { BOKEH_SCALE, ORB_GAIN_MAX, bokehFor, orbGain } from '../../src/visual/postfx';

describe('bokehFor', () => {
  it('does not blur the overview and blurs fully when focused', () => {
    expect(bokehFor(0)).toBe(0);
    expect(bokehFor(1)).toBe(BOKEH_SCALE);
    expect(bokehFor(0.5)).toBeCloseTo(BOKEH_SCALE / 2, 9);
  });

  it('clamps weights outside 0..1 and treats NaN as no focus', () => {
    expect(bokehFor(-1)).toBe(0);
    expect(bokehFor(2)).toBe(BOKEH_SCALE);
    expect(bokehFor(Number.NaN)).toBe(0);
  });
});

describe('orbGain', () => {
  it('leaves an idle core at 1 and brightens a saturated core to the maximum', () => {
    expect(orbGain(0)).toBe(1);
    expect(orbGain(1)).toBeCloseTo(ORB_GAIN_MAX, 9);
  });

  it('rises slowly at low load and grows with load', () => {
    expect(orbGain(0.2)).toBeLessThan(1.1);
    let previous = orbGain(0);
    for (let load = 0.1; load <= 1.0001; load += 0.1) {
      const gain = orbGain(load);
      expect(gain).toBeGreaterThan(previous);
      previous = gain;
    }
  });

  it('clamps loads outside 0..1 and treats NaN as idle', () => {
    expect(orbGain(-0.5)).toBe(1);
    expect(orbGain(3)).toBeCloseTo(ORB_GAIN_MAX, 9);
    expect(orbGain(Number.NaN)).toBe(1);
  });
});
