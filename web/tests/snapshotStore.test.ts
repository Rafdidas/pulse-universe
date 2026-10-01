import { beforeEach, describe, expect, it } from 'vitest';

import { LIFECYCLE_LOG_LIMIT, useSnapshotStore } from '../src/state/snapshotStore';
import { PROTOCOL_VERSION, type Snapshot } from '../src/protocol/schema';
import { emptyNetwork } from './fixtures/network';

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
    network: emptyNetwork,
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

  it('ignores a replay of the current snapshot (same seq, same t)', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(
      makeSnapshot(3, { lifecycle: { spawned: [{ pid: 1, ppid: 0, name: 'a.exe', group: 'a.exe:1' }], terminated: [] } }),
      100,
    );
    const beforeState = useSnapshotStore.getState();
    const beforeCurrent = beforeState.current;
    const beforePrevious = beforeState.previous;
    const beforeLog = beforeState.lifecycleLog;

    // 같은 seq, 같은 t 로 재전송된 스냅샷 — 엔진이 재연결 시 최신 스냅샷을 다시 보낸 것이다.
    store.pushSnapshot(
      makeSnapshot(3, { lifecycle: { spawned: [{ pid: 1, ppid: 0, name: 'a.exe', group: 'a.exe:1' }], terminated: [] } }),
      200,
    );

    const state = useSnapshotStore.getState();
    expect(state.current).toBe(beforeCurrent);
    expect(state.previous).toBe(beforePrevious);
    expect(state.arrivedAt).toBe(100);
    expect(state.lifecycleLog).toEqual(beforeLog);
    expect(state.lifecycleLog).toHaveLength(1);
  });

  it('beginSession on an empty store leaves it empty and records the session', () => {
    useSnapshotStore.getState().beginSession('A');

    const state = useSnapshotStore.getState();
    expect(state.session).toBe('A');
    expect(state.current).toBeNull();
    expect(state.previous).toBeNull();
    expect(state.lifecycleLog).toHaveLength(0);
  });

  it('beginSession with the same session keeps snapshot data intact', () => {
    const store = useSnapshotStore.getState();
    store.beginSession('A');
    store.pushSnapshot(makeSnapshot(1), 100);
    store.pushSnapshot(makeSnapshot(2), 200);
    const beforeCurrent = useSnapshotStore.getState().current;
    const beforePrevious = useSnapshotStore.getState().previous;
    const beforeLog = useSnapshotStore.getState().lifecycleLog;

    store.beginSession('A');

    const state = useSnapshotStore.getState();
    expect(state.current).toBe(beforeCurrent);
    expect(state.previous).toBe(beforePrevious);
    expect(state.lifecycleLog).toBe(beforeLog);
    expect(state.session).toBe('A');
  });

  it('beginSession with a different session clears data but keeps status and intervalMs', () => {
    const store = useSnapshotStore.getState();
    store.beginSession('A');
    store.pushSnapshot(makeSnapshot(1), 100);
    store.pushSnapshot(makeSnapshot(2), 200);
    store.setIntervalMs(500);
    const statusBefore = useSnapshotStore.getState().status;

    store.beginSession('B');

    const state = useSnapshotStore.getState();
    expect(state.session).toBe('B');
    expect(state.previous).toBeNull();
    expect(state.current).toBeNull();
    expect(state.arrivedAt).toBe(0);
    expect(state.lifecycleLog).toHaveLength(0);
    expect(state.intervalMs).toBe(500);
    expect(state.status).toBe(statusBefore);
  });

  it('a new session clears data mixed across an out-of-order-looking seq (5 then restart at 8)', () => {
    const store = useSnapshotStore.getState();
    store.beginSession('A');
    store.pushSnapshot(
      makeSnapshot(3, { lifecycle: { spawned: [], terminated: [1] } }),
      100,
    );
    store.pushSnapshot(
      makeSnapshot(5, { lifecycle: { spawned: [], terminated: [2] } }),
      200,
    );

    // 새 엔진이 이미 8초 돌아간 상태에서 재접속했다 — seq 는 낮아지지 않고
    // 오히려 더 큰 값(8)으로 온다. session 이 바뀌었으므로 이전 세션의
    // previous 와 로그가 섞이면 안 된다.
    store.beginSession('B');
    store.pushSnapshot(
      makeSnapshot(8, { lifecycle: { spawned: [], terminated: [99] } }),
      300,
    );

    const state = useSnapshotStore.getState();
    expect(state.previous).toBeNull();
    expect(state.current?.seq).toBe(8);
    expect(state.lifecycleLog).toHaveLength(1);
    expect(state.lifecycleLog[0].label).toContain('99');
  });

  it('treats a same seq but different t as the normal path, not a replay', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(
      makeSnapshot(5, { t: 1000, lifecycle: { spawned: [], terminated: [1] } }),
      100,
    );

    // seq 는 같지만 t 가 다르면 재전송이 아니다 — 평범한 다음 스냅샷으로 처리한다.
    store.pushSnapshot(
      makeSnapshot(5, { t: 2000, lifecycle: { spawned: [], terminated: [2] } }),
      200,
    );

    const state = useSnapshotStore.getState();
    expect(state.previous?.seq).toBe(5);
    expect(state.previous?.t).toBe(1000);
    expect(state.current?.t).toBe(2000);
    expect(state.arrivedAt).toBe(200);
    expect(state.lifecycleLog).toHaveLength(2);
  });

  it('clearSnapshots wipes snapshot data but keeps status and intervalMs', () => {
    const store = useSnapshotStore.getState();
    store.pushSnapshot(makeSnapshot(1), 100);
    store.pushSnapshot(makeSnapshot(2), 200);
    store.setIntervalMs(500);
    const statusBefore = useSnapshotStore.getState().status;

    useSnapshotStore.getState().clearSnapshots();

    const state = useSnapshotStore.getState();
    expect(state.previous).toBeNull();
    expect(state.current).toBeNull();
    expect(state.arrivedAt).toBe(0);
    expect(state.lifecycleLog).toHaveLength(0);
    expect(state.intervalMs).toBe(500);
    expect(state.status).toBe(statusBefore);
  });
});
