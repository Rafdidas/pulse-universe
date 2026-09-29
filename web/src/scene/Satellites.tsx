import { useFrame } from '@react-three/fiber';
import { useMemo, useRef, useState } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import { floatingPosition } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { SATELLITE_SCALE, orbitFor, satellitePosition } from '../visual/orbits';
import { PresenceTracker } from '../visual/presence';
import { FRAME_PRIORITY } from './framePriority';
import { SatelliteNode } from './SatelliteNode';
import { useSceneContext } from './sceneContext';

interface Shown {
  account: ProcessGroup['account'];
  pids: number[];
}

const NOTHING: Shown = { account: 'user', pids: [] };

// M5 스펙 9.4. 초점 그룹의 자식을 위성으로 돌린다. 위성도 존재 추적기를
// 거친다 — 초점 중 자식이 생기면 형성되고, 끝나면 붕괴한다.
export function Satellites() {
  const context = useSceneContext();
  const tracker = useMemo(() => new PresenceTracker<ChildProcess>(), []);
  const owner = useRef<string | null>(null);
  // 마지막으로 React 에 넘긴 추적기 version. -1 은 "아무것도 안 보여 주는 중".
  const shownVersion = useRef(-1);
  const [shown, setShown] = useState<Shown>(NOTHING);

  useFrame(() => {
    const { focus, presence, layout, cache, satellites, events } = context;
    // 초점을 푸는 동안에는 직전 초점 그룹으로 위성이 접혀 들어간다.
    const nextOwner = focus.key ?? (focus.weight > 0 ? focus.previousKey : null);
    if (nextOwner !== owner.current) {
      owner.current = nextOwner;
      tracker.reset();
    }

    const entry = nextOwner === null ? undefined : presence.get(nextOwner);
    const center =
      nextOwner === null || cache.timeSec === null
        ? undefined
        : floatingPosition(layout, nextOwner, cache.timeSec);
    satellites.clear();
    if (entry === undefined || center === undefined || cache.timeSec === null) {
      if (shownVersion.current !== -1) {
        shownVersion.current = -1;
        setShown(NOTHING);
      }
      return;
    }

    const born = new Set<string>();
    const died = new Set<string>();
    // 이벤트가 없는 프레임(대부분)에는 빈 집합 그대로 넘어간다.
    for (const event of events.list) {
      if (event.key !== nextOwner) {
        continue;
      }
      if (event.kind === 'child-born') {
        born.add(String(event.pid));
      } else if (event.kind === 'child-died') {
        died.add(String(event.pid));
      }
    }
    tracker.update(
      entry.value.children.map((child) => ({ key: String(child.pid), value: child })),
      born,
      died,
      cache.timeSec,
    );

    // 초점이 잡히면서 궤도가 펼쳐지고, 풀리면서 접힌다.
    const spread = focus.key === nextOwner ? focus.t : 1 - focus.t;
    const parentRadius = radiusFor(entry.value.mem_mb);
    for (const satellite of tracker.entries()) {
      const pid = satellite.value.pid;
      satellites.set(pid, {
        position: satellitePosition(orbitFor(parentRadius, pid), center, cache.timeSec, spread),
        radius: radiusFor(satellite.value.mem_mb) * SATELLITE_SCALE,
        child: satellite.value,
        entry: satellite,
        account: entry.value.account,
      });
    }

    // 위성 목록은 추적기 version 이 바뀔 때만 다시 만든다. 주인이 바뀌면 tracker.reset()
    // 이 (추적기가 비어 있지 않았다면) version 을 올려 계정 변경도 여기서 잡힌다.
    // 비어 있던 추적기는 version 이 그대로지만 그릴 위성이 없으니 문제없다.
    if (tracker.version !== shownVersion.current) {
      shownVersion.current = tracker.version;
      const pids = tracker.entries().map((satellite) => satellite.value.pid);
      setShown({ account: entry.value.account, pids });
    }
  }, FRAME_PRIORITY.satellites);

  return (
    <>
      {shown.pids.map((pid) => (
        <SatelliteNode key={pid} pid={pid} account={shown.account} />
      ))}
    </>
  );
}
