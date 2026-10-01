import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import { OVERVIEW_POSE } from '../../src/visual/camera';
import { placeLabels, type LabelInput } from '../../src/visual/labelPlacement';
import { OrbitLayout, type LayoutNode, type Vec3 } from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';
import { MAX_LABELS } from '../../src/visual/labels';
import { STAR_RADIUS, orbitSpeed, planOrbits } from '../../src/visual/solar';

// 화면 다듬기 스펙 1·6절의 측정을 회귀 시험으로 남긴 것. 픽스처의 40 그룹을 1920×1080, fov 50
// 으로 투영해, 공전 600 초를 훑으며 이름표와 별·천체의 겹침을 센다.
const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));
const nodes: LayoutNode[] = fixture.groups.map((group) => ({ key: group.key, radius: radiusFor(group.mem_mb) }));

const WIDTH = 1920;
const HEIGHT = 1080;
const FOV = 50;
// 별 빛의 화면 반지름은 별 반지름의 1.7 배로 본다 (측정과 같다).
const STAR_GLOW = 1.7;
const LABEL_BOX = { width: 80, height: 16 };

// 카메라가 원점을 바라볼 때의 투영. 화면 좌표(px)와 깊이, 깊이 1 에서의 px/단위를 돌려준다.
function project(camera: Vec3, point: Vec3) {
  const length = Math.hypot(camera.x, camera.y, camera.z);
  const forward = { x: -camera.x / length, y: -camera.y / length, z: -camera.z / length };
  // right = forward × up(0,1,0), up' = right × forward.
  const rx = -forward.z;
  const rz = forward.x;
  const rl = Math.hypot(rx, rz);
  const right = { x: rx / rl, y: 0, z: rz / rl };
  const up = {
    x: right.y * forward.z - right.z * forward.y,
    y: right.z * forward.x - right.x * forward.z,
    z: right.x * forward.y - right.y * forward.x,
  };
  const v = { x: point.x - camera.x, y: point.y - camera.y, z: point.z - camera.z };
  const depth = v.x * forward.x + v.y * forward.y + v.z * forward.z;
  const dx = v.x * right.x + v.y * right.y + v.z * right.z;
  const dy = v.x * up.x + v.y * up.y + v.z * up.z;
  const k = HEIGHT / 2 / Math.tan((FOV * Math.PI) / 360);
  return { sx: WIDTH / 2 + (dx / depth) * k, sy: HEIGHT / 2 - (dy / depth) * k, scale: k / depth };
}

function sweep(visit: (bodies: { key: string; sx: number; sy: number; sr: number }[], star: { sx: number; sy: number; r: number }) => void) {
  const plan = planOrbits(nodes);
  const radiusOf = new Map(nodes.map((node) => [node.key, node.radius]));
  const camera = OVERVIEW_POSE.position;
  for (let t = 0; t < 600; t += 3) {
    const bodies: { key: string; sx: number; sy: number; sr: number }[] = [];
    plan.rings.forEach((ring, k) => {
      ring.keys.forEach((key, i) => {
        const angle = orbitSpeed(ring.radius) * t + (2 * Math.PI * i) / ring.keys.length + k * 2.399;
        const p = project(camera, { x: ring.radius * Math.cos(angle), y: 0, z: ring.radius * Math.sin(angle) });
        bodies.push({ key, sx: p.sx, sy: p.sy, sr: (radiusOf.get(key) as number) * p.scale });
      });
    });
    const star = project(camera, { x: 0, y: 0, z: 0 });
    visit(bodies, { sx: star.sx, sy: star.sy, r: STAR_RADIUS * STAR_GLOW * star.scale });
  }
}

describe('visual polish measurements', () => {
  it('rarely lets an outward label box touch the star glow', () => {
    const firstRing = planOrbits(nodes).rings[0].keys.slice(0, MAX_LABELS);
    let frames = 0;
    let onStar = 0;
    sweep((bodies, star) => {
      const inputs: LabelInput[] = firstRing.map((key) => {
        const body = bodies.find((b) => b.key === key)!;
        return { key, sx: body.sx, sy: body.sy, radiusPx: body.sr, ...LABEL_BOX, wasVisible: false };
      });
      const placed = placeLabels(inputs, star);
      frames += 1;
      // 상자와 원의 교차: 상자에서 원 중심에 가장 가까운 점이 원 안인가.
      const touches = placed.some((p) => {
        if (!p.visible) {
          return false;
        }
        const nearX = Math.max(p.cx - LABEL_BOX.width / 2, Math.min(star.sx, p.cx + LABEL_BOX.width / 2));
        const nearY = Math.max(p.cy - LABEL_BOX.height / 2, Math.min(star.sy, p.cy + LABEL_BOX.height / 2));
        return Math.hypot(nearX - star.sx, nearY - star.sy) < star.r;
      });
      if (touches) {
        onStar += 1;
      }
    });
    // 지금 위(천체 위쪽) 배치는 이 시험에서 프레임의 대부분이 걸린다.
    expect(onStar / frames).toBeLessThan(0.05);
  });

  it('keeps bodies off the star glow (average per frame)', () => {
    let frames = 0;
    let overlapping = 0;
    sweep((bodies, star) => {
      frames += 1;
      overlapping += bodies.filter((b) => Math.hypot(b.sx - star.sx, b.sy - star.sy) < b.sr + star.r).length;
    });
    expect(overlapping / frames).toBeLessThan(0.2);
  });

  it('does not shuffle bodies between orbits under small memory noise', () => {
    // log(mem) 가 감쇠 0.97 로 평균에 돌아가는 잡음, 초당 σ = 1.5 %.
    let seed = 7;
    const random = () => {
      seed = (seed * 1664525 + 1013904223) % 4294967296;
      return seed / 4294967296;
    };
    const normal = () => Math.sqrt(-2 * Math.log(1 - random())) * Math.cos(2 * Math.PI * random());
    const drift = new Map<string, number>(nodes.map((node) => [node.key, 0]));
    const layout = new OrbitLayout();
    let previous = '';
    let changes = 0;
    const SECONDS = 600;
    for (let second = 0; second < SECONDS; second += 1) {
      const noisy = nodes.map((node) => {
        const x = (drift.get(node.key) as number) * 0.97 + 0.015 * normal();
        drift.set(node.key, x);
        return { key: node.key, radius: node.radius * Math.exp(x / 3) };
      });
      // 한 프레임마다 비교해 순간적으로 옮겼다 돌아오는 것도 센다.
      for (let frame = 0; frame < 60; frame += 1) {
        layout.step(noisy, 1 / 60);
        const membership = JSON.stringify(layout.plan().rings.map((ring) => ring.keys));
        if (previous !== '' && membership !== previous) {
          changes += 1;
        }
        previous = membership;
      }
    }
    expect(changes / (SECONDS / 60)).toBeLessThan(1);
  });
});
