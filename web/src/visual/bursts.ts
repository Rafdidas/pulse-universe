import { clamp01, easeInCubic, easeOutCubic } from './easing';
import type { Vec3 } from './layout';

// M5 스펙 7절. 버스트 하나는 한 천체(또는 위성)에서 재생되는 입자 묶음이다.
// 입자 위치는 시각과 기준점의 순수 함수다. 모든 버스트가 풀 하나를 나눠 쓴다.

export type BurstKind = 'converge' | 'implode' | 'scatter';

export interface BurstAnchor {
  kind: 'group' | 'satellite';
  key: string;
  // 기준 그룹의 key. 그룹 기준점이면 key 와 같다. 위성이 없을 때 그룹 자리로 물러난다.
  groupKey: string;
}

export interface BurstSpec {
  kind: BurstKind;
  anchor: BurstAnchor;
  count: number;
  durationSec: number;
  // 기준 천체의 반지름. 입자의 출발·도착 거리가 여기에 비례한다.
  radius: number;
  // 0~1 RGB.
  color: [number, number, number];
  seed: number;
}

export interface Burst extends BurstSpec {
  id: number;
  startSec: number;
  // 입자별 단위 방향(3 × count)과 거리 흩뜨림(count). 생성 시 한 번 만든다.
  directions: Float32Array;
  jitter: Float32Array;
  // 기준 천체가 사라져도 입자가 제자리에서 끝나도록 마지막 기준점을 기억한다.
  lastCenter: Vec3 | null;
}

// 7절 표.
export const GROUP_FORM: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'converge',
  count: 160,
  durationSec: 1.2,
};
export const GROUP_COLLAPSE: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'scatter',
  count: 200,
  durationSec: 1.0,
};
export const CHILD_BORN: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'converge',
  count: 24,
  durationSec: 0.8,
};
export const CHILD_DIED: Pick<BurstSpec, 'kind' | 'count' | 'durationSec'> = {
  kind: 'implode',
  count: 24,
  durationSec: 0.8,
};

export const POOL_CAPACITY = 2048;
// converge 입자가 출발하는 거리 (반지름 배수).
export const CONVERGE_START = 3.5;
// scatter 입자가 흩어지는 거리 (반지름 배수, 표면에서부터).
export const SCATTER_DISTANCE = 2.5;

// 작고 결정적인 의사난수. 같은 seed 는 같은 입자 배치를 만든다.
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function makeParticles(count: number, seed: number): Pick<Burst, 'directions' | 'jitter'> {
  const random = mulberry32(seed);
  const directions = new Float32Array(count * 3);
  const jitter = new Float32Array(count);
  for (let i = 0; i < count; i += 1) {
    // 구면 위 균일한 방향.
    const theta = 2 * Math.PI * random();
    const z = 2 * random() - 1;
    const r = Math.sqrt(1 - z * z);
    directions[i * 3] = r * Math.cos(theta);
    directions[i * 3 + 1] = z;
    directions[i * 3 + 2] = r * Math.sin(theta);
    jitter[i] = 0.7 + 0.6 * random();
  }
  return { directions, jitter };
}

export interface ParticleState {
  x: number;
  y: number;
  z: number;
  // 0~1. 가산 블렌딩이므로 색에 곱해 쓴다.
  alpha: number;
}

// 입자 i 의 현재 상태. 버스트의 시간 밖이면 null.
export function particleAt(
  burst: Burst,
  index: number,
  nowSec: number,
  center: Vec3,
): ParticleState | null {
  const elapsed = nowSec - burst.startSec;
  if (elapsed < 0 || elapsed >= burst.durationSec || index >= burst.count) {
    return null;
  }
  const t = clamp01(elapsed / burst.durationSec);
  const j = burst.jitter[index];
  let distance: number;
  let alpha: number;
  switch (burst.kind) {
    case 'converge':
      // 멀리서 천천히 출발해 중심에 빨려 들 듯 도착한다. 도착 직전에 가장 밝다.
      distance = burst.radius * CONVERGE_START * j * (1 - easeInCubic(t));
      alpha = t < 0.8 ? t / 0.8 : (1 - t) / 0.2;
      break;
    case 'implode':
      distance = burst.radius * j * (1 - easeInCubic(t));
      alpha = 1 - t;
      break;
    case 'scatter':
      distance = burst.radius * (1 + SCATTER_DISTANCE * j * easeOutCubic(t));
      alpha = 1 - t;
      break;
  }
  return {
    x: center.x + burst.directions[index * 3] * distance,
    y: center.y + burst.directions[index * 3 + 1] * distance,
    z: center.z + burst.directions[index * 3 + 2] * distance,
    alpha,
  };
}

export class BurstPool {
  private bursts: Burst[] = [];
  private nextId = 1;
  private readonly capacity: number;

  constructor(capacity = POOL_CAPACITY) {
    this.capacity = capacity;
  }

  // 풀이 가득 차면 가장 오래된 버스트부터 버린다. 한 스냅샷에 이벤트가 몰려도
  // 화면이 입자로 뒤덮이지 않는다.
  add(spec: BurstSpec, startSec: number): Burst {
    const count = Math.min(spec.count, this.capacity);
    while (this.used() + count > this.capacity && this.bursts.length > 0) {
      this.bursts.shift();
    }
    const burst: Burst = {
      ...spec,
      count,
      id: this.nextId,
      startSec,
      lastCenter: null,
      ...makeParticles(count, spec.seed),
    };
    this.nextId += 1;
    this.bursts.push(burst);
    return burst;
  }

  // 끝난 버스트를 치우고 살아 있는 것을 돌려준다.
  active(nowSec: number): readonly Burst[] {
    this.bursts = this.bursts.filter((b) => nowSec < b.startSec + b.durationSec);
    return this.bursts;
  }

  used(): number {
    return this.bursts.reduce((sum, b) => sum + b.count, 0);
  }

  clear(): void {
    this.bursts = [];
  }
}
