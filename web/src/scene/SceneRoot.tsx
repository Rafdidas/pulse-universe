import { useFrame } from '@react-three/fiber';
import { useMemo, useState } from 'react';
import { useShallow } from 'zustand/react/shallow';

import { sample } from '../state/interpolator';
import { useSnapshotStore } from '../state/snapshotStore';
import { createFrameCache, layoutNodesFrom, updateFrameCache } from '../visual/frameCache';
import { LayoutSim } from '../visual/layout';
import { parseNodeId, selectNodeIds } from './nodeList';
import { ProcessNode } from './ProcessNode';
import { SceneContext, type SceneContextValue } from './sceneContext';
import { Tooltip } from './Tooltip';

// 노드들의 useFrame(우선순위 0)보다 먼저 돈다. 양수 우선순위는 R3F 의 자동
// 렌더를 끄므로 음수를 쓴다.
const BEFORE_NODES = -1;

// 스펙 4절. 프레임당 한 번 보간하고 레이아웃을 한 스텝 진행한 뒤, 노드들이
// key 로 읽을 수 있게 FrameCache 에 둔다.
export function SceneRoot() {
  const ids = useSnapshotStore(useShallow(selectNodeIds));
  const [hovered, setHovered] = useState<string | null>(null);

  const context = useMemo<SceneContextValue>(
    () => ({ cache: createFrameCache(), layout: new LayoutSim(), setHovered }),
    [],
  );

  useFrame(() => {
    // 시계는 performance.now() 다 — arrivedAt 이 같은 시계로 찍힌다.
    const now = performance.now();
    const state = useSnapshotStore.getState();
    const frame = sample(
      {
        previous: state.previous,
        current: state.current,
        arrivedAt: state.arrivedAt,
        intervalMs: state.intervalMs,
      },
      now,
    );
    updateFrameCache(context.cache, frame, now);
    context.layout.step(layoutNodesFrom(context.cache), context.cache.dtSec);
  }, BEFORE_NODES);

  return (
    <SceneContext.Provider value={context}>
      {ids.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      {hovered !== null && <Tooltip nodeKey={hovered} />}
    </SceneContext.Provider>
  );
}
