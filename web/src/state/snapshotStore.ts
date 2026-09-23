import { create } from 'zustand';

import type { Snapshot } from '../protocol/schema';
import type { StreamStatus } from '../stream/SystemStream';

// 스토어가 들고 있는 상태의 타입이므로 여기서 다시 내보낸다.
// dashboard/ 는 stream/ 을 직접 참조하지 않는다.
export type { StreamStatus } from '../stream/SystemStream';

export const LIFECYCLE_LOG_LIMIT = 50;

export interface LifecycleEntry {
  kind: 'spawned' | 'terminated';
  label: string;
  seq: number;
}

interface SnapshotState {
  previous: Snapshot | null;
  current: Snapshot | null;
  arrivedAt: number;
  intervalMs: number;
  status: StreamStatus;
  lifecycleLog: LifecycleEntry[];

  pushSnapshot: (snapshot: Snapshot, now: number) => void;
  setStatus: (status: StreamStatus) => void;
  setIntervalMs: (ms: number) => void;
  reset: () => void;
}

const initialStatus: StreamStatus = {
  state: 'closed',
  hello: null,
  invalidCount: 0,
  versionReceived: null,
};

function entriesFor(snapshot: Snapshot): LifecycleEntry[] {
  const spawned = snapshot.lifecycle.spawned.map<LifecycleEntry>((process) => ({
    kind: 'spawned',
    label: `${process.name} (pid ${process.pid})`,
    seq: snapshot.seq,
  }));
  const terminated = snapshot.lifecycle.terminated.map<LifecycleEntry>((pid) => ({
    kind: 'terminated',
    label: `pid ${pid}`,
    seq: snapshot.seq,
  }));
  return [...spawned, ...terminated];
}

export const useSnapshotStore = create<SnapshotState>((set) => ({
  previous: null,
  current: null,
  arrivedAt: 0,
  intervalMs: 1000,
  status: initialStatus,
  lifecycleLog: [],

  pushSnapshot: (snapshot, now) =>
    set((state) => {
      const appended = [...state.lifecycleLog, ...entriesFor(snapshot)];
      return {
        previous: state.current,
        current: snapshot,
        arrivedAt: now,
        lifecycleLog: appended.slice(-LIFECYCLE_LOG_LIMIT),
      };
    }),

  setStatus: (status) => set({ status }),

  // hello 가 알려주는 값이다. 하드코딩하지 않는다.
  setIntervalMs: (ms) => set({ intervalMs: ms }),

  reset: () =>
    set({
      previous: null,
      current: null,
      arrivedAt: 0,
      intervalMs: 1000,
      status: initialStatus,
      lifecycleLog: [],
    }),
}));
