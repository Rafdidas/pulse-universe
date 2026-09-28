import { createContext, useContext } from 'react';

import type { FrameCache } from '../visual/frameCache';
import type { LayoutSim } from '../visual/layout';

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: LayoutSim;
  setHovered: (update: (current: string | null) => string | null) => void;
}

export const SceneContext = createContext<SceneContextValue | null>(null);

export function useSceneContext(): SceneContextValue {
  const value = useContext(SceneContext);
  if (value === null) {
    throw new Error('useSceneContext must be used inside <SceneRoot>');
  }
  return value;
}
