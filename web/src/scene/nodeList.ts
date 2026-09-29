import type { ProcessGroup } from '../protocol/schema';

// 이 파일은 노드 id `account|key` 를 만들고 되읽기만 한다. 장면(SceneRoot)은
// PresenceTracker.version 이 바뀔 때만 목록을 다시 만들어 React 상태를 갱신한다.
// account 는 색을 정하므로 id 에 함께 싣는다.
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
