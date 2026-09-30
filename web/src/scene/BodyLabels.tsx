import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef, useState } from 'react';
import type { Group } from 'three';

import { floatingPosition } from '../visual/layout';
import { labelKeys } from '../visual/labels';
import { radiusFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// 초점이 이만큼 이상 잡히면 이름표를 숨긴다. 위성과 패널이 그 역할을 한다.
const HIDE_WEIGHT = 0.5;

// 이름표·별 정보 스펙 4절. 가장 안쪽 궤도의 큰 천체 이름을 항상 보여 준다.
// 대상 목록이 바뀔 때만 React 상태를 갱신한다 — 궤도 안의 자리가 key 순으로 고정되어
// 있어 목록은 그룹이 들고 날 때만 바뀐다.
export function BodyLabels() {
  const { layout } = useSceneContext();
  const [keys, setKeys] = useState<string[]>([]);
  const signature = useRef('');

  useFrame(() => {
    const next = labelKeys(layout.plan());
    const nextSignature = next.join('\u0000');
    if (nextSignature !== signature.current) {
      signature.current = nextSignature;
      setKeys(next);
    }
  }, FRAME_PRIORITY.tooltip);

  return (
    <>
      {keys.map((key) => (
        <BodyLabel key={key} nodeKey={key} />
      ))}
    </>
  );
}

interface Props {
  nodeKey: string;
}

// 앵커를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저 돌아야 한 프레임
// 지연이 없다 (Tooltip 과 같은 이유, framePriority 참조). 글자는 DOM 에 직접 쓴다.
function BodyLabel({ nodeKey }: Props) {
  const { cache, layout, presence, focus, hover } = useSceneContext();
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);

  useFrame(() => {
    if (anchor.current === null || box.current === null) {
      return;
    }
    const entry = presence.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    const hovered = hover.current;
    const hide =
      entry === undefined ||
      position === undefined ||
      entry.phase === 'fading-out' ||
      entry.phase === 'collapsing' ||
      focus.weight > HIDE_WEIGHT ||
      // 호버 중인 천체는 툴팁이 이름을 보여 준다.
      (hovered !== null && hovered.kind === 'group' && hovered.key === nodeKey);
    if (hide) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(
      position.x,
      position.y + radiusFor(entry.value.mem_mb) * 1.25,
      position.z,
    );
    if (box.current.textContent !== entry.value.name) {
      box.current.textContent = entry.value.name;
    }
  }, FRAME_PRIORITY.tooltip);

  return (
    <group ref={anchor}>
      <Html center style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-label" style={{ display: 'none' }} />
      </Html>
    </group>
  );
}
