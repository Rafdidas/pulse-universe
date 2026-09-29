import { describe, expect, it } from 'vitest';

import type { Flow } from '../../src/protocol/schema';
import {
  AFTERGLOW_SEC,
  FADE_IN_SEC,
  FlowTracker,
  MAX_EDGES,
  edgeKey,
  edgeStrength,
} from '../../src/visual/flowTracker';

function flow(group: string, core: number, weight = 0.2): Flow {
  return { group, core, weight, source: 'estimated' };
}

const LIVE = new Set(['a.exe:1', 'b.exe:2']);

function edge(tracker: FlowTracker, group: string, core: number) {
  return tracker.edges().find((e) => e.key === edgeKey(group, core));
}

describe('FlowTracker', () => {
  it('brightens a new edge over FADE_IN_SEC', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, 0);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBe(0);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC / 2);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.5, 9);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBe(1);
  });

  it('lets a vanished edge glow on for AFTERGLOW_SEC, keeping its last weight', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3, 0.3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, AFTERGLOW_SEC / 2);

    const fading = edge(tracker, 'a.exe:1', 3)!;
    expect(fading.present).toBe(false);
    expect(fading.intensity).toBeCloseTo(0.5, 9);
    expect(fading.weight).toBe(0.3);
    expect(edgeStrength(fading)).toBeCloseTo(0.15, 9);

    tracker.update([], LIVE, AFTERGLOW_SEC / 2);
    expect(edge(tracker, 'a.exe:1', 3)).toBeUndefined();
  });

  it('brightens a returning edge from the intensity it had faded to', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, AFTERGLOW_SEC * 0.4);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.6, 9);

    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC * 0.25);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.85, 9);
    expect(edge(tracker, 'a.exe:1', 3)?.present).toBe(true);
  });

  it('updates weight and source of a present edge', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3, 0.1)], LIVE, 0.1);
    tracker.update([{ group: 'a.exe:1', core: 3, weight: 0.4, source: 'measured' }], LIVE, 0.1);
    const e = edge(tracker, 'a.exe:1', 3)!;
    expect(e.weight).toBe(0.4);
    expect(e.source).toBe('measured');
  });

  it('drops edges of groups that are no longer drawn at once', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3), flow('b.exe:2', 4)], LIVE, FADE_IN_SEC);
    tracker.update([flow('a.exe:1', 3)], new Set(['a.exe:1']), 0.01);
    expect(edge(tracker, 'b.exe:2', 4)).toBeUndefined();
    expect(tracker.size).toBe(1);
  });

  it('keeps at most MAX_EDGES, dropping the weakest', () => {
    const tracker = new FlowTracker();
    const groups = new Set<string>();
    const flows: Flow[] = [];
    for (let i = 0; i < MAX_EDGES + 10; i += 1) {
      groups.add(`g${i}.exe:${i}`);
      flows.push(flow(`g${i}.exe:${i}`, 0, 0.01 + i * 0.001));
    }
    tracker.update(flows, groups, FADE_IN_SEC);
    expect(tracker.size).toBe(MAX_EDGES);
    // 가장 약한 10 개(i = 0..9)가 빠졌다.
    expect(edge(tracker, 'g0.exe:0', 0)).toBeUndefined();
    expect(edge(tracker, 'g9.exe:9', 0)).toBeUndefined();
    expect(edge(tracker, 'g10.exe:10', 0)).toBeDefined();
  });

  it('ends the afterglow in one step after a long pause (tab return)', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC);
    tracker.update([], LIVE, 30);
    expect(tracker.size).toBe(0);
  });

  it('treats a NaN or negative dt as no time', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, FADE_IN_SEC / 2);
    tracker.update([flow('a.exe:1', 3)], LIVE, Number.NaN);
    tracker.update([flow('a.exe:1', 3)], LIVE, -1);
    expect(edge(tracker, 'a.exe:1', 3)?.intensity).toBeCloseTo(0.5, 9);
  });

  it('forgets everything on reset', () => {
    const tracker = new FlowTracker();
    tracker.update([flow('a.exe:1', 3)], LIVE, 0.1);
    tracker.reset();
    expect(tracker.size).toBe(0);
  });
});
