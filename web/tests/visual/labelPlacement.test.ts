import { describe, expect, it } from 'vitest';

import { LABEL_GAP, LABEL_PADDING, placeLabels, type LabelInput } from '../../src/visual/labelPlacement';

const STAR = { sx: 500, sy: 400 };

function input(key: string, sx: number, sy: number, over: Partial<LabelInput> = {}): LabelInput {
  return { key, sx, sy, radiusPx: 20, width: 80, height: 16, wasVisible: false, ...over };
}

describe('placeLabels', () => {
  it('puts the label on the side of the body away from the star', () => {
    const [right] = placeLabels([input('a', 700, 400)], STAR);
    expect(right.visible).toBe(true);
    expect(right.cx).toBeGreaterThan(700);
    expect(right.cy).toBeCloseTo(400, 6);

    const [below] = placeLabels([input('b', 500, 600)], STAR);
    expect(below.cy).toBeGreaterThan(600);
    expect(below.cx).toBeCloseTo(500, 6);

    const [diagonal] = placeLabels([input('c', 300, 250)], STAR);
    expect(diagonal.cx).toBeLessThan(300);
    expect(diagonal.cy).toBeLessThan(250);
  });

  it('keeps the label box clear of the body rim by the gap', () => {
    const [placed] = placeLabels([input('a', 700, 400, { radiusPx: 20 })], STAR);
    // 별 반대쪽이 +x 이므로 상자의 왼쪽 가장자리가 천체 가장자리 + 틈에 닿는다.
    expect(placed.cx - 40).toBeCloseTo(700 + 20 + LABEL_GAP, 6);
  });

  it('falls back to above the body when it sits exactly on the star', () => {
    const [placed] = placeLabels([input('a', STAR.sx, STAR.sy)], STAR);
    expect(placed.cx).toBeCloseTo(STAR.sx, 6);
    expect(placed.cy).toBeLessThan(STAR.sy);
  });

  it('hides the smaller body when two labels overlap', () => {
    const big = input('big', 700, 400, { radiusPx: 30 });
    const small = input('small', 700, 404, { radiusPx: 10 });
    const placed = placeLabels([small, big], STAR);
    expect(placed.map((p) => [p.key, p.visible])).toEqual([
      ['small', false],
      ['big', true],
    ]);
  });

  it('lets a label that is already visible win over a bigger new one', () => {
    const shown = input('shown', 700, 400, { radiusPx: 10, wasVisible: true });
    const fresh = input('fresh', 700, 404, { radiusPx: 30 });
    const placed = placeLabels([fresh, shown], STAR);
    expect(placed.find((p) => p.key === 'shown')!.visible).toBe(true);
    expect(placed.find((p) => p.key === 'fresh')!.visible).toBe(false);
  });

  it('breaks exact ties by key', () => {
    const a = input('a', 700, 400);
    const b = input('b', 700, 400);
    const placed = placeLabels([b, a], STAR);
    expect(placed.find((p) => p.key === 'a')!.visible).toBe(true);
    expect(placed.find((p) => p.key === 'b')!.visible).toBe(false);
  });

  it('treats boxes that only touch within the padding as overlapping, and farther ones as apart', () => {
    // 같은 높이에 나란히 놓인 두 이름표: 중심 간격이 폭 + 패딩 미만이면 겹친다.
    const left = input('l', 800, 400, { radiusPx: 0 });
    const nearer = input('r', 800 + 80 + LABEL_PADDING - 1, 400, { radiusPx: 0, wasVisible: false });
    const clash = placeLabels([left, nearer], { sx: 0, sy: 400 });
    expect(clash.filter((p) => p.visible)).toHaveLength(1);

    const farther = input('r', 800 + 80 + LABEL_PADDING + 1, 400, { radiusPx: 0 });
    const apart = placeLabels([left, farther], { sx: 0, sy: 400 });
    expect(apart.every((p) => p.visible)).toBe(true);
  });

  it('is deterministic and handles empty and single inputs', () => {
    expect(placeLabels([], STAR)).toEqual([]);
    const items = [input('a', 700, 400), input('b', 300, 300), input('c', 520, 700)];
    expect(placeLabels(items, STAR)).toEqual(placeLabels(items, STAR));
    expect(placeLabels([items[0]], STAR)).toHaveLength(1);
  });
});
