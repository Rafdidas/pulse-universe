import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, BufferGeometry, Color, SRGBColorSpace } from 'three';

import { coreColor, coreLoad } from '../visual/coreMapping';
import { coreIndexById, corePosition } from '../visual/coreRing';
import { curvePoint, flowControlPoint } from '../visual/flowCurve';
import { MAX_PARTICLES_PER_EDGE, advanceFlowPhase, particleCount, particleT } from '../visual/flowParticles';
import { FlowTracker, MAX_EDGES, edgeStrength, type FlowEdge } from '../visual/flowTracker';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { colorFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { DIM_DEPTH } from './interaction';
import { useSceneContext, type FocusFrame, type Hovered } from './sceneContext';

// M7 스펙 7절.
const SEGMENTS = 24;
const PARTICLE_SIZE = 0.22;
// 계약서 3.1: source 에 따라 선명도만 달리한다. 입자는 같다.
const CLARITY: Record<FlowEdge['source'], number> = { estimated: 0.55, measured: 1.0 };
// 호버한 대상과 이어지지 않은 선의 밝기 (D42).
const UNRELATED = 0.3;

// 프레임마다 새로 만들지 않는 임시 값들.
const groupRgb = new Color();
const coreRgb = new Color();
const from: Vec3 = { x: 0, y: 0, z: 0 };
const to: Vec3 = { x: 0, y: 0, z: 0 };
const a: Vec3 = { x: 0, y: 0, z: 0 };
const b: Vec3 = { x: 0, y: 0, z: 0 };

// 초점과 무관한 선은 다른 천체와 같은 비율로 어두워진다.
function dimFor(focus: FocusFrame, group: string): number {
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === group) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}

// 호버한 그룹·코어와 이어진 선만 밝다. 호버가 없거나 위성이면 전부 밝다.
function highlightFor(hovered: Hovered, edge: FlowEdge, coreIndex: number): number {
  if (hovered === null || hovered.kind === 'satellite') {
    return 1;
  }
  if (hovered.kind === 'group') {
    return hovered.key === edge.group ? 1 : UNRELATED;
  }
  return hovered.index === coreIndex ? 1 : UNRELATED;
}

