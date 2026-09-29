// M7 스펙 7.2절. 선 위를 그룹에서 코어 쪽으로 흐르는 빛 입자.

export const MAX_PARTICLES_PER_EDGE = 12;
const PARTICLES_PER_STRENGTH = 30;

export function particleCount(strength: number): number {
  if (!(strength > 0)) {
    return 0;
  }
  return Math.min(MAX_PARTICLES_PER_EDGE, Math.ceil(strength * PARTICLES_PER_STRENGTH));
}

// 곡선 매개변수/초.
export function flowSpeed(weight: number): number {
  return 0.25 + 0.6 * Math.max(0, weight);
}

// 선의 흐름 위상을 누적한다(0~1 에서 감는다). 속도 × 시각 으로 계산하면 weight 가
// 바뀌는 순간 입자가 튄다 (M6 불꽃 위상과 같은 이유).
export function advanceFlowPhase(phase: number, weight: number, dtSec: number): number {
  const dt = Number.isFinite(dtSec) ? Math.max(0, dtSec) : 0;
  const next = phase + flowSpeed(weight) * dt;
  return next - Math.floor(next);
}

// 입자 i (n 개 중)의 곡선 매개변수. [0, 1).
export function particleT(phase: number, i: number, n: number): number {
  const t = phase + i / Math.max(1, n);
  return t - Math.floor(t);
}
