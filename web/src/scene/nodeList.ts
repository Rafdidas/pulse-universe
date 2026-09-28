import type { ProcessGroup } from '../protocol/schema';

// 장면이 React 로 다시 그려야 하는 때는 그려야 할 key 집합이 바뀔 때뿐이다.
// 존재 추적기의 항목을 문자열 id 배열로 만들어 두고, 이어 붙인 서명이 바뀔
// 때만 React 상태를 갱신한다. account 는 색을 정하므로 함께 싣는다.
// account 에는 '|' 가 없으므로 첫 '|' 에서 자르면 key 에 '|' 가 있어도 안전하다.
export interface NodeSpec {
  key: string;
  account: ProcessGroup['account'];
}

export function nodeIdsOf(
  entries: readonly { key: string; value: Pick<ProcessGroup, 'account'> }[],
): string[] {
  return entries.map((entry) => `${entry.value.account}|${entry.key}`);
}

export function parseNodeId(id: string): NodeSpec {
  const bar = id.indexOf('|');
  return {
    account: id.slice(0, bar) as ProcessGroup['account'],
    key: id.slice(bar + 1),
  };
}
