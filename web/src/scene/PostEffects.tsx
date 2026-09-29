import { useFrame } from '@react-three/fiber';
import { Bloom, DepthOfField, EffectComposer, ToneMapping } from '@react-three/postprocessing';
import type { DepthOfFieldEffect } from 'postprocessing';
import { ToneMappingMode } from 'postprocessing';
import { useEffect, useRef, useState } from 'react';
import { HalfFloatType, UnsignedByteType, type Vector3 } from 'three';

import {
  BLOOM_INTENSITY,
  BLOOM_RADIUS,
  BLOOM_SMOOTHING,
  BLOOM_THRESHOLD,
  FOCUS_RANGE,
  bokehFor,
} from '../visual/postfx';
import { FOCUS_TRANSITION_SEC } from './CameraRig';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { renderSupport } from './renderSupport';
import { useSceneContext } from './sceneContext';

// M9 스펙 4절. MSAA 샘플 수. 8 에서 4 로 줄여도 가장자리 차이는 작고, dpr 2 에서 약 1 ms 를 던다.
const MULTISAMPLING = 4;
// 반정밀 렌더 타깃을 못 쓰면 8비트로 그린다 (M9 스펙 7절). Bloom 이 덜 밝아진다.
const FRAME_BUFFER_TYPE = renderSupport.halfFloat ? HalfFloatType : UnsignedByteType;

// drei OrbitControls(makeDefault)가 등록하는 controls 에서 쓰는 부분만.
interface Controls {
  target: Vector3;
}

// M8 스펙 4절. 장면을 선형 HDR 버퍼에 그린 뒤 심도 → Bloom → ACES 를 한 번씩 거친다.
// 세 효과는 래퍼가 하나의 EffectPass 로 합친다. Bloom 은 심도를 거치지 않은 선명한
// 장면 입력을 읽고, 그 결과가 심도 결과 뒤에 더해진다.
// 톤 매핑은 여기 한 곳에서만 한다. composer 의 장면은 렌더 타깃에 그려지고, 렌더 타깃에
// 그릴 때 three 는 재질에 톤 매핑을 적용하지 않는다. 또 @react-three/postprocessing 은
// 마운트되어 있는 동안 gl.toneMapping 을 NoToneMapping 으로 강제한다. 따라서 ToneMapping
// 효과가 유일한 톤 매핑이며, composer 밖에서 그리는 것은 톤 매핑을 받지 못한다.
export function PostEffects() {
  const { focus } = useSceneContext();
  const depthOfField = useRef<DepthOfFieldEffect>(null);
  const depthActive = useDepthOfFieldActive();

  // 초점은 OrbitControls 의 target 이다. Focus 중에는 카메라 연출이 target 을 초점
  // 천체로 옮기므로 초점 대상의 좌표를 따로 구하지 않는다 (스펙 7절).
  useFrame((state) => {
    const effect = depthOfField.current;
    if (effect === null) {
      return;
    }
    const controls = state.controls as unknown as Controls | null;
    effect.target = controls === null ? null : controls.target;
    effect.bokehScale = bokehFor(focus.weight);
  }, FRAME_PRIORITY.postfx);

  return (
    <EffectComposer frameBufferType={FRAME_BUFFER_TYPE} multisampling={MULTISAMPLING}>
      {depthActive ? (
        <DepthOfField ref={depthOfField} focusRange={FOCUS_RANGE} bokehScale={0} />
      ) : (
        <></>
      )}
      <Bloom
        mipmapBlur
        luminanceThreshold={BLOOM_THRESHOLD}
        luminanceSmoothing={BLOOM_SMOOTHING}
        intensity={BLOOM_INTENSITY}
        radius={BLOOM_RADIUS}
      />
      <ToneMapping mode={ToneMappingMode.ACES_FILMIC} />
    </EffectComposer>
  );
}

// 심도 패스는 흐림 세기가 0 이어도 모든 패스를 돈다 (M9 스펙 2.2절, dpr 2 에서 약 3.3 ms).
// 초점이 잡혀 있거나, 풀린 뒤 카메라 전환이 끝나기 전(흐림이 걷히는 중)에만 composer 에 넣는다.
function useDepthOfFieldActive(): boolean {
  const focused = useFocusStore((state) => state.focusedKey !== null);
  // 초점이 풀릴 때마다 1 씩 늘어나는 번호. 전환이 끝나면 null. 번호가 바뀌면 타이머를 새로
  // 건다 — 풀고 곧바로 다시 잡았다 푸는 경우에도 마지막 해제부터 전환 시간을 잰다.
  const [release, setRelease] = useState<number | null>(null);
  const [wasFocused, setWasFocused] = useState(focused);
  // 초점이 풀리는 순간을 렌더 중에 잡는다 (이전 값과 비교하는 React 권장 방식).
  if (focused !== wasFocused) {
    setWasFocused(focused);
    if (!focused) {
      setRelease((count) => (count ?? 0) + 1);
    }
  }
  useEffect(() => {
    if (release === null) {
      return;
    }
    const timer = window.setTimeout(() => setRelease(null), FOCUS_TRANSITION_SEC * 1000);
    return () => window.clearTimeout(timer);
  }, [release]);
  return focused || release !== null;
}
