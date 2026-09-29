import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry, Color, SRGBColorSpace } from 'three';

import { useSnapshotStore } from '../state/snapshotStore';
import { coreColor, coreLoad, orbRadius } from '../visual/coreMapping';
import { corePosition } from '../visual/coreRing';
import {
  SPARKS_PER_CORE,
  activeSparks,
  advanceSparkPhase,
  sparkPosition,
} from '../visual/coreSparks';
import { FRAME_PRIORITY } from './framePriority';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

const SPARK_SIZE = 0.3;
// 코어마다 sRGB → 선형 변환에 쓰는 임시 색. 프레임마다 새로 만들지 않는다.
const scratch = new Color();

// M6 스펙 6절. 모든 코어의 불꽃이 Points 하나를 나눠 쓴다. 버퍼는 코어 수 × 24 로
// 잡고, 켜진 불꽃만 앞에서부터 채운다 (M5 Particles 와 같은 방식).
export function CoreSparks() {
  const { cache, focus } = useSceneContext();
  const count = useSnapshotStore((state) => state.current?.cores.length ?? 0);
  // 코어별 궤도 위상. 누적한다 (advanceSparkPhase 참조).
  const phases = useRef<number[]>([]);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    const capacity = Math.max(1, count * SPARKS_PER_CORE);
    g.setAttribute('position', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, [count]);

  useEffect(() => () => geometry.dispose(), [geometry]);

  useFrame(() => {
    const cores = cache.snapshot?.cores;
    if (cores === undefined) {
      geometry.setDrawRange(0, 0);
      return;
    }
    const positions = geometry.getAttribute('position') as BufferAttribute;
    const colors = geometry.getAttribute('color') as BufferAttribute;
    const dim = 1 - DIM_DEPTH * focus.weight;
    let n = 0;
    for (let c = 0; c < cores.length && c < count; c += 1) {
      const load = coreLoad(cores[c].pct);
      phases.current[c] = advanceSparkPhase(phases.current[c] ?? 0, load, cache.dtSec);
      const center = corePosition(c, count);
      const radius = orbRadius(load);
      const [r, g, b] = coreColor(load);
      // coreColor 는 sRGB 값인데 버텍스 색은 선형으로 읽힌다. 선형으로 바꿔 넣어야
      // Orb 본체와 같은 색으로 보인다.
      scratch.setRGB(r, g, b, SRGBColorSpace);
      const lit = activeSparks(load);
      for (let i = 0; i < lit; i += 1) {
        const p = sparkPosition(c, i, center, radius, phases.current[c]);
        positions.setXYZ(n, p.x, p.y, p.z);
        colors.setXYZ(n, scratch.r * dim, scratch.g * dim, scratch.b * dim);
        n += 1;
      }
    }
    geometry.setDrawRange(0, n);
    positions.needsUpdate = true;
    colors.needsUpdate = true;
  }, FRAME_PRIORITY.particles);

  return (
    // 불꽃은 호버·클릭 대상이 아니다. 경계 구가 바뀌지 않으므로 절두체 선별도 끈다.
    <points geometry={geometry} frustumCulled={false} raycast={() => null}>
      <pointsMaterial
        size={SPARK_SIZE}
        sizeAttenuation
        vertexColors
        transparent
        blending={AdditiveBlending}
        depthWrite={false}
      />
    </points>
  );
}
