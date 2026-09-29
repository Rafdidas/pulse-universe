import { clamp01 } from './easing';
import { hash01 } from './hash';
import type { Vec3 } from './layout';

// M6 스펙 6절. 코어마다 최대 24 개의 불꽃이 제 코어 둘레를 돈다. 부하가 클수록
// 많이 켜지고 빨리 돈다 (계약서 4.5 "파티클 밀도").

export const SPARKS_PER_CORE = 24;
const ORBIT_MIN = 1.3;
const ORBIT_MAX = 2.2;

const SALT_ORBIT = 41;
const SALT_TILT = 42;
const SALT_NODE = 43;
const SALT_PHASE = 44;

export function activeSparks(load: number): number {
  return Math.round(clamp01(load) * SPARKS_PER_CORE);
}

export function sparkAngularSpeed(load: number): number {
  return 0.6 + 2.0 * clamp01(load);
}

// 코어 하나의 궤도 위상을 dt 만큼 진행한다. 위상은 누적한다 — 각도를
// speed(load) × 시각 으로 계산하면 performance.now() 가 수천 초일 때 부하가
// 조금만 바뀌어도 각도가 몇 바퀴씩 튄다 (M4 맥박 위상과 같은 이유).
export function advanceSparkPhase(phase: number, load: number, dtSec: number): number {
  return (phase + sparkAngularSpeed(load) * Math.max(0, dtSec)) % (2 * Math.PI);
}

// 불꽃 (coreIndex, i) 의 위치. 궤도 반지름은 Orb 반지름의 1.3~2.2 배, 궤도면·초기
// 위상은 해시, orbitPhase 는 그 코어의 누적 위상이다.
export function sparkPosition(
  coreIndex: number,
  i: number,
  center: Vec3,
  orbRadius: number,
  orbitPhase: number,
): Vec3 {
  const key = `${coreIndex}:${i}`;
  const r = orbRadius * (ORBIT_MIN + (ORBIT_MAX - ORBIT_MIN) * hash01(key, SALT_ORBIT));
  const tilt = (hash01(key, SALT_TILT) - 0.5) * Math.PI;
  const node = hash01(key, SALT_NODE) * 2 * Math.PI;
  const angle = hash01(key, SALT_PHASE) * 2 * Math.PI + orbitPhase;

  const px = r * Math.cos(angle);
  const pz = r * Math.sin(angle);
  const y1 = -pz * Math.sin(tilt);
  const z1 = pz * Math.cos(tilt);
  const x2 = px * Math.cos(node) + z1 * Math.sin(node);
  const z2 = -px * Math.sin(node) + z1 * Math.cos(node);
  return { x: center.x + x2, y: center.y + y1, z: center.z + z2 };
}
