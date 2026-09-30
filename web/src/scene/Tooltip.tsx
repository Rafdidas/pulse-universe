import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import type { Group } from 'three';

import { formatMb, formatPct } from '../dashboard/format';
import { coreLoad, orbRadius } from '../visual/coreMapping';
import { corePosition } from '../visual/coreRing';
import { systemDetail } from '../visual/labels';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { STAR_RADIUS } from '../visual/solar';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext, type Hovered, type SceneContextValue } from './sceneContext';

// 툴팁은 이름표(BodyLabels 의 LABEL_Z [900, 0])보다 위에 그린다.
const TOOLTIP_Z: [number, number] = [16777271, 1000];

interface Label {
  position: Vec3;
  radius: number;
  title: string;
  detail: string;
}

// 숫자는 대시보드와 같은 formatMb·formatPct 로 쓴다 — 두 화면이 일치해야 한다.
function labelFor(target: NonNullable<Hovered>, context: SceneContextValue): Label | null {
  const { cache, layout, presence, satellites } = context;
  if (target.kind === 'system') {
    const system = cache.snapshot?.system;
    if (system === undefined) {
      return null;
    }
    return {
      position: { x: 0, y: 0, z: 0 },
      radius: STAR_RADIUS,
      title: 'System',
      detail: systemDetail(system),
    };
  }
  if (target.kind === 'core') {
    const cores = cache.snapshot?.cores;
    const core = cores?.[target.index];
    if (cores === undefined || core === undefined) {
      return null;
    }
    return {
      position: corePosition(target.index, cores.length),
      radius: orbRadius(coreLoad(core.pct)),
      title: `CPU ${core.id}`,
      detail: `${formatPct(core.pct)}%`,
    };
  }
  if (target.kind === 'satellite') {
    const view = satellites.get(target.pid);
    if (view === undefined) {
      return null;
    }
    return {
      position: view.position,
      radius: view.radius,
      title: `${view.child.name} · pid ${view.child.pid}`,
      detail: `${formatMb(view.child.mem_mb)} MB · CPU ${formatPct(view.child.cpu_pct)}%`,
    };
  }
  const entry = presence.get(target.key);
  const position =
    cache.timeSec === null ? undefined : floatingPosition(layout, target.key, cache.timeSec);
  // 떠나는 중인 천체의 값은 고정된 옛 값이다. 보여 주지 않는다.
  if (
    entry === undefined ||
    position === undefined ||
    entry.phase === 'fading-out' ||
    entry.phase === 'collapsing'
  ) {
    return null;
  }
  const group = entry.value;
  return {
    position,
    radius: radiusFor(group.mem_mb),
    title: group.name,
    detail: `${formatMb(group.mem_mb)} MB · CPU ${formatPct(group.cpu_pct)}%`,
  };
}

interface Props {
  target: NonNullable<Hovered>;
}

// 호버한 천체나 위성 위에 이름·메모리·CPU 를 띄운다. 값은 보간된 프레임
// 값이므로 React 상태를 거치지 않고 DOM 을 직접 바꾼다.
// anchor 를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저
// 돌아야 한 프레임 지연이나 원점 깜빡임이 없다 (framePriority 참조).
export function Tooltip({ target }: Props) {
  const context = useSceneContext();
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
    const label = labelFor(target, context);
    // Html 은 DOM 이라 group.visible 로는 숨겨지지 않는다. DOM 쪽을 직접 숨긴다.
    if (label === null) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(
      label.position.x,
      label.position.y + label.radius * 1.2,
      label.position.z,
    );
    name.current.textContent = label.title;
    detail.current.textContent = label.detail;
  }, FRAME_PRIORITY.tooltip);

  return (
    <group ref={anchor}>
      <Html center zIndexRange={TOOLTIP_Z} style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-tooltip" style={{ display: 'none' }}>
          <div ref={name} className="universe-tooltip-name" />
          <div ref={detail} />
        </div>
      </Html>
    </group>
  );
}
