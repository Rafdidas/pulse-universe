import type { Flow } from '../protocol/schema';

// M7 스펙 4절. flow 선마다 밝기(0~1)를 관리한다. 있는 선은 짧게 밝아지고, 사라진
// 선은 잔광으로 천천히 흐려진다 — 엔진의 추정 flow 는 뜨거운 코어 순위가 초마다
// 바뀌어 선이 매초 1/3~1/2 씩 교체된다(스펙 2절). 흐려지는 동안의 값은 마지막으로
// 본 옛 값이며, 밝기가 그것을 드러낸다.

export const FADE_IN_SEC = 0.4;
export const AFTERGLOW_SEC = 2.5;
export const MAX_EDGES = 96;

export interface FlowEdge {
  key: string;
  group: string;
  coreId: number;
  weight: number;
  source: Flow['source'];
  // 0~1.
  intensity: number;
  // 이번 스냅샷의 flows 에 있는가.
  present: boolean;
}

export function edgeKey(group: string, coreId: number): string {
  return `${group}>${coreId}`;
}

// 그리는 세기. 흐려지는 선은 약해진다.
export function edgeStrength(edge: Pick<FlowEdge, 'weight' | 'intensity'>): number {
  return edge.weight * edge.intensity;
}

export class FlowTracker {
  private readonly items = new Map<string, FlowEdge>();

  // flows: 이번 프레임의 스냅샷 flows. liveGroups: 존재 추적기에 있는 그룹 key.
  // dtSec: 프레임 간격(자르지 않는다 — 탭 복귀 뒤에는 잔광이 끝나 있는 것이 맞다).
  update(flows: readonly Flow[], liveGroups: ReadonlySet<string>, dtSec: number): void {
    const dt = Number.isFinite(dtSec) ? Math.max(0, dtSec) : 0;

    for (const edge of this.items.values()) {
      edge.present = false;
    }
    for (const flow of flows) {
      const key = edgeKey(flow.group, flow.core);
      const existing = this.items.get(key);
      if (existing === undefined) {
        this.items.set(key, {
          key,
          group: flow.group,
          coreId: flow.core,
          weight: flow.weight,
          source: flow.source,
          intensity: 0,
          present: true,
        });
      } else {
        existing.weight = flow.weight;
        existing.source = flow.source;
        existing.present = true;
      }
    }

    // Map 은 순회 중 삭제해도 안전하다. 복사하지 않고 그대로 돈다.
    for (const edge of this.items.values()) {
      // 그 천체가 더 이상 그려지지 않는다. 선도 즉시 지운다.
      if (!liveGroups.has(edge.group)) {
        this.items.delete(edge.key);
        continue;
      }
      if (edge.present) {
        edge.intensity = Math.min(1, edge.intensity + dt / FADE_IN_SEC);
      } else {
        edge.intensity = Math.max(0, edge.intensity - dt / AFTERGLOW_SEC);
        if (edge.intensity <= 0) {
          this.items.delete(edge.key);
        }
      }
    }

    // 넘치면 잔광 중인 선부터, 그 안에서 가장 약한 선부터 버린다.
    // (막 생긴 선은 세기가 작아 곧바로 버려지는 일을 막는다.)
    if (this.items.size > MAX_EDGES) {
      const weakest = [...this.items.values()]
        .sort((a, b) => Number(a.present) - Number(b.present) || edgeStrength(a) - edgeStrength(b))
        .slice(0, this.items.size - MAX_EDGES);
      for (const edge of weakest) {
        this.items.delete(edge.key);
      }
    }
  }

  edges(): FlowEdge[] {
    return [...this.items.values()];
  }

  get size(): number {
    return this.items.size;
  }

  reset(): void {
    this.items.clear();
  }
}
