// M8 스펙 5~7절. 후처리 상수와 계산. 장면은 선형 HDR 버퍼에 그려지고
// 심도 → Bloom → ACES 톤 매핑을 한 번씩 거친다 (D43).

function clamp01(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(1, Math.max(0, value));
}

// Bloom (D44). 선형 휘도가 임계값을 넘는 것만 번진다.
export const BLOOM_THRESHOLD = 0.5;
export const BLOOM_SMOOTHING = 0.25;
export const BLOOM_INTENSITY = 0.8;
export const BLOOM_RADIUS = 0.7;

// 심도 (D45). Focus 중에만 흐려진다.
export const BOKEH_SCALE = 4;
export const FOCUS_RANGE = 12;

// 개요 화면(weight 0)에서는 흐리지 않는다.
export function bokehFor(weight: number): number {
  return BOKEH_SCALE * clamp01(weight);
}

// 코어 Orb 의 HDR 배율 (D47). 한가한 코어는 임계값 아래에 머물고, 뜨거운 코어만
// 임계값을 넘어 번진다.
export const ORB_GAIN_MAX = 2.2;

export function orbGain(load: number): number {
  const l = clamp01(load);
  return 1 + (ORB_GAIN_MAX - 1) * l * l;
}
