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
  session: string | null;

  pushSnapshot: (snapshot: Snapshot, now: number) => void;
  setStatus: (status: StreamStatus) => void;
  setIntervalMs: (ms: number) => void;
  clearSnapshots: () => void;
  beginSession: (session: string) => void;
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

export const useSnapshotStore = create<SnapshotState>((set, get) => ({
  previous: null,
  current: null,
  arrivedAt: 0,
  intervalMs: 1000,
  status: initialStatus,
  lifecycleLog: [],
  session: null,

  pushSnapshot: (snapshot, now) =>
    set((state) => {
      // 재연결 직후 엔진이 보유 중인 최신 스냅샷을 즉시 재전송하기 때문에,
      // 클라이언트가 이미 가진 것과 같은 스냅샷(seq, t 모두 동일)이 다시
      // 올 수 있다. 완전히 무시해야 로그에 같은 생멸 이벤트가 중복되지 않는다.
      if (
        state.current !== null &&
        state.current.seq === snapshot.seq &&
        state.current.t === snapshot.t
      ) {
        return state;
      }

      // 평범한 다음 스냅샷.
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

  // 버전 불일치 시 계약서 7.2 절에 따라 화면에 남은 마지막 유효 프레임을
  // 지운다. status 와 intervalMs 는 그대로 둔다 — 배너와 무관하다.
  clearSnapshots: () =>
    set({
      previous: null,
      current: null,
      arrivedAt: 0,
      lifecycleLog: [],
    }),

  // hello 의 session 이 바뀌면 엔진이 재시작한 것이다 — 재연결(같은 session)과
  // 달리 이전 세션의 데이터와 섞이면 안 되므로 지운다. session 이 같으면 그냥
  // 재연결이니 아무것도 하지 않는다.
  beginSession: (session) => {
    if (get().session === session) {
      return;
    }
    get().clearSnapshots();
    set({ session });
  },

  reset: () =>
    set({
      previous: null,
      current: null,
      arrivedAt: 0,
      intervalMs: 1000,
      status: initialStatus,
      lifecycleLog: [],
      session: null,
    }),
}));
