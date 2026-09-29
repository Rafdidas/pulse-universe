import { clamp01 } from './easing';

// M6 스펙 5절. 코어 부하(0~100 %)를 Orb 의 크기·일그러짐·색·발광으로 바꾼다.
// 셰이더는 이 값들을 uniform 으로 받아 계산만 한다.

export function coreLoad(pct: number): number {
  return clamp01(pct / 100);
}

// 5.1 크기. 0.7 ~ 1.6.
export function orbRadius(load: number): number {
  return 0.7 + 0.9 * clamp01(load);
}

// 5.2 일그러짐. 부하의 제곱에 비례한다 — 대기 코어(절반이 5% 미만)는 거의 매끈해야 한다.
export function distortion(load: number): number {
  const l = clamp01(load);
  return 0.04 + 0.28 * l * l;
}

export function noiseSpeed(load: number): number {
  return 0.25 + 1.75 * clamp01(load);
}

// 셰이더 노이즈의 시간 오프셋을 dt 만큼 진행한다. 누적한다 — 시각 × speed(load) 로
// 넘기면 부하가 바뀌는 순간 오프셋이 (시각 × 속도 변화)만큼 튀어 Orb 가 떨린다.
export function advanceNoiseOffset(offset: number, load: number, dtSec: number): number {
  return offset + noiseSpeed(load) * Math.max(0, dtSec);
}

// 5.3 색. 부하 0 → 0.5 → 1 을 세 색으로 잇는다. RGB 로 섞으면 파랑과 주황 사이가
// 회색이 되어 "식은" 것처럼 보인다. 그래서 HSL 로 섞되 색조를 파랑 → 보라 → 자홍 →
// 주황 방향(증가)으로 돌린다 — 달아오르는 것처럼 보인다.
export interface HslStop {
  // 도. 360 을 넘어도 된다 (색조를 증가 방향으로만 돌리기 위해).
  h: number;
  s: number;
  l: number;
}

export const COLD: HslStop = { h: 205, s: 0.9, l: 0.6 };
export const WARM: HslStop = { h: 382, s: 1.0, l: 0.62 };
export const HOT: HslStop = { h: 402, s: 1.0, l: 0.82 };

// HSL(h 는 도, s·l 은 0~1) → RGB(0~1).
export function hslToRgb(h: number, s: number, l: number): [number, number, number] {
  const hue = (((h % 360) + 360) % 360) / 360;
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  const channel = (t: number) => {
    let x = t;
    if (x < 0) x += 1;
    if (x > 1) x -= 1;
    if (x < 1 / 6) return p + (q - p) * 6 * x;
    if (x < 1 / 2) return q;
    if (x < 2 / 3) return p + (q - p) * (2 / 3 - x) * 6;
    return p;
  };
  return [channel(hue + 1 / 3), channel(hue), channel(hue - 1 / 3)];
}

function mixStop(a: HslStop, b: HslStop, t: number): HslStop {
  return { h: a.h + (b.h - a.h) * t, s: a.s + (b.s - a.s) * t, l: a.l + (b.l - a.l) * t };
}

export function coreHsl(load: number): HslStop {
  const l = clamp01(load);
  return l < 0.5 ? mixStop(COLD, WARM, l / 0.5) : mixStop(WARM, HOT, (l - 0.5) / 0.5);
}

export function coreColor(load: number): [number, number, number] {
  const c = coreHsl(load);
  return hslToRgb(c.h, c.s, c.l);
}

// 5.4 발광. Bloom 은 M8 이다.
export function rimIntensity(load: number): number {
  return 0.3 + 1.2 * clamp01(load);
}

export const CORE_HALO_SCALE = 1.5;

export function coreHaloOpacity(load: number): number {
  return 0.04 + 0.3 * clamp01(load);
}
