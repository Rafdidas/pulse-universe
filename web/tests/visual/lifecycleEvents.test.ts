import { describe, expect, it } from 'vitest';

import type { ProcessGroup } from '../../src/protocol/schema';
import { LifecycleConsumer } from '../../src/visual/lifecycleEvents';

function group(name: string, rootPid: number, childPids: number[] = []): ProcessGroup {
  return {
    key: `${name}:${rootPid}`,
    name,
    root_pid: rootPid,
    cpu_pct: 0,
    mem_mb: 100,
    proc_count: 1 + childPids.length,
    thread_count: 1,
    started_at: 0,
    account: 'user',
    image_path: '',
    children: childPids.map((pid) => ({
      pid,
      name: `${name}-child`,
      role: 'child',
      cpu_pct: 0,
      mem_mb: 10,
      threads: 1,
    })),
  };
}

function snap(
  seq: number,
  groups: ProcessGroup[],
  spawned: number[] = [],
  terminated: number[] = [],
) {
  return {
    seq,
    groups,
    lifecycle: {
      spawned: spawned.map((pid) => ({ pid, ppid: 1, name: 'p.exe', group: '' })),
      terminated,
    },
  };
}

// 첫 스냅샷(기준선)을 이미 소비한 소비기.
function primed(groups: ProcessGroup[]): LifecycleConsumer {
  const consumer = new LifecycleConsumer();
  consumer.consume(snap(1, groups));
  return consumer;
}

describe('LifecycleConsumer', () => {
  it('treats the first snapshot as a baseline', () => {
    const consumer = new LifecycleConsumer();
    expect(consumer.consume(snap(1, [group('a.exe', 10, [11])], [11], [99]))).toEqual([]);
  });

  it('turns a spawned root of a visible group into group-born', () => {
    const consumer = primed([group('a.exe', 10)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10), group('b.exe', 20)], [20]));
    expect(events).toEqual([{ kind: 'group-born', key: 'b.exe:20', pid: 20 }]);
  });

  it('turns a spawned child of a visible group into child-born', () => {
    const consumer = primed([group('a.exe', 10)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10, [11])], [11]));
    expect(events).toEqual([{ kind: 'child-born', key: 'a.exe:10', pid: 11 }]);
  });

  it('maps a terminated root through the previous snapshot to group-died', () => {
    // 종료된 그룹은 현재 스냅샷에 없다 — 직전 스냅샷에서 찾아야 한다.
    const consumer = primed([group('a.exe', 10), group('b.exe', 20)]);
    const events = consumer.consume(snap(2, [group('a.exe', 10)], [], [20]));
    expect(events).toEqual([{ kind: 'group-died', key: 'b.exe:20', pid: 20 }]);
  });

  it('maps a terminated child through the previous snapshot to child-died', () => {
    const consumer = primed([group('a.exe', 10, [11, 12])]);
    const events = consumer.consume(snap(2, [group('a.exe', 10, [12])], [], [11]));
    expect(events).toEqual([{ kind: 'child-died', key: 'a.exe:10', pid: 11 }]);
  });

  it('ignores processes outside the visible groups', () => {
    const consumer = primed([group('a.exe', 10)]);
    expect(consumer.consume(snap(2, [group('a.exe', 10)], [500], [600]))).toEqual([]);
  });

  it('yields each snapshot’s events only once', () => {
    const consumer = primed([group('a.exe', 10)]);
    const next = snap(2, [group('a.exe', 10, [11])], [11]);
    expect(consumer.consume(next)).toHaveLength(1);
    expect(consumer.consume(next)).toEqual([]);
    expect(consumer.consume(next)).toEqual([]);
  });

  it('starts over after a null snapshot', () => {
    const consumer = primed([group('a.exe', 10, [11])]);
    expect(consumer.consume(null)).toEqual([]);
    // 다음 스냅샷은 다시 기준선이다. 직전 스냅샷의 자식 11 을 기억하지 않는다.
    expect(consumer.consume(snap(7, [group('a.exe', 10)], [], [11]))).toEqual([]);
  });

  it('starts over after reset', () => {
    const consumer = primed([group('a.exe', 10, [11])]);
    consumer.reset();
    expect(consumer.consume(snap(2, [group('a.exe', 10)], [], [11]))).toEqual([]);
  });
});
