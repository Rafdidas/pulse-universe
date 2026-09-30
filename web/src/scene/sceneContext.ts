import { createContext, useContext } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import type { InterpolatedGroup } from '../state/interpolator';
import type { FrameCache } from '../visual/frameCache';
import type { OrbitLayout, Vec3 } from '../visual/layout';
import type { LifecycleEvent } from '../visual/lifecycleEvents';
import type { PresenceEntry, PresenceTracker } from '../visual/presence';

export type Hovered =
  | { kind: 'group'; key: string }
  | { kind: 'satellite'; pid: number }
  | { kind: 'core'; index: number }
  | null;

// CameraRig 가 매 프레임 채운다. 노드는 이것으로 어두워지고, 위성은 펼쳐진다.
// 장면 곳곳이 같은 인스턴스를 읽으므로 값은 메서드로만 바꾼다.
export class FocusFrame {
  // 지금 초점 그룹. 없으면 null.
  key: string | null = null;
  // 직전 초점 그룹. 초점을 푸는 동안 위성이 이 그룹으로 접혀 들어간다.
  previousKey: string | null = null;
  // 카메라 전환 진행도 0~1.
  t = 1;
  // 초점 강도 0~1. 초점이 잡힐수록 1, 풀릴수록 0.
  weight = 0;

  begin(key: string | null): void {
    this.previousKey = this.key;
    this.key = key;
  }

  sync(t: number, weight: number): void {
    this.t = t;
    this.weight = weight;
  }
}

// 이번 프레임에 소비한 lifecycle 이벤트. 대부분의 프레임에서 비어 있다.
export class FrameEvents {
  list: readonly LifecycleEvent[] = [];

  replace(events: readonly LifecycleEvent[]): void {
    this.list = events;
  }
}

// 지금 호버 중인 대상. SceneRoot 의 React 상태를 프레임 루프가 읽을 수 있게 비춘다
// (M7 flow 강조). 값은 메서드로만 바꾼다.
export class HoverFrame {
  current: Hovered = null;

  set(hovered: Hovered): void {
    this.current = hovered;
  }
}

// Satellites 가 매 프레임 채운다. 툴팁과 입자가 위성 위치를 읽는다.
export interface SatelliteView {
  position: Vec3;
  radius: number;
  child: ChildProcess;
  entry: PresenceEntry<ChildProcess>;
  account: ProcessGroup['account'];
}

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: OrbitLayout;
  presence: PresenceTracker<InterpolatedGroup>;
  events: FrameEvents;
  focus: FocusFrame;
  hover: HoverFrame;
  satellites: Map<number, SatelliteView>;
  setHovered: (update: (current: Hovered) => Hovered) => void;
}

export const SceneContext = createContext<SceneContextValue | null>(null);

export function useSceneContext(): SceneContextValue {
  const value = useContext(SceneContext);
  if (value === null) {
    throw new Error('useSceneContext must be used inside <SceneRoot>');
  }
  return value;
}
