import { useFrame } from '@react-three/fiber';
import { useMemo, useRef, useState } from 'react';
import { Color } from 'three';

import { sample, type InterpolatedGroup } from '../state/interpolator';
import { useSnapshotStore } from '../state/snapshotStore';
import {
  CHILD_BORN,
  CHILD_DIED,
  GROUP_COLLAPSE,
  GROUP_FORM,
  BurstPool,
  type BurstAnchor,
  type BurstSpec,
} from '../visual/bursts';
import { createFrameCache, layoutNodesFrom, updateFrameCache } from '../visual/frameCache';
import { hash01 } from '../visual/hash';
import { LayoutSim } from '../visual/layout';
import { LifecycleConsumer, type LifecycleEvent } from '../visual/lifecycleEvents';
import { colorFor, radiusFor } from '../visual/mapping';
import { PresenceTracker } from '../visual/presence';
import { CameraRig } from './CameraRig';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { nodeIdsOf, parseNodeId } from './nodeList';
import { Particles } from './Particles';
import { ProcessNode } from './ProcessNode';
import { Satellites } from './Satellites';
import {
  FocusFrame,
  FrameEvents,
  SceneContext,
  type Hovered,
  type SceneContextValue,
} from './sceneContext';
import { Tooltip } from './Tooltip';

// 자식 버스트는 부모 천체 안쪽에서 일어나 보이도록 부모 반지름보다 작게 잡는다.
const CHILD_BURST_RADIUS = 0.5;

function rgbOf(group: Pick<InterpolatedGroup, 'account' | 'key'>): [number, number, number] {
  const hsl = colorFor(group.account, group.key);
  const color = new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  return [color.r, color.g, color.b];
}

