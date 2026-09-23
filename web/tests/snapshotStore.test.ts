import { beforeEach, describe, expect, it } from 'vitest';

import { LIFECYCLE_LOG_LIMIT, useSnapshotStore } from '../src/state/snapshotStore';
import { PROTOCOL_VERSION, type Snapshot } from '../src/protocol/schema';

function makeSnapshot(seq: number, overrides: Partial<Snapshot> = {}): Snapshot {
  return {
    type: 'snapshot',
    v: PROTOCOL_VERSION,
    seq,
    t: 1790000000000 + seq * 1000,
    system: {
      cpu_pct: 20,
      mem_used_mb: 8000,
      mem_total_mb: 32000,
      process_total: 400,
      thread_total: 8000,
    },
    cores: [],
    groups: [],
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
    ...overrides,
  };
}

describe('snapshot store', () => {
  beforeEach(() => {
    useSnapshotStore.getState().reset();
  });

  it('starts empty', () => {
    const state = useSnapshotStore.getState();

    expect(state.current).toBeNull();
    expect(state.previous).toBeNull();
    expect(state.lifecycleLog).toHaveLength(0);
  });

  it('keeps the first snapshot as current with no previous', () => {
    useSnapshotStore.getState().pushSnapshot(makeSnapshot(1), 100);

    const state = useSnapshotStore.getState();
    expect(state.current?.seq).toBe(1);
    expect(state.previous).toBeNull();
    expect(state.arrivedAt).toBe(100);
  });

  it('shifts current into previous on the next snapshot', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(makeSnapshot(1), 100);
    store.pushSnapshot(makeSnapshot(2), 1100);

    const state = useSnapshotStore.getState();
    expect(state.previous?.seq).toBe(1);
    expect(state.current?.seq).toBe(2);
    expect(state.arrivedAt).toBe(1100);
  });

  it('accumulates lifecycle events in arrival order', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(
      makeSnapshot(1, {
        lifecycle: {
          spawned: [{ pid: 10, ppid: 1, name: 'new.exe', group: 'new.exe:10' }],
          terminated: [],
        },
      }),
      100,
    );
    store.pushSnapshot(
      makeSnapshot(2, { lifecycle: { spawned: [], terminated: [10] } }),
      1100,
    );

    const log = useSnapshotStore.getState().lifecycleLog;
    expect(log).toHaveLength(2);
    expect(log[0].kind).toBe('spawned');
    expect(log[0].label).toContain('new.exe');
    expect(log[1].kind).toBe('terminated');
    expect(log[1].label).toContain('10');
  });

  it('caps the lifecycle log', () => {
    const store = useSnapshotStore.getState();
    for (let i = 1; i <= LIFECYCLE_LOG_LIMIT + 20; i += 1) {
      store.pushSnapshot(
        makeSnapshot(i, { lifecycle: { spawned: [], terminated: [i] } }),
        i * 1000,
      );
    }

    const log = useSnapshotStore.getState().lifecycleLog;
    expect(log).toHaveLength(LIFECYCLE_LOG_LIMIT);
    // 최근 것이 남는다.
    expect(log[log.length - 1].label).toContain(String(LIFECYCLE_LOG_LIMIT + 20));
  });

  it('takes the sampling interval from hello', () => {
    // interval_ms 는 보간기의 분모다. 하드코딩하지 않는다.
    useSnapshotStore.getState().setIntervalMs(250);

    expect(useSnapshotStore.getState().intervalMs).toBe(250);
  });

  it('clears everything on reset', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(makeSnapshot(1), 100);
    store.reset();

    const state = useSnapshotStore.getState();
    expect(state.current).toBeNull();
    expect(state.lifecycleLog).toHaveLength(0);
  });
});
