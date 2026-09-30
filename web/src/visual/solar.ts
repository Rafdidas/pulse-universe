import type { LayoutNode } from './layout';

// 태양계형 배치 스펙 4절 (파일 이름이 solar 인 것은 위성 궤도의 visual/orbits.ts 와 구별하려는 것). 그룹을 반지름(= 메모리) 내림차순으로 안쪽 궤도부터 채운다.
// 모든 궤도는 XZ 평면의 동심원이다. 순수 함수 — 같은 입력이면 같은 계획이다.

// 가운데 시스템 별의 반지름.
export const STAR_RADIUS = 3.2;
// 별 표면과 첫 궤도 천체 표면 사이의 틈.
export const STAR_GAP = 3.0;
// 같은 궤도에서 이웃한 천체 표면 사이에 두는 호 길이.
export const ARC_GAP = 1.2;
// 궤도 둘레 가운데 천체가 차지해도 되는 비율. 나머지는 빈틈이다.
export const FILL = 0.75;
// 이웃 궤도의 천체 표면 사이 틈.
export const RING_GAP = 3.0;

export interface OrbitRing {
  radius: number;
  // 안쪽 궤도부터, 궤도 안에서는 반지름 내림차순(같으면 key 오름차순)이다.
  keys: string[];
  // 이 궤도에서 가장 큰 천체의 반지름 (= 첫 천체).
  maxBodyRadius: number;
}

export interface OrbitPlan {
  rings: OrbitRing[];
  // 가장 바깥 궤도 반지름 + 그 궤도의 가장 큰 천체 반지름. 새 천체가 나타나는 자리의 기준이다.
  outerRadius: number;
}

// 반지름 내림차순, 같으면 key 오름차순. 순위가 흔들리지 않게 순서를 완전히 정한다.
function byRadiusThenKey(a: LayoutNode, b: LayoutNode): number {
  if (b.radius !== a.radius) {
    return b.radius - a.radius;
  }
  return a.key < b.key ? -1 : a.key > b.key ? 1 : 0;
}

export function planOrbits(nodes: readonly LayoutNode[]): OrbitPlan {
  const sorted = [...nodes].sort(byRadiusThenKey);
  const rings: OrbitRing[] = [];

  let index = 0;
  let previous: OrbitRing | null = null;
  while (index < sorted.length) {
    const first = sorted[index];
    const radius: number =
      previous === null
        ? STAR_RADIUS + STAR_GAP + first.radius
        : previous.radius + previous.maxBodyRadius + RING_GAP + first.radius;
    const capacity = 2 * Math.PI * radius * FILL;

    const ring: OrbitRing = { radius, keys: [], maxBodyRadius: first.radius };
    let used = 0;
    while (index < sorted.length) {
      const need = 2 * sorted[index].radius + ARC_GAP;
      // 궤도에는 최소 한 개가 들어간다.
      if (ring.keys.length > 0 && used + need > capacity) {
        break;
      }
      ring.keys.push(sorted[index].key);
      used += need;
      index += 1;
    }
    // 순위는 어느 궤도에 들어가는지만 정한다. 궤도 안의 자리는 key 순으로 고정한다 —
    // 메모리가 조금 흔들려 이웃한 두 천체의 순위가 바뀔 때마다 자리를 맞바꾸면 둘이 같은
    // 궤도에서 서로를 뚫고 지나간다 (검토에서 실제 40 개 그룹으로 측정: 메모리가 초당
    // 0.2% 흔들리면 프레임의 23% 에서 겹쳤다).
    ring.keys.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
    rings.push(ring);
    previous = ring;
  }

  const last = rings[rings.length - 1];
  return { rings, outerRadius: last === undefined ? STAR_RADIUS : last.radius + last.maxBodyRadius };
}

// 공전 각속도 (rad/s). 케플러처럼 안쪽일수록 빠르다 (ω ∝ r^-1.5).
// REF_RADIUS 궤도가 SPIN_PERIOD_AT_REF 초에 한 바퀴 돈다.
export const REF_RADIUS = 20;
export const SPIN_PERIOD_AT_REF = 180;

export function orbitSpeed(radius: number): number {
  const r = Math.max(1, radius);
  return ((2 * Math.PI) / SPIN_PERIOD_AT_REF) * (REF_RADIUS / r) ** 1.5;
}

// 가운데 별의 밝기 (선형 HDR 배율). 시스템 전체 CPU 사용률(0~100)을 따른다.
// 가장 한가해도 Bloom 임계값(0.5)을 넘어 은은히 빛나고, 바쁠수록 밝아진다.
export const STAR_GAIN_IDLE = 0.9;
export const STAR_GAIN_BUSY = 2.4;

export function starGain(cpuPct: number | null): number {
  const load = cpuPct === null || !Number.isFinite(cpuPct) ? 0 : Math.min(1, Math.max(0, cpuPct / 100));
  return STAR_GAIN_IDLE + (STAR_GAIN_BUSY - STAR_GAIN_IDLE) * load;
}
