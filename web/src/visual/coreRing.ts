import type { Vec3 } from './layout';

// M6 스펙 4절. CPU 코어는 프로세스 무리를 두르는 XZ 평면 고리 위에 id 순서대로
// 같은 간격으로 놓인다. 위치는 고정이다 — M7 의 flow 가 이 자리를 끝점으로 쓴다.

export const RING_MIN_RADIUS = 28;
// 고리 둘레 위 이웃 코어 사이의 간격. 코어가 많으면 고리가 넓어져 Orb 가 겹치지 않는다.
export const ORB_SPACING = 4.5;

export function ringRadius(coreCount: number): number {
  return Math.max(RING_MIN_RADIUS, (Math.max(0, coreCount) * ORB_SPACING) / (2 * Math.PI));
}

// index 는 cores 배열에서의 순서다(엔진이 id 순으로 보낸다). 첫 코어는 +x 에 놓인다.
export function corePosition(index: number, coreCount: number): Vec3 {
  const r = ringRadius(coreCount);
  const theta = (2 * Math.PI * index) / Math.max(1, coreCount);
  return { x: r * Math.cos(theta), y: 0, z: r * Math.sin(theta) };
}

// flows[].core 는 코어 id 다. cores 배열에서의 순서(index)와 다를 수 있다 — 64 코어를
// 넘는 머신 등 (M7 스펙 5절). 같은 프레임의 cores 로 id → index 를 만든다.
export function coreIndexById(cores: readonly { id: number }[]): Map<number, number> {
  const map = new Map<number, number>();
  cores.forEach((core, index) => map.set(core.id, index));
  return map;
}
