import type { ProcessGroup, Snapshot } from '../protocol/schema';

// 장면이 React 로 다시 그려야 하는 때는 그룹 key 집합이 바뀔 때뿐이다.
// 스토어 selector 가 문자열 배열을 돌려주면 useShallow 가 원소별로 비교해
// 1 Hz 스냅샷마다 재렌더되지 않는다. account 는 색을 정하므로 함께 싣는다.
// account 에는 '|' 가 없으므로 첫 '|' 에서 자르면 key 에 '|' 가 있어도 안전하다.
export interface NodeSpec {
  key: string;
  account: ProcessGroup['account'];
}

const EMPTY: string[] = [];

export function selectNodeIds(state: { current: Snapshot | null }): string[] {
  if (state.current === null) {
    return EMPTY;
  }
  return state.current.groups.map((group) => `${group.account}|${group.key}`);
}

export function parseNodeId(id: string): NodeSpec {
  const bar = id.indexOf('|');
  return {
    account: id.slice(0, bar) as ProcessGroup['account'],
    key: id.slice(bar + 1),
  };
}
