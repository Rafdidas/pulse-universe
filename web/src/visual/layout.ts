import { hash01 } from './hash';

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

// 시뮬레이션은 항상 1/60 초 고정 스텝으로 진행한다. 프레임 간격을 그대로
// 쓰면 같은 데이터라도 프레임률에 따라 다른 모양으로 수렴한다.
export const FIXED_STEP = 1 / 60;
// 탭이 백그라운드에 있다 돌아오면 수 초짜리 dt 가 들어온다. 그만큼을
// 한 번에 따라잡으려 하면 한 프레임에 수백 스텝을 돌게 된다.
export const MAX_DT = 1 / 30;
// 빈 시뮬레이션에 처음 노드가 들어오면 이만큼 미리 돌려 둔다. 첫 화면이
// 한 점에서 퍼져 나오는 대신 이미 자리 잡은 모양으로 뜨고, 그 모양은
// 프레임 타이밍과 무관하게 같다.
export const SETTLE_STEPS = 600;

// 아래 상수들은 실제 픽스처 40 개로 돌려 정했다 (스펙 6절).
export const SPAWN_RADIUS = 18;
// 사전 수렴이 끝난 뒤 들어오는 key 는 무리의 가장 먼 노드보다 이만큼 바깥에
// 놓는다. 무리 안쪽에서 생기면 형성 연출이 다른 천체에 가려진다 (M5 스펙 8절).
export const OUTSIDE_MARGIN = 4;
export const COLLISION_GAP = 1.0;
export const COLLISION_STIFFNESS = 8;
export const REPULSION = 3;
export const GRAVITY = 0.05;
// 1/60 초당 속도에 곱하는 감쇠.
export const DAMPING = 0.9;

const SALT_X = 11;
const SALT_Y = 12;
const SALT_Z = 13;

interface Body {
  position: Vec3;
  velocity: Vec3;
}

// key 해시로 정한 단위 방향에 거리 r 을 곱한 점.
function hashedPoint(key: string, r: number): Vec3 {
  const theta = 2 * Math.PI * hash01(key, SALT_X);
  const phi = Math.acos(2 * hash01(key, SALT_Y) - 1);
  return {
    x: r * Math.sin(phi) * Math.cos(theta),
    y: r * Math.cos(phi),
    z: r * Math.sin(phi) * Math.sin(theta),
  };
}

// key 해시로 반지름 SPAWN_RADIUS 인 구 안의 한 점을 고른다.
function spawnPosition(key: string): Vec3 {
  return hashedPoint(key, SPAWN_RADIUS * Math.cbrt(hash01(key, SALT_Z)));
}

export class LayoutSim {
  private readonly bodies = new Map<string, Body>();
  private accumulator = 0;

  get size(): number {
    return this.bodies.size;
  }

  position(key: string): Vec3 | undefined {
    return this.bodies.get(key)?.position;
  }

  step(nodes: readonly LayoutNode[], dtSec: number): void {
    const wasEmpty = this.bodies.size === 0;
    this.sync(nodes, wasEmpty);
    if (nodes.length === 0) {
      return;
    }

    if (wasEmpty) {
      for (let i = 0; i < SETTLE_STEPS; i += 1) {
        this.integrate(nodes);
      }
      this.accumulator = 0;
      return;
    }

    // NaN 이 누적기에 들어가면 이후 모든 프레임이 멈춘다.
    const dt = Number.isFinite(dtSec) ? Math.min(Math.max(dtSec, 0), MAX_DT) : 0;
    this.accumulator += dt;
    while (this.accumulator >= FIXED_STEP) {
      this.integrate(nodes);
      this.accumulator -= FIXED_STEP;
    }
  }

  private sync(nodes: readonly LayoutNode[], wasEmpty: boolean): void {
    // 이미 자리 잡은 무리가 있으면 새 key 는 그 바깥에 놓는다.
    let outside = 0;
    if (!wasEmpty) {
      for (const body of this.bodies.values()) {
        outside = Math.max(outside, Math.hypot(body.position.x, body.position.y, body.position.z));
      }
      outside += OUTSIDE_MARGIN;
    }

    const present = new Set<string>();
    for (const node of nodes) {
      present.add(node.key);
      if (!this.bodies.has(node.key)) {
        this.bodies.set(node.key, {
          position: wasEmpty ? spawnPosition(node.key) : hashedPoint(node.key, outside),
          velocity: { x: 0, y: 0, z: 0 },
        });
      }
    }
    for (const key of this.bodies.keys()) {
      if (!present.has(key)) {
        this.bodies.delete(key);
      }
    }
  }

  private integrate(nodes: readonly LayoutNode[]): void {
    const bodies = nodes.map((node) => this.bodies.get(node.key)!);
    const acc = nodes.map(() => ({ x: 0, y: 0, z: 0 }));

    for (let i = 0; i < nodes.length; i += 1) {
      for (let j = i + 1; j < nodes.length; j += 1) {
        const a = bodies[i].position;
        const b = bodies[j].position;
        const dx = b.x - a.x;
        const dy = b.y - a.y;
        const dz = b.z - a.z;
        const d = Math.hypot(dx, dy, dz);

        // 두 노드가 정확히 같은 자리면 방향이 없다. x 축으로 떼어 낸다.
        let ux = 1;
        let uy = 0;
        let uz = 0;
        if (d > 1e-6) {
          ux = dx / d;
          uy = dy / d;
          uz = dz / d;
        }

        // 원거리 반발. d 가 1 보다 작을 때는 1 로 보아 힘이 무한히 커지지 않게 한다.
        let force = REPULSION / Math.max(d, 1) ** 2;
        const minDistance = nodes[i].radius + nodes[j].radius + COLLISION_GAP;
        if (d < minDistance) {
          force += COLLISION_STIFFNESS * (minDistance - d);
        }

        acc[i].x -= ux * force;
        acc[i].y -= uy * force;
        acc[i].z -= uz * force;
        acc[j].x += ux * force;
        acc[j].y += uy * force;
        acc[j].z += uz * force;
      }
    }

    const damping = DAMPING ** (FIXED_STEP * 60);
    for (let i = 0; i < nodes.length; i += 1) {
      const { position, velocity } = bodies[i];
      const r = nodes[i].radius;
      // 중심 인력은 반지름의 제곱(대략 표면적)에 비례한다. 큰 천체가 가운데로 모인다.
      const pull = GRAVITY * (0.5 + r * r);
      acc[i].x -= pull * position.x;
      acc[i].y -= pull * position.y;
      acc[i].z -= pull * position.z;

      velocity.x = (velocity.x + acc[i].x * FIXED_STEP) * damping;
      velocity.y = (velocity.y + acc[i].y * FIXED_STEP) * damping;
      velocity.z = (velocity.z + acc[i].z * FIXED_STEP) * damping;
      position.x += velocity.x * FIXED_STEP;
      position.y += velocity.y * FIXED_STEP;
      position.z += velocity.z * FIXED_STEP;
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
  sim: LayoutSim,
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
