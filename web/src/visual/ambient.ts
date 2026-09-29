import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M6 스펙 7절. svchost 계열 서비스 프로세스(ambient)를 배경 먼지로 그린다.
// 위치는 인덱스의 해시로만 정해진다 — 개수가 바뀌어도 이미 있던 먼지는 제자리다.

export const AMBIENT_PER_SERVICE = 6;
export const AMBIENT_MAX = 800;
export const AMBIENT_INNER = 40;
export const AMBIENT_OUTER = 90;

const SALT_THETA = 51;
const SALT_PHI = 52;
const SALT_RADIUS = 53;

export function ambientCount(serviceProcCount: number): number {
  return Math.min(Math.max(0, Math.floor(serviceProcCount)) * AMBIENT_PER_SERVICE, AMBIENT_MAX);
}

export function ambientPoint(index: number): Vec3 {
  const key = String(index);
  const theta = 2 * Math.PI * hash01(key, SALT_THETA);
  const phi = Math.acos(2 * hash01(key, SALT_PHI) - 1);
  const r = AMBIENT_INNER + (AMBIENT_OUTER - AMBIENT_INNER) * hash01(key, SALT_RADIUS);
  return {
    x: r * Math.sin(phi) * Math.cos(theta),
    y: r * Math.cos(phi),
    z: r * Math.sin(phi) * Math.sin(theta),
  };
}
