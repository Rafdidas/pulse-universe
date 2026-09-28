import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import { sample } from '../../src/state/interpolator';
import {
  createFrameCache,
  layoutNodesFrom,
  updateFrameCache,
} from '../../src/visual/frameCache';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처를 보간기에 한 번 통과시킨 프레임.
const frame = sample({ previous: null, current: fixture, arrivedAt: 0, intervalMs: 1000 }, 0)!;

describe('frameCache', () => {
  it('looks up every group of the real fixture by key', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);

    expect(cache.byKey.size).toBe(fixture.groups.length);
    for (const group of fixture.groups) {
      expect(cache.byKey.get(group.key)?.name).toBe(group.name);
    }
  });

  it('takes the core count from the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    expect(cache.coreCount).toBe(fixture.cores.length);
  });

  it('never reports fewer than one core', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, { ...frame, cores: [] }, 1000);
    expect(cache.coreCount).toBe(1);
  });

  it('keeps a null cpu_pct as null', () => {
    const cache = createFrameCache();
    const withNull = {
      ...frame,
      groups: frame.groups.map((g, i) => (i === 0 ? { ...g, cpu_pct: null } : g)),
    };
    updateFrameCache(cache, withNull, 1000);
    expect(cache.byKey.get(frame.groups[0].key)?.cpu_pct).toBeNull();
  });

  it('empties the lookup when the snapshot goes away', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, null, 1016);

    expect(cache.snapshot).toBeNull();
    expect(cache.byKey.size).toBe(0);
  });

  it('drops groups that are no longer in the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, { ...frame, groups: frame.groups.slice(0, 5) }, 1016);
    expect(cache.byKey.size).toBe(5);
  });

  it('reuses the same Map instead of allocating a new one each frame', () => {
    const cache = createFrameCache();
    const map = cache.byKey;
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, frame, 1016);
    expect(cache.byKey).toBe(map);
  });

  it('reports seconds and the gap since the previous update', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    expect(cache.timeSec).toBe(2);
    expect(cache.dtSec).toBe(0);

    updateFrameCache(cache, frame, 2016);
    expect(cache.timeSec).toBeCloseTo(2.016, 9);
    expect(cache.dtSec).toBeCloseTo(0.016, 9);
  });

  it('never reports a negative gap', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    updateFrameCache(cache, frame, 1000);
    expect(cache.dtSec).toBe(0);
  });

  it('turns groups into layout nodes with their radius', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    const nodes = layoutNodesFrom(cache);

    expect(nodes).toHaveLength(fixture.groups.length);
    expect(nodes[0]).toEqual({
      key: fixture.groups[0].key,
      radius: radiusFor(fixture.groups[0].mem_mb),
    });
  });
});
