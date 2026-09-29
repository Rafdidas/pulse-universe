import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Group,
  type Mesh,
  type MeshBasicMaterial,
  type MeshStandardMaterial,
} from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { floatingPosition } from '../visual/layout';
import {
  HALO_SCALE,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../visual/mapping';
import { presenceVisual } from '../visual/presence';
import { useFocusStore } from './focusStore';
import { CLICK_SLOP, DIM_DEPTH } from './interaction';
import { useSceneContext, type FocusFrame } from './sceneContext';

const SALT_PHASE = 2;

// 초점과 무관한 천체일수록 1 보다 작다.
function dimFor(focus: FocusFrame, key: string): number {
  // 초점이 A 에서 B 로 옮겨 가는 동안은 weight 가 1 로 유지되므로, 카메라 전환
  // 진행도 t 로 A 는 어두워지고 B 는 밝아지게 섞는다.
  if (focus.key !== null && focus.previousKey !== null && focus.previousKey !== focus.key) {
    if (key === focus.key) {
      return 1 - DIM_DEPTH * focus.weight * (1 - focus.t);
    }
    if (key === focus.previousKey) {
      return 1 - DIM_DEPTH * focus.weight * focus.t;
    }
    return 1 - DIM_DEPTH * focus.weight;
  }
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === key) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}

interface Props {
  nodeKey: string;
  account: ProcessGroup['account'];
}

// 그룹 하나. 프레임 값은 React 상태를 거치지 않는다 — useFrame 에서 존재
// 추적기의 항목을 key 로 읽어 ref 를 직접 바꾼다. 떠나는 중인 천체는 고정된
// 마지막 값으로 그린다.
export function ProcessNode({ nodeKey, account }: Props) {
  const { cache, layout, presence, focus, setHovered } = useSceneContext();

  const root = useRef<Group>(null);
  const body = useRef<Mesh>(null);
  const bodyMaterial = useRef<MeshStandardMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 40 개가 동시에 숨 쉬지 않도록 초기 위상을 key 로 흩는다.
  const phase = useRef(hash01(nodeKey, SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, nodeKey);
    return new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  }, [account, nodeKey]);

  useFrame(() => {
    if (
      root.current === null ||
      body.current === null ||
      bodyMaterial.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const entry = presence.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    if (entry === undefined || position === undefined) {
      root.current.visible = false;
      return;
    }
    root.current.visible = true;

    const group = entry.value;
    const visual = presenceVisual(entry);
    const dim = dimFor(focus, nodeKey);
    const activity = activityFor(group.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);
    const scale =
      radiusFor(group.mem_mb) * visual.scale * (1 + pulse.amplitude * Math.sin(phase.current));

    root.current.position.set(position.x, position.y, position.z);
    body.current.scale.setScalar(scale);
    halo.current.scale.setScalar(scale * HALO_SCALE);

    const glow = glowFor(activity);
    const opacity = visual.opacity * dim;
    const material = bodyMaterial.current;
    material.emissiveIntensity = glow.emissiveIntensity * dim;
    material.opacity = opacity;
    // 불투명할 때는 transparent 를 끈다. 정렬 비용과 깊이 문제를 피한다.
    const transparent = opacity < 1;
    if (material.transparent !== transparent) {
      material.transparent = transparent;
      material.depthWrite = !transparent;
      material.needsUpdate = true;
    }
    haloMaterial.current.opacity = glow.haloOpacity * opacity;
  });

  return (
    <group ref={root} visible={false}>
      <mesh
        ref={body}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'group', key: nodeKey }));
        }}
        onPointerOut={() =>
          setHovered((current) =>
            current?.kind === 'group' && current.key === nodeKey ? null : current,
          )
        }
        onClick={(event) => {
          // R3F 는 드래그 끝에도 onClick 을 부른다. 궤도 조작을 초점 전환으로
          // 읽지 않도록 움직인 거리를 본다.
          if (event.delta > CLICK_SLOP) {
            return;
          }
          event.stopPropagation();
          const phase = presence.get(nodeKey)?.phase;
          if (phase === 'fading-out' || phase === 'collapsing') {
            return;
          }
          useFocusStore.getState().toggle(nodeKey);
        }}
      >
        <sphereGeometry args={[1, 32, 32]} />
        <meshStandardMaterial
          ref={bodyMaterial}
          color={color}
          emissive={color}
          roughness={0.55}
          metalness={0.1}
        />
      </mesh>
      {/* 헤일로는 호버 대상이 아니다. 광선 검사에서 빼야 뒤쪽 천체를 가리지 않는다. */}
      <mesh ref={halo} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          color={color}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
