import { describe, expect, it } from 'vitest';

import type { ProcessGroup, Snapshot } from '../src/protocol/schema';
import { parseNodeId, selectNodeIds } from '../src/scene/nodeList';

function group(key: string, account: ProcessGroup['account']): ProcessGroup {
  return {
    key,
    name: key.split(':')[0],
    root_pid: 1,
    cpu_pct: 0,
    mem_mb: 100,
    proc_count: 1,
    thread_count: 1,
    started_at: 0,
    account,
    image_path: '',
    children: [],
  };
}

function snapshot(groups: ProcessGroup[]): Snapshot {
  return {
    type: 'snapshot',
    v: 1,
    seq: 1,
    t: 0,
    system: { cpu_pct: 0, mem_used_mb: 0, mem_total_mb: 0, process_total: 0, thread_total: 0 },
    cores: [],
    groups,
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  };
}

describe('selectNodeIds', () => {
  it('returns one id per group in snapshot order', () => {
    const ids = selectNodeIds({
      current: snapshot([group('a.exe:1', 'user'), group('b.exe:2', 'system')]),
    });
    expect(ids).toEqual(['user|a.exe:1', 'system|b.exe:2']);
  });

  it('returns the same empty array every time there is no snapshot', () => {
    expect(selectNodeIds({ current: null })).toBe(selectNodeIds({ current: null }));
    expect(selectNodeIds({ current: null })).toEqual([]);
  });
});

describe('parseNodeId', () => {
  it('splits the account from the key', () => {
    expect(parseNodeId('system|svc.exe:44')).toEqual({ account: 'system', key: 'svc.exe:44' });
  });

  it('keeps a | inside the key', () => {
    // 파싱이 key 에 | 가 없다는 가정에 기대지 않는다.
    expect(parseNodeId('user|odd|name.exe:7')).toEqual({ account: 'user', key: 'odd|name.exe:7' });
  });

  it('round-trips every id selectNodeIds produces', () => {
    const groups = [group('a.exe:1', 'user'), group('b b.exe:2', 'system')];
    const parsed = selectNodeIds({ current: snapshot(groups) }).map(parseNodeId);
    expect(parsed).toEqual(groups.map((g) => ({ key: g.key, account: g.account })));
  });
});
