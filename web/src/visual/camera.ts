import type { Vec3 } from './layout';

// M5 스펙 9.2. GSAP 은 전환 진행도 하나만 움직인다. 카메라 자세는 매 프레임
// 이 함수들로 계산한다 — 그래서 떠다니는 천체를 따라갈 수 있다.

export interface Pose {
  position: Vec3;
  target: Vec3;
}

// 태양계형 배치 스펙 D64: 궤도가 한 평면에 있으므로 위에서 비스듬히 내려다본다.
export const OVERVIEW_POSE: Pose = {
  position: { x: 0, y: 50, z: 66 },
  target: { x: 0, y: 0, z: 0 },
};

export const FOCUS_DISTANCE_PER_RADIUS = 7;
export const FOCUS_DISTANCE_BASE = 6;

export function focusDistance(radius: number): number {
  return radius * FOCUS_DISTANCE_PER_RADIUS + FOCUS_DISTANCE_BASE;
}

// 노드에서 카메라 쪽을 가리키는 단위 벡터. 전환 시작 때 한 번 정해 두면
// 카메라가 지금 보던 쪽에서 곧장 다가간다. 두 점이 겹치면 +z.
export function focusDirection(camera: Vec3, node: Vec3): Vec3 {
  const dx = camera.x - node.x;
  const dy = camera.y - node.y;
  const dz = camera.z - node.z;
  const length = Math.hypot(dx, dy, dz);
  if (length < 1e-6) {
    return { x: 0, y: 0, z: 1 };
  }
  return { x: dx / length, y: dy / length, z: dz / length };
}

export function focusPose(node: Vec3, radius: number, direction: Vec3): Pose {
  const d = focusDistance(radius);
  return {
    position: {
      x: node.x + direction.x * d,
      y: node.y + direction.y * d,
      z: node.z + direction.z * d,
    },
    target: { ...node },
  };
}

function lerpVec(a: Vec3, b: Vec3, t: number): Vec3 {
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t };
}

export function blendPose(from: Pose, to: Pose, t: number): Pose {
  return {
    position: lerpVec(from.position, to.position, t),
    target: lerpVec(from.target, to.target, t),
  };
}
