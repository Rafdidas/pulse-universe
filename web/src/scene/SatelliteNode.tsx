import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { Color, type Mesh, type MeshStandardMaterial } from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { activityFor, advancePhase, colorFor, glowFor, pulseFor } from '../visual/mapping';
import { presenceVisual } from '../visual/presence';
import { useSceneContext } from './sceneContext';

const SALT_PHASE = 2;
// 위성은 부모 색 계열에서 조금 더 밝게 그린다.
const SATELLITE_LIGHTNESS = 0.68;
// 드래그 끝에 버튼을 뗀 것은 클릭이 아니다 (px). ProcessNode 의 CLICK_SLOP 과 동일한 값.
const CLICK_SLOP = 2;

interface Props {
  pid: number;
  account: ProcessGroup['account'];
}

// 위성 하나. 위치와 값은 Satellites 가 프레임마다 채운 SatelliteView 에서 읽는다.
export function SatelliteNode({ pid, account }: Props) {
  const { cache, satellites, setHovered } = useSceneContext();
  const mesh = useRef<Mesh>(null);
  const material = useRef<MeshStandardMaterial>(null);
  const phase = useRef(hash01(String(pid), SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, String(pid));
    return new Color().setHSL(hsl.h / 360, hsl.s, SATELLITE_LIGHTNESS);
  }, [account, pid]);

  useFrame(() => {
    if (mesh.current === null || material.current === null) {
      return;
    }
    const view = satellites.get(pid);
    if (view === undefined) {
      mesh.current.visible = false;
      return;
    }
    mesh.current.visible = true;

    const visual = presenceVisual(view.entry);
    const activity = activityFor(view.child.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);

    mesh.current.position.set(view.position.x, view.position.y, view.position.z);
    mesh.current.scale.setScalar(
      view.radius * visual.scale * (1 + pulse.amplitude * Math.sin(phase.current)),
    );
    material.current.emissiveIntensity = glowFor(activity).emissiveIntensity;
    material.current.opacity = visual.opacity;
  });

  return (
    <mesh
      ref={mesh}
      visible={false}
      onPointerOver={(event) => {
        event.stopPropagation();
        setHovered(() => ({ kind: 'satellite', pid }));
      }}
      onPointerOut={() =>
        setHovered((current) =>
          current?.kind === 'satellite' && current.pid === pid ? null : current,
        )
      }
      onClick={(event) => {
        if (event.delta > CLICK_SLOP) {
          return;
        }
        // 초점이 잡히면 위성이 부모 천체 앞을 지난다. 여기서 막지 않으면 클릭이
        // 뒤쪽 ProcessNode 로 새어 들어가 초점을 꺼버린다.
        event.stopPropagation();
      }}
    >
      <sphereGeometry args={[1, 16, 16]} />
      <meshStandardMaterial
        ref={material}
        color={color}
        emissive={color}
        roughness={0.5}
        transparent
      />
    </mesh>
  );
}
