import { describe, expect, it } from 'vitest';

import { clamp01, easeInCubic, easeOutCubic } from '../../src/visual/easing';
import {
  COLLAPSE_SEC,
  FADE_SEC,
  FORM_SEC,
  PresenceTracker,
  presenceVisual,
  type PresenceInput,
} from '../../src/visual/presence';

const NONE = new Set<string>();

function items(...keys: string[]): PresenceInput<number>[] {
  return keys.map((key, i) => ({ key, value: i }));
}

// 첫 입력(= 이미 있던 것)으로 시작한 추적기.
function started(...keys: string[]): PresenceTracker<number> {
  const tracker = new PresenceTracker<number>();
  tracker.update(items(...keys), NONE, NONE, 0);
  return tracker;
}

describe('easing', () => {
  it('clamps to [0, 1]', () => {
    expect(clamp01(-1)).toBe(0);
    expect(clamp01(2)).toBe(1);
    expect(clamp01(0.3)).toBe(0.3);
  });

  it('pins both ends and bends the middle the right way', () => {
    expect(easeOutCubic(0)).toBe(0);
    expect(easeOutCubic(1)).toBe(1);
    expect(easeInCubic(0)).toBe(0);
    expect(easeInCubic(1)).toBe(1);
    expect(easeOutCubic(0.5)).toBeGreaterThan(0.5);
    expect(easeInCubic(0.5)).toBeLessThan(0.5);
  });
});

describe('PresenceTracker', () => {
  it('treats the first input as already present', () => {
    const tracker = started('a', 'b');
    expect(tracker.entries().map((e) => [e.key, e.phase])).toEqual([
      ['a', 'present'],
      ['b', 'present'],
    ]);
  });

  it('forms a key that was really born', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), new Set(['b']), NONE, 1);
    expect(tracker.get('b')?.phase).toBe('forming');

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FORM_SEC / 2);
    expect(tracker.get('b')?.progress).toBeCloseTo(0.5, 6);

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FORM_SEC);
    expect(tracker.get('b')?.phase).toBe('present');
  });

  it('fades in a key that only climbed into the list', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), NONE, NONE, 1);
    expect(tracker.get('b')?.phase).toBe('fading-in');

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC);
    expect(tracker.get('b')?.phase).toBe('present');
  });

  it('collapses a key that really died, then removes it', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, new Set(['b']), 1);
    expect(tracker.get('b')?.phase).toBe('collapsing');

    tracker.update(items('a'), NONE, NONE, 1 + COLLAPSE_SEC / 2);
    expect(tracker.get('b')?.phase).toBe('collapsing');

    tracker.update(items('a'), NONE, NONE, 1 + COLLAPSE_SEC);
    expect(tracker.get('b')).toBeUndefined();
  });

  it('quietly fades out a key that only dropped out of the list', () => {
    // 계약서 4.6 — 순위에서 빠진 것을 붕괴시키면 살아 있는 프로그램이 터지는 장면이 된다.
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, NONE, 1);
    expect(tracker.get('b')?.phase).toBe('fading-out');

    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC);
    expect(tracker.get('b')).toBeUndefined();
  });

  it('keeps the last value of a leaving key', () => {
    const tracker = new PresenceTracker<number>();
    tracker.update([{ key: 'b', value: 42 }], NONE, NONE, 0);
    tracker.update([], NONE, NONE, 1);
    tracker.update([], NONE, NONE, 1.1);
    expect(tracker.get('b')?.value).toBe(42);
  });

  it('updates the value of a present key every call', () => {
    const tracker = new PresenceTracker<number>();
    tracker.update([{ key: 'a', value: 1 }], NONE, NONE, 0);
    tracker.update([{ key: 'a', value: 2 }], NONE, NONE, 0.1);
    expect(tracker.get('a')?.value).toBe(2);
  });

  it('brightens a returning key from the opacity it had reached', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, NONE, 1);
    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC * 0.25);
    const dimmed = presenceVisual(tracker.get('b')!).opacity;
    expect(dimmed).toBeCloseTo(0.75, 6);

    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC * 0.25);
    const entry = tracker.get('b')!;
    expect(entry.phase).toBe('fading-in');
    expect(presenceVisual(entry).opacity).toBeCloseTo(dimmed, 6);
  });

  it('dims a key that leaves mid fade-in from the opacity it had reached', () => {
    const tracker = started('a');
    tracker.update(items('a', 'b'), NONE, NONE, 1);
    tracker.update(items('a', 'b'), NONE, NONE, 1 + FADE_SEC * 0.4);
    tracker.update(items('a'), NONE, NONE, 1 + FADE_SEC * 0.4);

    const entry = tracker.get('b')!;
    expect(entry.phase).toBe('fading-out');
    expect(presenceVisual(entry).opacity).toBeCloseTo(0.4, 6);
  });

  it('does not restart a collapse on later calls', () => {
    const tracker = started('a', 'b');
    tracker.update(items('a'), NONE, new Set(['b']), 1);
    tracker.update(items('a'), NONE, NONE, 1.5);
    expect(tracker.get('b')?.startSec).toBe(1);
  });

  it('forgets everything on reset and treats the next input as already present', () => {
    const tracker = started('a');
    tracker.reset();
    expect(tracker.entries()).toEqual([]);

    tracker.update(items('x'), new Set(['x']), NONE, 5);
    expect(tracker.get('x')?.phase).toBe('present');
  });
});

describe('presenceVisual', () => {
  it('matches the spec table', () => {
    expect(presenceVisual({ phase: 'present', progress: 1 })).toEqual({ scale: 1, opacity: 1 });
    expect(presenceVisual({ phase: 'forming', progress: 0 })).toEqual({ scale: 0, opacity: 0 });
    expect(presenceVisual({ phase: 'forming', progress: 1 })).toEqual({ scale: 1, opacity: 1 });
    expect(presenceVisual({ phase: 'fading-in', progress: 0.3 })).toEqual({
      scale: 1,
      opacity: 0.3,
    });
    expect(presenceVisual({ phase: 'fading-out', progress: 0.3 }).opacity).toBeCloseTo(0.7, 6);
    expect(presenceVisual({ phase: 'collapsing', progress: 1 })).toEqual({ scale: 0, opacity: 0 });
    expect(presenceVisual({ phase: 'collapsing', progress: 0.5 }).opacity).toBeCloseTo(0.75, 6);
  });
});
