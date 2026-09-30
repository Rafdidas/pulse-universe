import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import { AdditiveBlending, Color, SRGBColorSpace, type MeshBasicMaterial } from 'three';

import { STAR_RADIUS, starGain } from '../visual/solar';
import { useFocusStore } from './focusStore';
import { CLICK_SLOP, DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 따뜻한 흰색 (sRGB).
const STAR_RGB: [number, number, number] = [1.0, 0.86, 0.62];
const HALO_SCALE = 1.6;
const HALO_OPACITY = 0.18;

// 태양계형 배치 스펙 6절. 궤도의 중심. 크기는 고정이고 밝기는 시스템 전체 CPU 사용률을
// 따른다. 초점이 잡히면 다른 천체처럼 어두워진다. 호버하면 시스템 요약 툴팁이 뜬다 (이름표·별
// 정보 스펙 3절). 클릭하면 초점을 푼다. 별이 호버를 받아야 별 뒤의 안쪽 궤도 천체가
// 대신 잡히지 않는다.
export function SystemStar() {
  const { cache, focus, setHovered } = useSceneContext();
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
    <group>
      <mesh
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'system' }));
        }}
        onPointerOut={() => setHovered((current) => (current?.kind === 'system' ? null : current))}
        onClick={(event) => {
          // 별은 클릭 대상이 아니다. 별을 눌러도 뒤에 있는 천체로 클릭이 새지 않게 막고, 예전에
          // 빈 곳을 누른 것과 같이 초점을 푼다 (Universe 의 onPointerMissed 와 같은 동작).
          // 드래그 끝의 클릭은 궤도 조작이므로 무시한다.
          if (event.delta > CLICK_SLOP) {
            return;
          }
          event.stopPropagation();
          useFocusStore.getState().clear();
        }}
      >
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
