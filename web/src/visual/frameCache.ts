import type { InterpolatedGroup, InterpolatedSnapshot } from '../state/interpolator';
import type { LayoutNode } from './layout';
import { radiusFor } from './mapping';

// 스펙 4절. 장면 루트가 프레임마다 한 번 채우고, 노드들은 key 로 읽는다.
// React 상태가 아니라 가변 객체다 — 매 프레임 새로 만들지 않는다.
export interface FrameCache {
  snapshot: InterpolatedSnapshot | null;
  byKey: Map<string, InterpolatedGroup>;
  // activityFor 의 코어 수. 현재 스냅샷의 cores.length, 최소 1.
  coreCount: number;
  // performance.now() 기준 초. 노드의 부유와 툴팁이 같은 시각을 쓰게 한다.
  // 한 번도 갱신되지 않았으면 null.
  timeSec: number | null;
  // 직전 갱신과의 간격(초). 첫 갱신은 0.
  dtSec: number;
}

export function createFrameCache(): FrameCache {
  return { snapshot: null, byKey: new Map(), coreCount: 1, timeSec: null, dtSec: 0 };
}

export function updateFrameCache(
  cache: FrameCache,
  snapshot: InterpolatedSnapshot | null,
  nowMs: number,
): void {
  const timeSec = nowMs / 1000;
  cache.dtSec = cache.timeSec === null ? 0 : Math.max(0, timeSec - cache.timeSec);
  cache.timeSec = timeSec;

  cache.snapshot = snapshot;
  cache.byKey.clear();
  if (snapshot === null) {
    return;
  }
  for (const group of snapshot.groups) {
    cache.byKey.set(group.key, group);
  }
  cache.coreCount = Math.max(1, snapshot.cores.length);
}

// 궤도 배치(OrbitLayout)의 입력. M5 부터는 스냅샷의 그룹이 아니라 존재 추적기의
// 항목(떠나는 중인 그룹 포함)을 넘긴다 — 사라지는 천체도 연출이 끝날 때까지
// 제자리를 지켜야 한다.
export function layoutNodesFrom(
  groups: Iterable<Pick<InterpolatedGroup, 'key' | 'mem_mb'>>,
): LayoutNode[] {
  const nodes: LayoutNode[] = [];
  for (const group of groups) {
    nodes.push({ key: group.key, radius: radiusFor(group.mem_mb) });
  }
  return nodes;
}
