import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry } from 'three';

import { SETTLE_TAU } from '../visual/layout';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 궤도 하나를 이만큼의 선분으로 그린다.
const SEGMENTS = 128;
// 이보다 많은 궤도는 그리지 않는다 (기본 40 그룹에서 3~4 개).
const MAX_RINGS = 16;
const RING_OPACITY = 0.22;
const RING_COLOR = '#7d93bd';

// 태양계형 배치 스펙 6절. 궤도 계획의 궤도마다 가는 원. 그룹이 들고 나며 궤도 반지름이
// 바뀌면 천체와 같은 속도로 따라가 옮겨 간다. 초점이 잡히면 더 흐려진다.
export function OrbitRings() {
  const { cache, layout, focus } = useSceneContext();
  // 궤도마다 지금 그리고 있는 반지름. 계획의 반지름으로 지수 평활한다.
  const shown = useRef<number[]>([]);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(new Float32Array(MAX_RINGS * SEGMENTS * 2 * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);
  useEffect(() => () => geometry.dispose(), [geometry]);
  const material = useRef<{ opacity: number } | null>(null);

  useFrame(() => {
    const rings = layout.plan().rings.slice(0, MAX_RINGS);
    const radii = shown.current;
    const follow = 1 - Math.exp(-cache.dtSec / SETTLE_TAU);
    radii.length = rings.length;
    rings.forEach((ring, k) => {
      const current = radii[k];
      radii[k] = current === undefined ? ring.radius : current + (ring.radius - current) * follow;
    });

    const positions = geometry.getAttribute('position') as BufferAttribute;
    let vertex = 0;
    for (const r of radii) {
      for (let s = 0; s < SEGMENTS; s += 1) {
        const a0 = (2 * Math.PI * s) / SEGMENTS;
        const a1 = (2 * Math.PI * (s + 1)) / SEGMENTS;
        positions.setXYZ(vertex, r * Math.cos(a0), 0, r * Math.sin(a0));
        positions.setXYZ(vertex + 1, r * Math.cos(a1), 0, r * Math.sin(a1));
        vertex += 2;
      }
    }
    geometry.setDrawRange(0, vertex);
    positions.needsUpdate = true;

    if (material.current !== null) {
      material.current.opacity = RING_OPACITY * (1 - DIM_DEPTH * focus.weight);
    }
  });

  return (
    <lineSegments geometry={geometry} frustumCulled={false} raycast={() => null}>
      <lineBasicMaterial
        ref={material}
        color={RING_COLOR}
        transparent
        opacity={RING_OPACITY}
        blending={AdditiveBlending}
        depthWrite={false}
      />
    </lineSegments>
  );
}