// 이벤트 하나를 버스트 명세로. 기준 그룹을 모르면(이미 추적기에서도 사라짐) null.
function burstFor(
  event: LifecycleEvent,
  group: InterpolatedGroup | undefined,
  focusedKey: string | null,
  seq: number,
): BurstSpec | null {
  if (group === undefined) {
    return null;
  }
  const radius = radiusFor(group.mem_mb);
  const color = rgbOf(group);
  const seed = Math.floor(hash01(`${event.kind}:${event.pid}`, seq) * 0x100000000);
  const groupAnchor: BurstAnchor = { kind: 'group', key: event.key, groupKey: event.key };
  // Focus 중인 그룹의 자식이면 그 위성 자리에서 재생한다 (M5 스펙 D30).
  // 위성은 focus 프레임 값의 key 를 따르므로 스토어가 아니라 그것으로 판단한다.
  const childAnchor: BurstAnchor =
    focusedKey === event.key
      ? { kind: 'satellite', key: String(event.pid), groupKey: event.key }
      : groupAnchor;

  switch (event.kind) {
    case 'group-born':
      return { ...GROUP_FORM, anchor: groupAnchor, radius, color, seed };
    case 'group-died':
      return { ...GROUP_COLLAPSE, anchor: groupAnchor, radius, color, seed };
    case 'child-born':
      return { ...CHILD_BORN, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
    case 'child-died':
      return { ...CHILD_DIED, anchor: childAnchor, radius: radius * CHILD_BURST_RADIUS, color, seed };
  }
}

// 스펙 4절(M4)과 M5 스펙 5~7절. 프레임당 한 번 보간하고, 존재 추적기와
// lifecycle 소비기를 갱신하고, 레이아웃을 한 스텝 진행하고, 이벤트를 버스트로
// 바꾼다. 노드 목록은 스토어가 아니라 존재 추적기의 항목이다 — 떠나는 천체도
// 연출이 끝날 때까지 그려야 한다.
export function SceneRoot() {
  const [nodeIds, setNodeIds] = useState<string[]>([]);
  const [hovered, setHovered] = useState<Hovered>(null);
  const signature = useRef('');
  // 마지막으로 본 세션. null 프레임을 못 보고 세션이 바뀌어도 장면을 비우기 위해.
  const lastSession = useRef<string | null>(useSnapshotStore.getState().session);

  const context = useMemo<SceneContextValue>(
    () => ({
      cache: createFrameCache(),
      layout: new LayoutSim(),
      presence: new PresenceTracker<InterpolatedGroup>(),
      events: new FrameEvents(),
      focus: new FocusFrame(),
      satellites: new Map(),
      setHovered,
    }),
    [],
  );
  const consumer = useMemo(() => new LifecycleConsumer(), []);
  const pool = useMemo(() => new BurstPool(), []);

  useFrame(() => {
    // 시계는 performance.now() 다 — arrivedAt 이 같은 시계로 찍힌다.
    const now = performance.now();
    const state = useSnapshotStore.getState();
    const frame = sample(
      {
        previous: state.previous,
        current: state.current,
        arrivedAt: state.arrivedAt,
        intervalMs: state.intervalMs,
      },
      now,
    );
    const { cache, layout, presence } = context;
    updateFrameCache(cache, frame, now);
    const nowSec = now / 1000;

    const sessionChanged = state.session !== lastSession.current;
    lastSession.current = state.session;

    if (frame === null) {
      // 세션이 바뀌었거나 버전 불일치로 스토어가 비었다. 다음 스냅샷은 다시
      // "이미 있던 것" 으로 시작한다.
      presence.reset();
      consumer.reset();
      pool.clear();
      context.events.replace([]);
    } else {
      if (sessionChanged) {
        // 두 프레임 사이에 hello 와 다음 스냅샷이 함께 도착해 null 프레임을 못 봤다.
        // 이전 세션의 장면·초점을 비우고 이 스냅샷을 기준선으로 삼는다.
        presence.reset();
        consumer.reset();
        pool.clear();
        useFocusStore.getState().clear();
      }
      const events = consumer.consume(frame);
      context.events.replace(events);
      const born = new Set(events.filter((e) => e.kind === 'group-born').map((e) => e.key));
      const died = new Set(events.filter((e) => e.kind === 'group-died').map((e) => e.key));
      presence.update(
        frame.groups.map((group) => ({ key: group.key, value: group })),
        born,
        died,
        nowSec,
      );

      const focusedKey = context.focus.key;
      for (const event of events) {
        const spec = burstFor(event, presence.get(event.key)?.value, focusedKey, frame.seq);
        if (spec !== null) {
          pool.add(spec, nowSec);
        }
      }
    }

    const entries = presence.entries();
    layout.step(layoutNodesFrom(entries.map((entry) => entry.value)), cache.dtSec);

    // 초점 그룹이 떠나기 시작하면 초점을 푼다 (M5 스펙 9.1).
    const focus = useFocusStore.getState();
    if (focus.focusedKey !== null) {
      const phase = presence.get(focus.focusedKey)?.phase;
      if (phase === undefined || phase === 'fading-out' || phase === 'collapsing') {
        focus.clear();
      }
    }

    const ids = nodeIdsOf(entries);
    const next = ids.join('\n');
    if (next !== signature.current) {
      signature.current = next;
      setNodeIds(ids);
    }
  }, FRAME_PRIORITY.scene);

  const keys = nodeIds.map((id) => parseNodeId(id).key);
  // 호버 중이던 천체가 사라지면 R3F 가 onPointerOut 없이 오브젝트를 지운다.
  // 지금도 있는 그룹일 때만 그룹 툴팁을 그린다. 위성 툴팁은 위성이 없으면
  // 스스로 숨는다.
  const liveHovered =
    hovered !== null && (hovered.kind === 'satellite' || keys.includes(hovered.key))
      ? hovered
      : null;

  return (
    <SceneContext.Provider value={context}>
      <CameraRig />
      {nodeIds.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      <Satellites />
      <Particles pool={pool} />
      {liveHovered !== null && <Tooltip target={liveHovered} />}
    </SceneContext.Provider>
  );
}
