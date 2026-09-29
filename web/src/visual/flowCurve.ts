import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M7 스펙 6절. 그룹 천체에서 코어로 가는 2차 베지어 아치. 제어점을 중간점 위로
// 올려 무리 위로 솟았다 고리로 내려앉게 하고, key 해시로 옆으로 조금 흩어 같은
// 코어로 가는 선들이 한 줄로 겹치지 않게 한다.

export const ARCH = 0.35;
export const SPREAD = 0.3;
const SALT_SIDE = 61;

export function flowControlPoint(from: Vec3, to: Vec3, key: string): Vec3 {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const dz = to.z - from.z;
  const dist = Math.hypot(dx, dy, dz);
  // (to − from) × (0,1,0) = (−dz, 0, dx) — 수평 법선.
  const sideLength = Math.hypot(dz, dx);
  const sx = sideLength > 1e-9 ? -dz / sideLength : 1;
  const sz = sideLength > 1e-9 ? dx / sideLength : 0;
  const side = SPREAD * dist * (hash01(key, SALT_SIDE) - 0.5);
  return {
    x: (from.x + to.x) / 2 + sx * side,
    y: (from.y + to.y) / 2 + ARCH * dist,
    z: (from.z + to.z) / 2 + sz * side,
  };
}

// 곡선 위의 점. out 에 써서 돌려준다 — 매 프레임 수천 번 불리므로 할당하지 않는다.
export function curvePoint(from: Vec3, control: Vec3, to: Vec3, t: number, out: Vec3): Vec3 {
  const u = 1 - t;
  const a = u * u;
  const b = 2 * u * t;
  const c = t * t;
  out.x = a * from.x + b * control.x + c * to.x;
  out.y = a * from.y + b * control.y + c * to.y;
  out.z = a * from.z + b * control.z + c * to.z;
  return out;
}