// 그룹 천체에서 코어 Orb 로 흐르는 빛. 추적기 갱신(잔광)과 선·입자 그리기를 한 곳에서
// 한다 — 곡선을 선마다 한 번만 계산한다. 존재 추적기와 레이아웃이 정해진 뒤에 돈다.
export function FlowStreams() {
  const { cache, layout, presence, focus, hover } = useSceneContext();
  const tracker = useMemo(() => new FlowTracker(), []);
  // 선마다의 흐름 위상. 누적한다 (advanceFlowPhase 참조).
  const phases = useRef(new Map<string, number>());

  const lineGeometry = useMemo(() => {
    const g = new BufferGeometry();
    const vertices = MAX_EDGES * SEGMENTS * 2;
    g.setAttribute('position', new BufferAttribute(new Float32Array(vertices * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(vertices * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  const particleGeometry = useMemo(() => {
    const g = new BufferGeometry();
    const capacity = MAX_EDGES * MAX_PARTICLES_PER_EDGE;
    g.setAttribute('position', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setAttribute('color', new BufferAttribute(new Float32Array(capacity * 3), 3));
    g.setDrawRange(0, 0);
    return g;
  }, []);

  useEffect(() => () => lineGeometry.dispose(), [lineGeometry]);
  useEffect(() => () => particleGeometry.dispose(), [particleGeometry]);

  useFrame(() => {
    const snapshot = cache.snapshot;
    if (snapshot === null || cache.timeSec === null) {
      tracker.reset();
      phases.current.clear();
      lineGeometry.setDrawRange(0, 0);
      particleGeometry.setDrawRange(0, 0);
      return;
    }

    const live = new Set(presence.entries().map((entry) => entry.key));
    tracker.update(snapshot.flows, live, cache.dtSec);

    const edges = tracker.edges();
    // 추적기에서 사라진 선의 위상을 버린다.
    for (const key of phases.current.keys()) {
      if (!edges.some((edge) => edge.key === key)) {
        phases.current.delete(key);
      }
    }

    const cores = snapshot.cores;
    const indexById = coreIndexById(cores);
    const hovered = hover.current;
    const linePositions = lineGeometry.getAttribute('position') as BufferAttribute;
    const lineColors = lineGeometry.getAttribute('color') as BufferAttribute;
    const particlePositions = particleGeometry.getAttribute('position') as BufferAttribute;
    const particleColors = particleGeometry.getAttribute('color') as BufferAttribute;
    let lineVertex = 0;
    let particle = 0;

    for (const edge of edges) {
      const entry = presence.get(edge.group);
      const coreIndex = indexById.get(edge.coreId);
      const start =
        entry === undefined ? undefined : floatingPosition(layout, edge.group, cache.timeSec);
      // 코어가 목록에 없으면 이번 프레임에는 그리지 않는다. 추적기에는 남는다.
      if (entry === undefined || coreIndex === undefined || start === undefined) {
        continue;
      }
      from.x = start.x;
      from.y = start.y;
      from.z = start.z;
      const end = corePosition(coreIndex, cores.length);
      to.x = end.x;
      to.y = end.y;
      to.z = end.z;
      const control = flowControlPoint(from, to, edge.key);

      const hsl = colorFor(entry.value.account, edge.group);
      groupRgb.setHSL(hsl.h / 360, hsl.s, hsl.l);
      const [cr, cg, cb] = coreColor(coreLoad(cores[coreIndex].pct));
      // coreColor 는 sRGB 값이다. 버텍스 색은 선형으로 읽히므로 바꿔 넣는다 (M6 불꽃과 같다).
      coreRgb.setRGB(cr, cg, cb, SRGBColorSpace);

      const strength = edgeStrength(edge);
      const shade = dimFor(focus, edge.group) * highlightFor(hovered, edge, coreIndex);
      const lineAlpha = Math.min(1, CLARITY[edge.source] * (0.25 + 1.6 * strength)) * shade;

      for (let s = 0; s < SEGMENTS; s += 1) {
        const t0 = s / SEGMENTS;
        const t1 = (s + 1) / SEGMENTS;
        curvePoint(from, control, to, t0, a);
        curvePoint(from, control, to, t1, b);
        linePositions.setXYZ(lineVertex, a.x, a.y, a.z);
        lineColors.setXYZ(
          lineVertex,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t0) * lineAlpha,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t0) * lineAlpha,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t0) * lineAlpha,
        );
        lineVertex += 1;
        linePositions.setXYZ(lineVertex, b.x, b.y, b.z);
        lineColors.setXYZ(
          lineVertex,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t1) * lineAlpha,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t1) * lineAlpha,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t1) * lineAlpha,
        );
        lineVertex += 1;
      }

      const phase = advanceFlowPhase(phases.current.get(edge.key) ?? 0, edge.weight, cache.dtSec);
      phases.current.set(edge.key, phase);
      const n = particleCount(strength);
      const glow = (0.4 + 0.6 * edge.intensity) * shade;
      for (let i = 0; i < n; i += 1) {
        const t = particleT(phase, i, n);
        curvePoint(from, control, to, t, a);
        particlePositions.setXYZ(particle, a.x, a.y, a.z);
        particleColors.setXYZ(
          particle,
          (groupRgb.r + (coreRgb.r - groupRgb.r) * t) * glow,
          (groupRgb.g + (coreRgb.g - groupRgb.g) * t) * glow,
          (groupRgb.b + (coreRgb.b - groupRgb.b) * t) * glow,
        );
        particle += 1;
      }
    }

    lineGeometry.setDrawRange(0, lineVertex);
    particleGeometry.setDrawRange(0, particle);
    linePositions.needsUpdate = true;
    lineColors.needsUpdate = true;
    particlePositions.needsUpdate = true;
    particleColors.needsUpdate = true;
  }, FRAME_PRIORITY.particles);

  return (
    <>
      {/* 선과 입자는 호버·클릭 대상이 아니다. 경계 구가 바뀌므로 절두체 선별도 끈다. */}
      <lineSegments geometry={lineGeometry} frustumCulled={false} raycast={() => null}>
        <lineBasicMaterial vertexColors transparent blending={AdditiveBlending} depthWrite={false} />
      </lineSegments>
      <points geometry={particleGeometry} frustumCulled={false} raycast={() => null}>
        <pointsMaterial
          size={PARTICLE_SIZE}
          sizeAttenuation
          vertexColors
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </points>
    </>
  );
}
