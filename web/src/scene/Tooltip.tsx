import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import type { Group } from 'three';

import { formatMb, formatPct } from '../dashboard/format';
import { floatingPosition } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { useSceneContext } from './sceneContext';

interface Props {
  nodeKey: string;
}

// SceneRoot 의 useFrame(-1) 뒤, drei Html 의 useFrame(0, anchor 를 투영) 보다
// 먼저 돌아야 한다. 같은 프레임 안에서 Tooltip 이 anchor 위치를 옮긴 다음
// Html 이 그 위치를 읽어야 한 프레임 지연이나 원점 깜빡임이 없다.
const BEFORE_HTML_PROJECTION = -0.5;

// 호버한 천체 위에 이름·메모리·CPU 를 띄운다. 값은 1 Hz 가 아니라 보간된
// 프레임 값이므로 React 상태를 거치지 않고 DOM 을 직접 바꾼다.
// 숫자는 대시보드와 같은 formatMb·formatPct 로 쓴다 — 두 화면이 일치해야 한다.
export function Tooltip({ nodeKey }: Props) {
  const { cache, layout } = useSceneContext();
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);
  const name = useRef<HTMLDivElement>(null);
  const detail = useRef<HTMLDivElement>(null);

  useFrame(() => {
    if (
      anchor.current === null ||
      box.current === null ||
      name.current === null ||
      detail.current === null
    ) {
      return;
    }
    const group = cache.byKey.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    // Html 은 DOM 이라 group.visible 로는 숨겨지지 않는다. DOM 쪽을 직접 숨긴다.
    if (group === undefined || position === undefined) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(position.x, position.y + radiusFor(group.mem_mb) * 1.2, position.z);
    name.current.textContent = group.name;
    detail.current.textContent = `${formatMb(group.mem_mb)} MB · CPU ${formatPct(group.cpu_pct)}%`;
  }, BEFORE_HTML_PROJECTION);

  return (
    <group ref={anchor}>
      <Html center style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-tooltip" style={{ display: 'none' }}>
          <div ref={name} className="universe-tooltip-name" />
          <div ref={detail} />
        </div>
      </Html>
    </group>
  );
}
