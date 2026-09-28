import { describe, expect, it } from 'vitest';

import { sample, type InterpolationInput } from '../src/state/interpolator';
import { PROTOCOL_VERSION, type ProcessGroup, type Snapshot } from '../src/protocol/schema';

function makeGroup(overrides: Partial<ProcessGroup> = {}): ProcessGroup {
  return {
    key: 'app.exe:100',
    name: 'app.exe',
    root_pid: 100,
    cpu_pct: 10,
    mem_mb: 1000,
    proc_count: 2,
    thread_count: 20,
    started_at: 1,
    account: 'user',
    image_path: 'C:/app.exe',
    children: [],
    ...overrides,
  };
}

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
    cores: [
      { id: 0, pct: 10 },
      { id: 1, pct: 20 },
    ],
    groups: [makeGroup()],
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 80, service_mem_mb: 1000 },
    ...overrides,
  };
}

function input(overrides: Partial<InterpolationInput> = {}): InterpolationInput {
  return {
    previous: null,
    current: null,
    arrivedAt: 0,
    intervalMs: 1000,
    ...overrides,
  };
}

describe('interpolator', () => {
  it('returns null when no snapshot has arrived', () => {
    expect(sample(input(), 0)).toBeNull();
  });

  it('uses current values when there is no previous snapshot', () => {
    const current = makeSnapshot(1);

    const frame = sample(input({ current, arrivedAt: 0 }), 500);

    expect(frame?.groups[0].cpu_pct).toBe(10);
    expect(frame?.system.cpu_pct).toBe(20);
  });

  it('shows the previous value at the start of the interval', () => {
    // 계약서 7.1: 직전 스냅샷에서 현재 스냅샷으로 주기에 걸쳐 보간한다.
    const previous = makeSnapshot(1);
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1000);

    expect(frame?.groups[0].cpu_pct).toBe(10);
  });

  it('reaches the current value at the end of the interval', () => {
    const previous = makeSnapshot(1);
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 2000);

    expect(frame?.groups[0].cpu_pct).toBe(30);
  });

  it('interpolates halfway through the interval', () => {
    const previous = makeSnapshot(1);
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30, mem_mb: 2000 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].cpu_pct).toBe(20);
    expect(frame?.groups[0].mem_mb).toBe(1500);
  });

  it('holds at the current value when the next snapshot is late', () => {
    // 없는 데이터를 추정해 만들어내지 않는다.
    const previous = makeSnapshot(1);
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 9999);

    expect(frame?.groups[0].cpu_pct).toBe(30);
  });

  it('jumps immediately when seq is discontinuous', () => {
    // 계약서 4.5: seq 가 건너뛰면 보간을 리셋한다.
    const previous = makeSnapshot(1);
    const current = makeSnapshot(5, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1000);

    expect(frame?.groups[0].cpu_pct).toBe(30);
  });

  it('does not interpolate between null and a number', () => {
    // null 은 모름, 0 은 측정된 0 이다. 그 사이에는 중간값이 없다.
    const previous = makeSnapshot(1, { groups: [makeGroup({ cpu_pct: null })] });
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].cpu_pct).toBe(30);
  });

  it('keeps null when the current value is null', () => {
    const previous = makeSnapshot(1, { groups: [makeGroup({ cpu_pct: 10 })] });
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: null })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].cpu_pct).toBeNull();
  });

  it('starts a newly appeared group at its current value', () => {
    // 0 에서 자라 올라오면 실제보다 작게 보이는 순간이 생긴다.
    const previous = makeSnapshot(1, { groups: [] });
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30, mem_mb: 4000 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1000);

    expect(frame?.groups[0].cpu_pct).toBe(30);
    expect(frame?.groups[0].mem_mb).toBe(4000);
  });

  it('drops a group that is gone from the current snapshot', () => {
    const previous = makeSnapshot(1, { groups: [makeGroup(), makeGroup({ key: 'other:2' })] });
    const current = makeSnapshot(2, { groups: [makeGroup()] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups).toHaveLength(1);
    expect(frame?.groups[0].key).toBe('app.exe:100');
  });

  it('matches children within their group, not globally', () => {
    // pid 는 전역적으로 유일하지만 자식 목록은 그룹에 중첩돼 있다.
    // 전체를 훑어 찾으면 그룹이 바뀐 프로세스에서 엉뚱하게 대응된다.
    const child = { pid: 5, name: 'c.exe', role: 'child', cpu_pct: 0, mem_mb: 100, threads: 1 };
    const previous = makeSnapshot(1, {
      groups: [makeGroup({ key: 'a:1', children: [child] })],
    });
    const current = makeSnapshot(2, {
      groups: [makeGroup({ key: 'b:2', children: [{ ...child, mem_mb: 900 }] })],
    });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].children[0].mem_mb).toBe(900);
  });

  it('interpolates a child inside a matched group', () => {
    const child = { pid: 5, name: 'c.exe', role: 'child', cpu_pct: 0, mem_mb: 100, threads: 1 };
    const previous = makeSnapshot(1, { groups: [makeGroup({ children: [child] })] });
    const current = makeSnapshot(2, {
      groups: [makeGroup({ children: [{ ...child, mem_mb: 300 }] })],
    });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].children[0].mem_mb).toBe(200);
  });

  it('matches cores by id regardless of order', () => {
    const previous = makeSnapshot(1, { cores: [{ id: 1, pct: 20 }, { id: 0, pct: 10 }] });
    const current = makeSnapshot(2, { cores: [{ id: 0, pct: 30 }, { id: 1, pct: 40 }] });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.cores.find((c) => c.id === 0)?.pct).toBe(20);
    expect(frame?.cores.find((c) => c.id === 1)?.pct).toBe(30);
  });

  it('passes discrete fields through unchanged', () => {
    const previous = makeSnapshot(1, { groups: [makeGroup({ proc_count: 2 })] });
    const current = makeSnapshot(2, {
      groups: [makeGroup({ proc_count: 9, thread_count: 99 })],
      flows: [{ group: 'app.exe:100', core: 3, weight: 0.5, source: 'estimated' }],
      lifecycle: { spawned: [], terminated: [4242] },
    });

    const frame = sample(input({ previous, current, arrivedAt: 1000 }), 1500);

    expect(frame?.groups[0].proc_count).toBe(9);
    expect(frame?.groups[0].thread_count).toBe(99);
    expect(frame?.flows[0].core).toBe(3);
    expect(frame?.lifecycle.terminated).toEqual([4242]);
    expect(frame?.seq).toBe(2);
  });

  it('carries an extra wire field through the group, child and top-level snapshot', () => {
    // 스키마가 새 필드를 받아들이면 보간된 프레임도 그것을 그대로 들고 있어야
    // 한다 — 손으로 필드를 나열해 베끼면 여기서 조용히 빠진다.
    const child = { pid: 5, name: 'c.exe', role: 'child', cpu_pct: 0, mem_mb: 100, threads: 1, extra_child_field: 'child-value' };
    const group = { ...makeGroup({ children: [child as never] }), extra_group_field: 'group-value' };
    const current = {
      ...makeSnapshot(1, { groups: [group as never] }),
      extra_snapshot_field: 'snapshot-value',
    } as unknown as Snapshot;

    const frame = sample(input({ current, arrivedAt: 0 }), 0);
    if (frame === null) {
      throw new Error('expected a frame');
    }

    expect((frame as unknown as Record<string, unknown>).extra_snapshot_field).toBe(
      'snapshot-value',
    );
    expect((frame.groups[0] as unknown as Record<string, unknown>).extra_group_field).toBe(
      'group-value',
    );
    expect(
      (frame.groups[0].children[0] as unknown as Record<string, unknown>).extra_child_field,
    ).toBe('child-value');
  });

  it('does not divide by a zero interval', () => {
    const previous = makeSnapshot(1);
    const current = makeSnapshot(2, { groups: [makeGroup({ cpu_pct: 30 })] });

    const frame = sample(input({ previous, current, arrivedAt: 1000, intervalMs: 0 }), 1500);

    expect(frame?.groups[0].cpu_pct).toBe(30);
  });
});
