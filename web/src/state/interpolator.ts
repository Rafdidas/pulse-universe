import type {
  Ambient,
  ChildProcess,
  CoreLoad,
  Flow,
  Lifecycle,
  ProcessGroup,
  Snapshot,
} from '../protocol/schema';

export interface InterpolatedCore {
  id: number;
  pct: number;
}

export interface InterpolatedChild {
  pid: number;
  name: string;
  role: string;
  cpu_pct: number | null;
  mem_mb: number;
  threads: number;
}

export interface InterpolatedGroup {
  key: string;
  name: string;
  root_pid: number;
  cpu_pct: number | null;
  mem_mb: number;
  proc_count: number;
  thread_count: number;
  started_at: number;
  account: 'user' | 'system';
  image_path: string;
  children: InterpolatedChild[];
}

export interface InterpolatedSnapshot {
  seq: number;
  t: number;
  system: {
    cpu_pct: number | null;
    mem_used_mb: number;
    mem_total_mb: number;
    process_total: number;
    thread_total: number;
  };
  cores: InterpolatedCore[];
  groups: InterpolatedGroup[];
  flows: Flow[];
  lifecycle: Lifecycle;
  ambient: Ambient;
}

export interface InterpolationInput {
  previous: Snapshot | null;
  current: Snapshot | null;
  // current 가 도착한 시각. performance.now() 와 같은 단조 시계.
  arrivedAt: number;
  intervalMs: number;
}

function lerp(from: number, to: number, alpha: number): number {
  return from + (to - from) * alpha;
}

// null 은 모름, 0 은 측정된 0 이다. 그 사이에는 중간값이 없으므로 보간하지 않는다.
function lerpNullable(
  from: number | null | undefined,
  to: number | null,
  alpha: number,
): number | null {
  if (to === null || from === null || from === undefined) {
    return to;
  }
  return lerp(from, to, alpha);
}

function interpolateChildren(
  previous: ChildProcess[] | undefined,
  current: ChildProcess[],
  alpha: number,
): InterpolatedChild[] {
  // 자식은 짝지어진 그룹 안에서만 pid 로 대응한다. 전체를 훑으면
  // 그룹이 바뀐 프로세스에서 엉뚱하게 대응된다.
  const before = new Map<number, ChildProcess>();
  for (const child of previous ?? []) {
    before.set(child.pid, child);
  }

  return current.map((child) => {
    const prior = before.get(child.pid);
    return {
      pid: child.pid,
      name: child.name,
      role: child.role,
      cpu_pct: lerpNullable(prior?.cpu_pct, child.cpu_pct, alpha),
      mem_mb: prior === undefined ? child.mem_mb : lerp(prior.mem_mb, child.mem_mb, alpha),
      threads: child.threads,
    };
  });
}

function interpolateGroups(
  previous: ProcessGroup[] | undefined,
  current: ProcessGroup[],
  alpha: number,
): InterpolatedGroup[] {
  const before = new Map<string, ProcessGroup>();
  for (const group of previous ?? []) {
    before.set(group.key, group);
  }

  return current.map((group) => {
    const prior = before.get(group.key);
    return {
      key: group.key,
      name: group.name,
      root_pid: group.root_pid,
      // 새로 나타난 그룹은 보간하지 않는다. 0 에서 자라 올라오면
      // 실제보다 작게 보이는 순간이 생긴다.
      cpu_pct: lerpNullable(prior?.cpu_pct, group.cpu_pct, alpha),
      mem_mb: prior === undefined ? group.mem_mb : lerp(prior.mem_mb, group.mem_mb, alpha),
      proc_count: group.proc_count,
      thread_count: group.thread_count,
      started_at: group.started_at,
      account: group.account,
      image_path: group.image_path,
      children: interpolateChildren(prior?.children, group.children, alpha),
    };
  });
}

function interpolateCores(
  previous: CoreLoad[] | undefined,
  current: CoreLoad[],
  alpha: number,
): InterpolatedCore[] {
  const before = new Map<number, CoreLoad>();
  for (const core of previous ?? []) {
    before.set(core.id, core);
  }

  return current.map((core) => {
    const prior = before.get(core.id);
    return {
      id: core.id,
      pct: prior === undefined ? core.pct : lerp(prior.pct, core.pct, alpha),
    };
  });
}

// 프레임 시각을 받아 그 순간의 값을 돌려준다. 아무것도 구독하지 않으며
// React 를 모른다. M3 의 대시보드는 100ms 마다, M4 의 시각화는 useFrame
// 안에서 이 함수를 부른다.
export function sample(input: InterpolationInput, now: number): InterpolatedSnapshot | null {
  const { previous, current, arrivedAt, intervalMs } = input;
  if (current === null) {
    return null;
  }

  // seq 가 건너뛰면 보간을 리셋하고 현재 값으로 즉시 점프한다 — 계약서 4.5 절.
  const continuous = previous !== null && current.seq === previous.seq + 1;
  const alpha =
    continuous && intervalMs > 0
      ? Math.min(Math.max((now - arrivedAt) / intervalMs, 0), 1)
      : 1;

  const prior = continuous ? previous : null;

  return {
    seq: current.seq,
    t: current.t,
    system: {
      cpu_pct: lerpNullable(prior?.system.cpu_pct, current.system.cpu_pct, alpha),
      mem_used_mb:
        prior === null
          ? current.system.mem_used_mb
          : lerp(prior.system.mem_used_mb, current.system.mem_used_mb, alpha),
      mem_total_mb: current.system.mem_total_mb,
      process_total: current.system.process_total,
      thread_total: current.system.thread_total,
    },
    cores: interpolateCores(prior?.cores, current.cores, alpha),
    groups: interpolateGroups(prior?.groups, current.groups, alpha),
    flows: current.flows,
    lifecycle: current.lifecycle,
    ambient: current.ambient,
  };
}
