import { describe, expect, it } from 'vitest';

import { DIM_DEPTH, dimFor } from '../src/scene/interaction';

function frame(key: string | null, previousKey: string | null, t: number, weight: number) {
  return { key, previousKey, t, weight };
}

describe('dimFor', () => {
  it('leaves everything bright without a focus', () => {
    expect(dimFor(frame(null, null, 1, 0), 'a.exe:1')).toBe(1);
  });

  it('keeps the focused group bright and dims unrelated groups by the focus weight', () => {
    const focus = frame('a.exe:1', null, 1, 0.5);
    expect(dimFor(focus, 'a.exe:1')).toBe(1);
    expect(dimFor(focus, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);
  });

  it('keeps the previous group bright while the focus is released', () => {
    const focus = frame(null, 'a.exe:1', 1, 0.4);
    expect(dimFor(focus, 'a.exe:1')).toBe(1);
    expect(dimFor(focus, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.4, 9);
  });

  it('cross-fades from A to B with the camera transition while moving the focus', () => {
    const start = frame('b.exe:2', 'a.exe:1', 0, 1);
    expect(dimFor(start, 'a.exe:1')).toBe(1);
    expect(dimFor(start, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH, 9);

    const middle = frame('b.exe:2', 'a.exe:1', 0.5, 1);
    expect(dimFor(middle, 'a.exe:1')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);
    expect(dimFor(middle, 'b.exe:2')).toBeCloseTo(1 - DIM_DEPTH * 0.5, 9);

    const end = frame('b.exe:2', 'a.exe:1', 1, 1);
    expect(dimFor(end, 'a.exe:1')).toBeCloseTo(1 - DIM_DEPTH, 9);
    expect(dimFor(end, 'b.exe:2')).toBe(1);
    expect(dimFor(end, 'c.exe:3')).toBeCloseTo(1 - DIM_DEPTH, 9);
  });
});
