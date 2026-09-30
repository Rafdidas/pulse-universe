import { hash01 } from './hash';
import { orbitSpeed, planOrbits, type OrbitPlan } from './solar';

// 스펙 6절. three 를 모르는 순수 모듈이다. 위치는 평범한 객체로 다룬다.
export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface LayoutNode {
  key: string;
  radius: number;
}

// 태양계형 배치 스펙 5절. 그룹은 메모리 순위로 정한 동심원 궤도(visual/orbits.ts)를
// 따라 공전한다. 궤도나 자리가 바뀌면 극좌표(반지름, 각)에서 부드럽게 옮겨 간다 —
// 순간이동하지 않고, 가운데를 가로지르지도 않는다.

// 탭이 백그라운드에 있다 돌아오면 수 초짜리 dt 가 들어온다. 공전이 한 번에 크게
// 건너뛰지 않게 자른다.
export const MAX_DT = 1 / 30;
// 목표 자리로 따라가는 지수 평활의 시간 상수(초). 약 3 배(1.5 초)면 거의 도착한다.
export const SETTLE_TAU = 0.5;
// 옮겨 가는 속도의 상한 (장면 단위/초). 평활은 처음 몇 프레임이 가장 빨라서, 궤도 반대편
// 자리로 옮겨 가면 순간적으로 초당 100 단위 넘게 휩쓸고 지나간다. 공전 자체(바깥 궤도에서
// 초당 1 단위 안팎)는 이 상한에 걸리지 않는다.
export const MAX_GLIDE_SPEED = 24;
// 자리 잡은 뒤 들어오는 key 는 가장 바깥 궤도의 천체보다 이만큼 바깥에서 나타난다.
// 무리 안쪽에서 생기면 형성 연출이 다른 천체에 가려진다 (M5 스펙 8절).
export const OUTSIDE_MARGIN = 4;
// 궤도마다 첫 자리를 이만큼씩 돌려 둔다 (황금각). 궤도의 첫 천체들이 한 줄로 서지 않는다.
export const RING_PHASE_STEP = Math.PI * (3 - Math.sqrt(5));

interface Body {
  // 현재 극좌표. XZ 평면이다.
  radius: number;
  angle: number;
  position: Vec3;
}

// a 에서 b 로 가는 가장 짧은 각 차이 (−π, π].
function shortestTurn(from: number, to: number): number {
  const turn = (to - from) % (2 * Math.PI);
  if (turn > Math.PI) {
    return turn - 2 * Math.PI;
  }
  if (turn <= -Math.PI) {
    return turn + 2 * Math.PI;
  }
  return turn;
}

export class OrbitLayout {
  private readonly bodies = new Map<string, Body>();
  // 궤도 순번마다 누적한 공전각. 궤도 반지름이 바뀌어도 순번의 각은 이어진다.
  private readonly spins: number[] = [];
  private current: OrbitPlan = { rings: [], outerRadius: 0 };

  get size(): number {
    return this.bodies.size;
  }

  position(key: string): Vec3 | undefined {
    return this.bodies.get(key)?.position;
  }

  // 지금의 궤도 계획. 궤도선을 그리는 쪽이 읽는다.
  plan(): OrbitPlan {
    return this.current;
  }

  step(nodes: readonly LayoutNode[], dtSec: number): void {
    // NaN 이 들어가면 이후 모든 각이 NaN 이 된다.
    const dt = Number.isFinite(dtSec) ? Math.min(Math.max(dtSec, 0), MAX_DT) : 0;
    const wasEmpty = this.bodies.size === 0;
    const previousOuter = this.current.outerRadius;
    const plan = planOrbits(nodes);
    this.current = plan;

    const follow = 1 - Math.exp(-dt / SETTLE_TAU);
    const present = new Set<string>();
    plan.rings.forEach((ring, k) => {
      const spin = (this.spins[k] ?? 0) + orbitSpeed(ring.radius) * dt;
      this.spins[k] = spin % (2 * Math.PI);
      ring.keys.forEach((key, i) => {
        present.add(key);
        const targetAngle = this.spins[k] + k * RING_PHASE_STEP + (2 * Math.PI * i) / ring.keys.length;
        let body = this.bodies.get(key);
        if (body === undefined) {
          // 처음 화면은 이미 정리된 모양으로 뜬다. 그 뒤의 새 천체는 바깥에서 들어온다.
          const startRadius = wasEmpty
            ? ring.radius
            : Math.max(previousOuter, plan.outerRadius) + OUTSIDE_MARGIN;
          body = { radius: startRadius, angle: targetAngle, position: { x: 0, y: 0, z: 0 } };
          this.bodies.set(key, body);
        } else {
          const reach = MAX_GLIDE_SPEED * dt;
          const dr = (ring.radius - body.radius) * follow;
          body.radius += Math.max(-reach, Math.min(reach, dr));
          // 각은 호 길이로 제한한다: 같은 각이라도 바깥 궤도에서는 더 먼 거리다.
          const maxTurn = reach / Math.max(1, body.radius);
          const da = shortestTurn(body.angle, targetAngle) * follow;
          body.angle += Math.max(-maxTurn, Math.min(maxTurn, da));
        }
        body.position.x = body.radius * Math.cos(body.angle);
        body.position.y = 0;
        body.position.z = body.radius * Math.sin(body.angle);
      });
    });

    for (const key of this.bodies.keys()) {
      if (!present.has(key)) {
        this.bodies.delete(key);
      }
    }
  }
}

const FLOAT_AMPLITUDE = 0.2;
const SALT_FLOAT_PERIOD = 21;
const SALT_FLOAT_PHASE = 24;

// 부유. 시뮬레이션 상태에는 들어가지 않고 그릴 때만 더한다 — 계약서 7.1 절.
// 축마다 주기(6~10 초)와 위상이 key 해시로 다르다.
export function floatOffset(key: string, timeSec: number): Vec3 {
  const axis = (i: number) => {
    const period = 6 + 4 * hash01(key, SALT_FLOAT_PERIOD + i);
    const phase = 2 * Math.PI * hash01(key, SALT_FLOAT_PHASE + i);
    return FLOAT_AMPLITUDE * Math.sin((2 * Math.PI * timeSec) / period + phase);
  };
  return { x: axis(0), y: axis(1), z: axis(2) };
}

// 노드와 툴팁이 같은 자리를 가리키도록 둘 다 이 함수로 위치를 구한다.
export function floatingPosition(
  sim: OrbitLayout,
  key: string,
  timeSec: number,
): Vec3 | undefined {
  const base = sim.position(key);
  if (base === undefined) {
    return undefined;
  }
  const offset = floatOffset(key, timeSec);
  return { x: base.x + offset.x, y: base.y + offset.y, z: base.z + offset.z };
}
