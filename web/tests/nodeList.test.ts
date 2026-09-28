import { describe, expect, it } from 'vitest';

import type { ProcessGroup } from '../src/protocol/schema';
import { nodeIdsOf, parseNodeId } from '../src/scene/nodeList';

function entry(key: string, account: ProcessGroup['account']) {
  return { key, value: { account } };
}

describe('nodeIdsOf', () => {
  it('returns one id per entry in order', () => {
    expect(nodeIdsOf([entry('a.exe:1', 'user'), entry('b.exe:2', 'system')])).toEqual([
      'user|a.exe:1',
      'system|b.exe:2',
    ]);
  });

  it('returns an empty list for no entries', () => {
    expect(nodeIdsOf([])).toEqual([]);
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

  it('round-trips every id nodeIdsOf produces', () => {
    const entries = [entry('a.exe:1', 'user'), entry('b b.exe:2', 'system')];
    expect(nodeIdsOf(entries).map(parseNodeId)).toEqual(
      entries.map((e) => ({ key: e.key, account: e.value.account })),
    );
  });
});
