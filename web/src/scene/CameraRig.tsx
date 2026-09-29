import { useFrame, useThree } from '@react-three/fiber';
import gsap from 'gsap';
import { useEffect, useRef } from 'react';
import type { Vector3 } from 'three';

import {
  OVERVIEW_POSE,
  blendPose,
  focusDirection,
  focusPose,
  type Pose,
} from '../visual/camera';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// M5 스펙 9.2. GSAP 은 전환 진행도(t)와 초점 강도(weight) 두 수만 움직인다.
// 카메라 자세는 매 프레임 그 진행도로 계산한다 — 그래서 떠다니는 천체를 따라간다.
export const FOCUS_TRANSITION_SEC = 1.0;
const FOCUS_EASE = 'power2.inOut';

// drei OrbitControls(makeDefault)가 등록하는 controls 에서 쓰는 부분만.
interface Controls {
  target: Vector3;
}

function toVec(v: Vector3): Vec3 {
  return { x: v.x, y: v.y, z: v.z };
}

export function CameraRig() {
  const context = useSceneContext();
  // camera·controls 는 효과 안에서 get() 으로 읽는다. 의존성에 넣으면 전환 중에
  // 바뀔 때 정리 함수가 트윈을 죽이고 재실행은 일찍 반환해 t·weight 가 멈춘다.
  const get = useThree((state) => state.get);
  const focusedKey = useFocusStore((state) => state.focusedKey);

  // GSAP 이 직접 바꾸는 평범한 객체.
  const tween = useRef({ t: 1, weight: 0 });
  const tweening = useRef(false);
  const from = useRef<Pose>(OVERVIEW_POSE);
  const direction = useRef<Vec3>({ x: 0, y: 0, z: 1 });
  const lastNode = useRef<Vec3 | null>(null);

  useEffect(() => {
    const { focus, cache, layout } = context;
    // 처음 마운트될 때(초점 없음 → 초점 없음)는 전환할 것이 없다. 사용자의
    // 궤도 조작과 자동 회전을 1초 동안 빼앗지 않는다.
    if (focusedKey === null && focus.key === null) {
      return;
    }
    const camera = get().camera;
    const controls = get().controls as unknown as Controls | null;
    focus.begin(focusedKey);

    from.current = {
      position: toVec(camera.position),
      target: controls === null ? { ...OVERVIEW_POSE.target } : toVec(controls.target),
    };
    if (focusedKey !== null && cache.timeSec !== null) {
      const node = floatingPosition(layout, focusedKey, cache.timeSec);
      if (node !== undefined) {
        direction.current = focusDirection(from.current.position, node);
      }
    }
    lastNode.current = null;

    tween.current.t = 0;
    tweening.current = true;
    const animation = gsap.to(tween.current, {
      t: 1,
      weight: focusedKey === null ? 0 : 1,
      duration: FOCUS_TRANSITION_SEC,
      ease: FOCUS_EASE,
      onComplete: () => {
        tweening.current = false;
      },
    });
    return () => {
      animation.kill();
    };
  }, [focusedKey, get, context]);

  // 장면이 사라지면(대시보드로 전환) 초점도 푼다.
  useEffect(() => () => useFocusStore.getState().clear(), []);

  // 카메라와 controls 는 useFrame 이 넘겨주는 state 에서 꺼내 쓴다 — R3F 가
  // 렌더 루프 안에서 바꾸라고 내주는 객체다.
  useFrame((state) => {
    const camera = state.camera;
    const controls = state.controls as unknown as Controls | null;
    const { focus, cache, layout, presence } = context;
    focus.sync(tween.current.t, tween.current.weight);
    if (controls === null || cache.timeSec === null) {
      return;
    }

    const key = focus.key;
    const node = key === null ? undefined : floatingPosition(layout, key, cache.timeSec);
    const group = key === null ? undefined : presence.get(key)?.value;

    if (tweening.current) {
      const to =
        node !== undefined && group !== undefined
          ? focusPose(node, radiusFor(group.mem_mb), direction.current)
          : OVERVIEW_POSE;
      const pose = blendPose(from.current, to, focus.t);
      camera.position.set(pose.position.x, pose.position.y, pose.position.z);
      controls.target.set(pose.target.x, pose.target.y, pose.target.z);
      lastNode.current = node ?? null;
      return;
    }

    // 전환이 끝난 뒤에는 사용자가 그 천체 주위를 돌려 볼 수 있어야 한다.
    // 카메라를 고정하지 않고, 천체가 떠다닌 만큼만 카메라와 주시점을 옮긴다.
    if (node !== undefined) {
      if (lastNode.current !== null) {
        camera.position.x += node.x - lastNode.current.x;
        camera.position.y += node.y - lastNode.current.y;
        camera.position.z += node.z - lastNode.current.z;
      }
      controls.target.set(node.x, node.y, node.z);
      lastNode.current = node;
    }
  }, FRAME_PRIORITY.camera);

  return null;
}
