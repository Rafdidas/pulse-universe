import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M5 스펙 9.4. Focus 한 그룹의 자식을 위성으로 돌린다. 궤도는 pid 해시로만
// 정해진다 — 자식 목록의 순서가 바뀌어도 위성이 다른 궤도로 튀지 않는다.

export interface Orbit {
  radius: number;
  // 궤도면을 x 축으로 기울인 각(라디안).
  tilt: number;
  // 기울인 궤도면을 y 축으로 돌린 각(라디안).
  node: number;
  phase: number;
  // rad/s.
  angularSpeed: number;
}

export const SATELLITE_SCALE = 0.45;
const RINGS = 4;
const RING_GAP = 0.35;
const BASE_SPEED = 0.25;

const SALT_TILT = 31;
const SALT_NODE = 32;
const SALT_PHASE = 33;
const SALT_RING = 34;

export function orbitFor(parentRadius: number, pid: number): Orbit {
  const key = String(pid);
  const ring = Math.floor(hash01(key, SALT_RING) * RINGS);
  const inner = parentRadius * 1.8;
  const radius = inner + 1.2 + RING_GAP * ring;
  return {
    radius,
    // ±54°. 모든 위성이 한 평면에 겹치지 않게 한다.
    tilt: (hash01(key, SALT_TILT) - 0.5) * Math.PI * 0.6,
    node: hash01(key, SALT_NODE) * 2 * Math.PI,
    phase: hash01(key, SALT_PHASE) * 2 * Math.PI,
    // 바깥 궤도일수록 느리다.
    angularSpeed: BASE_SPEED * (inner / radius),
  };
}

// spread 는 Focus 전환 진행도(0~1). 0 이면 부모 중심, 1 이면 제 궤도.
export function satellitePosition(
  orbit: Orbit,
  center: Vec3,
  timeSec: number,
  spread: number,
): Vec3 {
  const angle = orbit.phase + orbit.angularSpeed * timeSec;
  const r = orbit.radius * spread;
  // 궤도면(xz) 위의 점.
  const px = r * Math.cos(angle);
  const pz = r * Math.sin(angle);
  // x 축으로 tilt 만큼 기울인다.
  const y1 = -pz * Math.sin(orbit.tilt);
  const z1 = pz * Math.cos(orbit.tilt);
  // y 축으로 node 만큼 돌린다.
  const x2 = px * Math.cos(orbit.node) + z1 * Math.sin(orbit.node);
  const z2 = -px * Math.sin(orbit.node) + z1 * Math.cos(orbit.node);
  return { x: center.x + x2, y: center.y + y1, z: center.z + z2 };
}
