import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import { AdditiveBlending, Color, SRGBColorSpace, type MeshBasicMaterial } from 'three';

import { STAR_RADIUS, starGain } from '../visual/solar';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 따뜻한 흰색 (sRGB).
const STAR_RGB: [number, number, number] = [1.0, 0.86, 0.62];
const HALO_SCALE = 1.6;
const HALO_OPACITY = 0.18;

// 태양계형 배치 스펙 6절. 궤도의 중심. 크기는 고정이고 밝기는 시스템 전체 CPU 사용률을
// 따른다. 초점이 잡히면 다른 천체처럼 어두워진다. 포인터 이벤트는 받지 않는다.
export function SystemStar() {
  const { cache, focus } = useSceneContext();
  const body = useRef<MeshBasicMaterial>(null);
  const halo = useRef<MeshBasicMaterial>(null);

  useFrame(() => {
    if (body.current === null || halo.current === null) {
      return;
    }
    const dim = 1 - DIM_DEPTH * focus.weight;
    const gain = starGain(cache.snapshot?.system.cpu_pct ?? null) * dim;
    body.current.color.setRGB(STAR_RGB[0], STAR_RGB[1], STAR_RGB[2], SRGBColorSpace).multiplyScalar(gain);
    halo.current.opacity = HALO_OPACITY * dim;
  });

  return (
    <group raycast={() => null}>
      <mesh raycast={() => null}>
        <sphereGeometry args={[STAR_RADIUS, 48, 48]} />
        <meshBasicMaterial ref={body} color={new Color(1, 1, 1)} />
      </mesh>
      <mesh raycast={() => null} scale={HALO_SCALE}>
        <sphereGeometry args={[STAR_RADIUS, 32, 32]} />
        <meshBasicMaterial
          ref={halo}
          color="#ffd9a0"
          transparent
          opacity={HALO_OPACITY}
          depthWrite={false}
          blending={AdditiveBlending}
        />
      </mesh>
    </group>
  );
}
