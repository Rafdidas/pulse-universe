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
import { useSceneContext } from './sceneContext';

const SALT_PHASE = 2;

interface Props {
  nodeKey: string;
  account: ProcessGroup['account'];
}

// 그룹 하나. 프레임 값은 React 상태를 거치지 않는다 — useFrame 에서
// FrameCache 를 key 로 읽어 ref 를 직접 바꾼다 (스펙 4절).
export function ProcessNode({ nodeKey, account }: Props) {
  const { cache, layout, setHovered } = useSceneContext();

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
    const group = cache.byKey.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    // 첫 프레임 전이나 그룹이 막 사라진 프레임에는 원점에 크기 1 로 뜨지 않게 숨긴다.
    if (group === undefined || position === undefined) {
      root.current.visible = false;
      return;
    }
    root.current.visible = true;

    const activity = activityFor(group.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);
    const scale = radiusFor(group.mem_mb) * (1 + pulse.amplitude * Math.sin(phase.current));

    root.current.position.set(position.x, position.y, position.z);
    body.current.scale.setScalar(scale);
    halo.current.scale.setScalar(scale * HALO_SCALE);

    const glow = glowFor(activity);
    bodyMaterial.current.emissiveIntensity = glow.emissiveIntensity;
    haloMaterial.current.opacity = glow.haloOpacity;
  });

  return (
    <group ref={root} visible={false}>
      <mesh
        ref={body}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => nodeKey);
        }}
        onPointerOut={() => setHovered((current) => (current === nodeKey ? null : current))}
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
