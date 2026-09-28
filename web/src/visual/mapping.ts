import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from './hash';

// 스펙 5절. 모든 상수는 여기 한 곳에 있다 — 룩을 바꾸려면 이 파일만 고친다.

// 5.1 반지름. 부피가 메모리에 비례한다 (r ∝ ∛mem). 최댓값으로 정규화하지
// 않는다 — 가장 큰 프로세스가 사라져도 다른 천체의 크기가 튀지 않는다.
export const RADIUS_SCALE = 0.25;
export const MIN_RADIUS = 0.3;

export function radiusFor(memMb: number): number {
  return Math.max(MIN_RADIUS, RADIUS_SCALE * Math.cbrt(Math.max(0, memMb)));
}

// 5.2 활동도. cpu_pct 는 전체 코어 수로 나눈 값이라(28 코어에서 한 코어를
// 꽉 채워도 3.6%) 그대로 쓰면 모든 천체가 어둡다. 사용 중인 코어 개수로
// 환산한 뒤 로그로 누르고, SATURATION_CORES 개에서 1 이 된다.
export const SATURATION_CORES = 4;

export function activityFor(cpuPct: number | null, coreCount: number): number | null {
  // null 은 모름이다. 0 으로 바꾸지 않는다.
  if (cpuPct === null) {
    return null;
  }
  const cores = (Math.max(0, cpuPct) * Math.max(1, coreCount)) / 100;
  return Math.min(1, Math.log2(1 + cores) / Math.log2(1 + SATURATION_CORES));
}

// 5.3 맥박. 활동이 없어도 느리게 숨 쉰다. null 은 기본 호흡으로 그린다.
export interface PulseParams {
  freqHz: number;
  // 반지름 대비 비율.
  amplitude: number;
}

export function pulseFor(activity: number | null): PulseParams {
  const a = activity ?? 0;
  return { freqHz: 0.15 + 1.2 * a, amplitude: 0.02 + 0.06 * a };
}

// 위상은 누적한다. sin(2π·freq·t) 로 계산하면 freq 가 바뀌는 순간 위상이 튄다.
export function advancePhase(phase: number, freqHz: number, dtSec: number): number {
  return (phase + 2 * Math.PI * freqHz * Math.max(0, dtSec)) % (2 * Math.PI);
}

// 5.4 발광. 진짜 Bloom 은 M8 이다. 헤일로는 그때까지 발광을 읽게 해 주는 장치다.
export const HALO_SCALE = 1.35;

export interface Glow {
  emissiveIntensity: number;
  haloOpacity: number;
}

export function glowFor(activity: number | null): Glow {
  const a = activity ?? 0;
  return { emissiveIntensity: 0.15 + 1.6 * a, haloOpacity: 0.05 + 0.35 * a };
}

// 5.5 색. account 가 색 계열을, key 해시가 계열 안의 작은 흔들림을 정한다.
export const HUE_USER = 185;
export const HUE_SYSTEM = 270;
export const HUE_JITTER = 12;
const SALT_HUE = 1;

export interface Hsl {
  // 도 단위.
  h: number;
  // 0~1.
  s: number;
  l: number;
}

export function colorFor(account: ProcessGroup['account'], key: string): Hsl {
  const base = account === 'user' ? HUE_USER : HUE_SYSTEM;
  const jitter = (hash01(key, SALT_HUE) * 2 - 1) * HUE_JITTER;
  return { h: base + jitter, s: 0.65, l: 0.55 };
}
