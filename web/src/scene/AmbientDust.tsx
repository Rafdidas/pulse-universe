import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { BufferAttribute, BufferGeometry, type Points } from 'three';

import { AMBIENT_MAX, ambientCount, ambientPoint } from '../visual/ambient';
import { useSceneContext } from './sceneContext';

// 먼지 전체를 y 축으로 이만큼씩 돌린다 (rad/s). 입자별 계산이 없다.
const DUST_ROTATION_SPEED = 0.01;
const DUST_SIZE = 0.25;
const DUST_OPACITY = 0.35;
const DUST_COLOR = '#8fa3c0';

// M6 스펙 7절. svchost 계열 서비스(ambient)를 배경 먼지로 그린다. 위치 800 개를
// 미리 계산해 두고 서비스 수에 맞춰 drawRange 만 바꾼다. 가산 블렌딩을 쓰지 않는다 —
// 배경이 밝아지면 천체가 묻힌다.
export function AmbientDust() {
  const { cache } = useSceneContext();
  const points = useRef<Points>(null);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    const positions = new Float32Array(AMBIENT_MAX * 3);
    for (let i = 0; i < AMBIENT_MAX; i += 1) {
      const p = ambientPoint(i);
      positions[i * 3] = p.x;
      positions[i * 3 + 1] = p.y;
      positions[i * 3 + 2] = p.z;
    }
    g.setAttribute('position', new BufferAttribute(positions, 3));
    return g;
  }, []);

  useEffect(() => () => geometry.dispose(), [geometry]);

  useFrame(() => {
    if (points.current === null) {
      return;
    }
    // 서비스 수는 스냅샷마다 조금씩 바뀐다. 보이는 개수만 맞춘다.
    const services = cache.snapshot?.ambient.service_proc_count ?? 0;
    points.current.geometry.setDrawRange(0, ambientCount(services));
    points.current.rotation.y += DUST_ROTATION_SPEED * cache.dtSec;
  });

  return (
    <points ref={points} geometry={geometry} raycast={() => null}>
      <pointsMaterial
        size={DUST_SIZE}
        sizeAttenuation
        color={DUST_COLOR}
        transparent
        opacity={DUST_OPACITY}
        depthWrite={false}
      />
    </points>
  );
}
