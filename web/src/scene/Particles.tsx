import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry, type Points } from 'three';

import { POOL_CAPACITY, particleAt, type Burst, type BurstPool } from '../visual/bursts';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext, type SceneContextValue } from './sceneContext';

const PARTICLE_SIZE = 0.35;

// 버스트의 기준점. 기준 천체가 이미 사라졌으면 마지막으로 본 자리.
function centerOf(burst: Burst, context: SceneContextValue): Vec3 | null {
  const { cache, layout, satellites } = context;
  let center: Vec3 | undefined;
  if (burst.anchor.kind === 'group') {
    center =
      cache.timeSec === null ? undefined : floatingPosition(layout, burst.anchor.key, cache.timeSec);
  } else {
    // 위성이 아직 없거나 이미 사라졌으면 그룹 자리에서 재생한다.
    center =
      satellites.get(Number(burst.anchor.key))?.position ??
      (cache.timeSec === null
        ? undefined
        : floatingPosition(layout, burst.anchor.groupKey, cache.timeSec));
  }
  if (center !== undefined) {
    burst.lastCenter = center;
  }
  return burst.lastCenter;
}

interface Props {
  pool: BurstPool;
}

// M5 스펙 7절. 모든 버스트가 Points 하나를 나눠 쓴다. 입자 수만큼 버퍼를
// 미리 잡아 두고 매 프레임 살아 있는 입자만 앞에서부터 채운다.
export function Particles({ pool }: Props) {
  const context = useSceneContext();
  const points = useRef<Points>(null);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(new Float32Array(POOL_CAPACITY * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(POOL_CAPACITY * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  useFrame(() => {
    const { cache } = context;
    if (cache.timeSec === null) {
      return;
    }
    const positions = geometry.getAttribute('position') as BufferAttribute;
    const colors = geometry.getAttribute('color') as BufferAttribute;
    let n = 0;
    for (const burst of pool.active(cache.timeSec)) {
      const center = centerOf(burst, context);
      if (center === null) {
        continue;
      }
      for (let i = 0; i < burst.count && n < POOL_CAPACITY; i += 1) {
        const particle = particleAt(burst, i, cache.timeSec, center);
        if (particle === null) {
          continue;
        }
        positions.setXYZ(n, particle.x, particle.y, particle.z);
        // 가산 블렌딩이므로 알파 대신 색을 어둡게 해 사라지게 한다.
        colors.setXYZ(
          n,
          burst.color[0] * particle.alpha,
          burst.color[1] * particle.alpha,
          burst.color[2] * particle.alpha,
        );
        n += 1;
      }
    }
    geometry.setDrawRange(0, n);
    positions.needsUpdate = true;
    colors.needsUpdate = true;
  }, FRAME_PRIORITY.particles);

  return (
    // 입자는 호버·클릭 대상이 아니다. 경계 구가 바뀌지 않으므로 절두체 선별도 끈다.
    <points ref={points} geometry={geometry} frustumCulled={false} raycast={() => null}>
      <pointsMaterial
        size={PARTICLE_SIZE}
        sizeAttenuation
        vertexColors
        transparent
        blending={AdditiveBlending}
        depthWrite={false}
      />
    </points>
  );
}
