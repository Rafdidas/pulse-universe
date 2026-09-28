import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from './hash';

// スペック第5節。すべての定数はここ一箇所にある — ルックを変えるなら、このファイルだけを直す。

// 5.1 半径。体積がメモリに比例している (r ∝ ∛mem)。最大値で正規化しない。
// 最も大きいプロセスが消えても、他の天体のサイズが飛ばない。
export const RADIUS_SCALE = 0.25;
export const MIN_RADIUS = 0.3;

export function radiusFor(memMb: number): number {
  return Math.max(MIN_RADIUS, RADIUS_SCALE * Math.cbrt(Math.max(0, memMb)));
}

// 5.2 活動度。cpu_pct は全体のコア数で割った値だから（28 コアで 1 コアを
// 満杯にしても 3.6%）そのまま使うと、すべての天体が暗い。使用中のコア個数に
// 換算した後に対数で押し下げて、SATURATION_CORES 個で 1 になる。
export const SATURATION_CORES = 4;

export function activityFor(cpuPct: number | null, coreCount: number): number | null {
  // null は分からないことだ。0 に変わらない。
  if (cpuPct === null) {
    return null;
  }
  const cores = (Math.max(0, cpuPct) * Math.max(1, coreCount)) / 100;
  return Math.min(1, Math.log2(1 + cores) / Math.log2(1 + SATURATION_CORES));
}

// 5.3 脈動。活動がなくても遅くゆっくり呼吸する。null は既定の呼吸で描く。
export interface PulseParams {
  freqHz: number;
  // 半径に対する比率。
  amplitude: number;
}

export function pulseFor(activity: number | null): PulseParams {
  const a = activity ?? 0;
  return { freqHz: 0.15 + 1.2 * a, amplitude: 0.02 + 0.06 * a };
}

// 位相は累積する。sin(2π·freq·t) で計算するなら freq が変わる瞬間に位相が飛ぶ。
export function advancePhase(phase: number, freqHz: number, dtSec: number): number {
  return (phase + 2 * Math.PI * freqHz * Math.max(0, dtSec)) % (2 * Math.PI);
}

// 5.4 発光。実物の Bloom は M8 だ。ハロはそれまで発光を読むために提供する仕掛けだ。
export const HALO_SCALE = 1.35;

export interface Glow {
  emissiveIntensity: number;
  haloOpacity: number;
}

export function glowFor(activity: number | null): Glow {
  const a = activity ?? 0;
  return { emissiveIntensity: 0.15 + 1.6 * a, haloOpacity: 0.05 + 0.35 * a };
}

// 5.5 色。account が色族を定める。key ハッシュが族内の小さな揺らぎを定める。
export const HUE_USER = 185;
export const HUE_SYSTEM = 270;
export const HUE_JITTER = 12;
const SALT_HUE = 1;

export interface Hsl {
  // 度単位。
  h: number;
  // 0~1。
  s: number;
  l: number;
}

export function colorFor(account: ProcessGroup['account'], key: string): Hsl {
  const base = account === 'user' ? HUE_USER : HUE_SYSTEM;
  const jitter = (hash01(key, SALT_HUE) * 2 - 1) * HUE_JITTER;
  return { h: base + jitter, s: 0.65, l: 0.55 };
}
