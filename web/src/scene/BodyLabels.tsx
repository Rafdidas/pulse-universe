import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef, useState } from 'react';
import { Vector3, type Group } from 'three';

import { floatingPosition } from '../visual/layout';
import { labelKeys } from '../visual/labels';
import { placeLabels, type LabelInput } from '../visual/labelPlacement';
import { radiusFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// 이름표는 툴팁(TOOLTIP_Z 범위)보다 아래에 그린다. drei 는 카메라 거리로 z-index 를 정하므로
// 범위가 겹치면 앞쪽 이름표가 툴팁을 덮는다.
const LABEL_Z: [number, number] = [900, 0];

// 초점이 이만큼 이상 잡히면 이름표를 숨긴다. 위성과 패널이 그 역할을 한다.
const HIDE_WEIGHT = 0.5;

// 이름표 상자의 크기를 아직 못 쟀을 때 쓰는 값 (px).
const FALLBACK_WIDTH = 80;
const FALLBACK_HEIGHT = 16;

interface Slot {
  anchor: Group | null;
  box: HTMLDivElement | null;
}

// 이름표·별 정보 스펙 4절, 화면 다듬기 스펙 3절. 가장 안쪽 궤도의 큰 천체 이름을 항상 보여 준다.
// 이름표는 별에서 천체로 향하는 화면 방향의 바깥쪽에 놓이고, 겹치면 작은 쪽이 숨는다.
// 대상 목록이 바뀔 때만 React 상태를 갱신한다 — 궤도 안의 자리가 key 순으로 고정되어
// 있어 목록은 그룹이 들고 날 때만 바뀐다. 위치와 표시는 매 프레임 DOM 에 직접 쓴다.
export function BodyLabels() {
  const { cache, layout, presence, focus, hover } = useSceneContext();
  const [keys, setKeys] = useState<string[]>([]);
  const signature = useRef('');
  // 이름표가 자기 앵커·상자를 등록하는 곳. 안정된 Map 하나를 계속 쓴다.
  const [slots] = useState(() => new Map<string, Slot>());
  const sizes = useRef(new Map<string, { width: number; height: number }>());
  const shown = useRef(new Set<string>());
  const scratch = useRef({ world: new Vector3(), view: new Vector3() });

  // 앵커를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저 돌아야 한 프레임
  // 지연이 없다 (Tooltip 과 같은 이유, framePriority 참조).
  useFrame(({ camera, size }) => {
    const next = labelKeys(layout.plan());
    const nextSignature = next.join('\u0000');
    if (nextSignature !== signature.current) {
      signature.current = nextSignature;
      setKeys(next);
    }

    const hovered = hover.current;
    const { world, view } = scratch.current;
    camera.updateMatrixWorld();
    const perspective = 'fov' in camera ? (camera as { fov: number }).fov : null;
    const pixelsPerUnitAtDepth1 =
      perspective === null ? 0 : size.height / 2 / Math.tan((perspective * Math.PI) / 360);

    const inputs: LabelInput[] = [];
    for (const key of next) {
      const slot = slots.get(key);
      if (slot === undefined || slot.anchor === null || slot.box === null) {
        continue;
      }
      const entry = presence.get(key);
      const position =
        cache.timeSec === null ? undefined : floatingPosition(layout, key, cache.timeSec);
      const hide =
        entry === undefined ||
        position === undefined ||
        entry.phase === 'fading-out' ||
        entry.phase === 'collapsing' ||
        focus.weight > HIDE_WEIGHT ||
        // 호버 중인 천체는 툴팁이 이름을 보여 준다.
        (hovered !== null && hovered.kind === 'group' && hovered.key === key);
      if (hide || pixelsPerUnitAtDepth1 === 0) {
        slot.box.style.display = 'none';
        shown.current.delete(key);
        continue;
      }
      slot.anchor.position.set(position.x, position.y, position.z);

      if (slot.box.textContent !== entry.value.name) {
        slot.box.textContent = entry.value.name;
        sizes.current.delete(key);
      }
      let box = sizes.current.get(key);
      if (box === undefined) {
        slot.box.style.display = '';
        const width = slot.box.offsetWidth;
        const height = slot.box.offsetHeight;
        box = { width: width || FALLBACK_WIDTH, height: height || FALLBACK_HEIGHT };
        // 아직 레이아웃되지 않은 0 은 저장하지 않는다 (다음 프레임에 다시 잰다).
        if (width > 0 && height > 0) {
          sizes.current.set(key, box);
        }
      }

      view.set(position.x, position.y, position.z).applyMatrix4(camera.matrixWorldInverse);
      const depth = -view.z;
      if (depth <= 0) {
        slot.box.style.display = 'none';
        shown.current.delete(key);
        continue;
      }
      world.set(position.x, position.y, position.z).project(camera);
      inputs.push({
        key,
        sx: (world.x * 0.5 + 0.5) * size.width,
        sy: (-world.y * 0.5 + 0.5) * size.height,
        radiusPx: (radiusFor(entry.value.mem_mb) * pixelsPerUnitAtDepth1) / depth,
        width: box.width,
        height: box.height,
        wasVisible: shown.current.has(key),
      });
    }

    // 목록에서 빠진 key 의 기억을 지운다. 다시 들어왔을 때 옛 표시 상태가 우선권을 갖지 않게.
    const active = new Set(next);
    for (const key of shown.current) {
      if (!active.has(key)) {
        shown.current.delete(key);
      }
    }
    for (const key of sizes.current.keys()) {
      if (!active.has(key)) {
        sizes.current.delete(key);
      }
    }

    // 별이 카메라 뒤에 있으면 방향을 알 수 없다. 이름표를 이번 프레임만 숨긴다.
    view.set(0, 0, 0).applyMatrix4(camera.matrixWorldInverse);
    if (view.z >= 0) {
      for (const input of inputs) {
        const slot = slots.get(input.key);
        if (slot !== undefined && slot.box !== null) {
          slot.box.style.display = 'none';
        }
        shown.current.delete(input.key);
      }
      return;
    }
    world.set(0, 0, 0).project(camera);
    const star = { sx: (world.x * 0.5 + 0.5) * size.width, sy: (-world.y * 0.5 + 0.5) * size.height };
    const placements = placeLabels(inputs, star);
    placements.forEach((placement, index) => {
      const slot = slots.get(placement.key);
      if (slot === undefined || slot.box === null) {
        return;
      }
      if (!placement.visible) {
        slot.box.style.display = 'none';
        shown.current.delete(placement.key);
        return;
      }
      slot.box.style.display = '';
      slot.box.style.transform = `translate(${(placement.cx - inputs[index].sx).toFixed(1)}px, ${(placement.cy - inputs[index].sy).toFixed(1)}px)`;
      shown.current.add(placement.key);
    });
  }, FRAME_PRIORITY.tooltip);

  return (
    <>
      {keys.map((key) => (
        <BodyLabel key={key} nodeKey={key} slots={slots} />
      ))}
    </>
  );
}

interface Props {
  nodeKey: string;
  slots: Map<string, Slot>;
}

// 앵커와 상자만 들고 있다. 위치·표시·글자는 BodyLabels 의 useFrame 이 정한다.
function BodyLabel({ nodeKey, slots }: Props) {
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);

  // ref 가 채워진 뒤에 등록하고, 사라질 때 지운다. 렌더 중에 ref 를 읽지 않는다.
  function register(slot: Slot | null) {
    if (slot === null) {
      slots.delete(nodeKey);
    } else {
      slots.set(nodeKey, slot);
    }
  }

  return (
    <group
      ref={(group) => {
        anchor.current = group;
        register(group === null ? null : { anchor: group, box: box.current });
      }}
    >
      <Html center zIndexRange={LABEL_Z} style={{ pointerEvents: 'none' }}>
        <div
          ref={(element) => {
            box.current = element;
            const existing = slots.get(nodeKey);
            if (existing !== undefined) {
              existing.box = element;
            } else if (anchor.current !== null) {
              slots.set(nodeKey, { anchor: anchor.current, box: element });
            }
          }}
          className="universe-label"
          style={{ display: 'none' }}
        />
      </Html>
    </group>
  );
}
