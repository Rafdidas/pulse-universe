import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Mesh,
  type MeshBasicMaterial,
  SRGBColorSpace,
  type ShaderMaterial,
} from 'three';

import {
  CORE_HALO_SCALE,
  NOISE_PERIOD,
  advanceNoiseOffset,
  coreColor,
  coreHaloOpacity,
  coreLoad,
  distortion,
  orbRadius,
  rimIntensity,
} from '../visual/coreMapping';
import { corePosition } from '../visual/coreRing';
import { orbGain } from '../visual/postfx';
import { coreFragmentShader, coreVertexShader } from './coreShader';
import { CLICK_SLOP, DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

interface Props {
  index: number;
  count: number;
}

// M6 스펙 5절. 코어 하나. 고리 위 고정 자리에서 부하에 따라 커지고, 달아오르고,
// 일그러진다. 프레임 값은 useFrame 에서 uniform·scale 로만 바뀐다.
export function CoreOrb({ index, count }: Props) {
  const { cache, focus, setHovered } = useSceneContext();
  const body = useRef<Mesh>(null);
  const material = useRef<ShaderMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 초기 오프셋도 노이즈 주기 안에 둔다 (advanceNoiseOffset 참조).
  const noiseOffset = useRef((index * 7.3) % NOISE_PERIOD);

  const position = useMemo(() => corePosition(index, count), [index, count]);
  // 재질마다 제 uniform 객체를 가진다. 같은 셰이더 프로그램을 28 개가 나눠 쓴다.
  const uniforms = useMemo(
    () => ({
      uOffset: { value: 0 },
      uAmplitude: { value: 0 },
      uColor: { value: new Color() },
      uRim: { value: 0 },
      uOpacity: { value: 1 },
    }),
    [],
  );

  // R3F 는 언마운트된 메시를 onPointerOut 없이 호버 목록에서 뺀다. 코어 수가 줄거나
  // 스토어가 비면 호버가 남아 툴팁이 엉뚱한 곳에서 다시 뜨므로 정리 때 직접 푼다.
  useEffect(
    () => () =>
      setHovered((current) =>
        current?.kind === 'core' && current.index === index ? null : current,
      ),
    [index, setHovered],
  );

  useFrame(() => {
    if (
      body.current === null ||
      material.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const core = cache.snapshot?.cores[index];
    if (core === undefined) {
      body.current.visible = false;
      halo.current.visible = false;
      return;
    }
    body.current.visible = true;
    halo.current.visible = true;

    const load = coreLoad(core.pct);
    const radius = orbRadius(load);
    // 초점과 무관하므로 초점이 잡히면 다른 천체처럼 어두워진다.
    const dim = 1 - DIM_DEPTH * focus.weight;
    noiseOffset.current = advanceNoiseOffset(noiseOffset.current, load, cache.dtSec);

    body.current.scale.setScalar(radius);
    halo.current.scale.setScalar(radius * CORE_HALO_SCALE);

    const u = material.current.uniforms;
    const [r, g, b] = coreColor(load);
    u.uOffset.value = noiseOffset.current;
    u.uAmplitude.value = distortion(load);
    // 장면은 선형 HDR 버퍼에 그려진다. sRGB 값을 선형으로 바꾸고, 뜨거운 코어만
    // Bloom 임계값을 넘도록 부하에 비례해 밝힌다 (M8 D47).
    (u.uColor.value as Color).setRGB(r, g, b, SRGBColorSpace).multiplyScalar(orbGain(load));
    u.uRim.value = rimIntensity(load) * dim;
    u.uOpacity.value = dim;
    // 어둡지 않을 때는 불투명 패스에 둔다. 불투명 패스가 깊이를 먼저 써야 불꽃·먼지가
    // 깊이 검사로 Orb 뒤에서만 가려진다 (투명 패스에서는 정렬 순서에 따라 Orb 가 덮어쓴다).
    const transparent = dim < 1;
    if (material.current.transparent !== transparent) {
      material.current.transparent = transparent;
      material.current.depthWrite = !transparent;
      material.current.needsUpdate = true;
    }

    // coreColor 는 sRGB 값이다. 본체와 같이 선형으로 바꿔 넣는다.
    haloMaterial.current.color.setRGB(r, g, b, SRGBColorSpace);
    haloMaterial.current.opacity = coreHaloOpacity(load) * dim;
  });

  return (
    <group position={[position.x, position.y, position.z]}>
      <mesh
        ref={body}
        visible={false}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'core', index }));
        }}
        onPointerOut={() =>
          setHovered((current) =>
            current?.kind === 'core' && current.index === index ? null : current,
          )
        }
        onClick={(event) => {
          // 코어 클릭은 아무 동작이 없다. 뒤쪽 천체로 새어 초점이 바뀌지 않게만 막는다.
          if (event.delta <= CLICK_SLOP) {
            event.stopPropagation();
          }
        }}
      >
        <sphereGeometry args={[1, 48, 48]} />
        <shaderMaterial
          ref={material}
          vertexShader={coreVertexShader}
          fragmentShader={coreFragmentShader}
          uniforms={uniforms}
        />
      </mesh>
      <mesh ref={halo} visible={false} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
