import type { InterpolatedSnapshot } from '../state/interpolator';

// M5 스펙 6절. lifecycle 은 한 주기 내내 같은 값으로 통과한다. 매 프레임 그대로
// 읽으면 같은 생성을 60 번 연출하므로, seq 가 바뀔 때 한 번만 이벤트로 바꾼다.

export type LifecycleEvent =
  // 보이는 그룹의 루트가 새로 생겼다.
  | { kind: 'group-born'; key: string; pid: number }
  // 보이는 그룹 안에서 자식이 새로 생겼다.
  | { kind: 'child-born'; key: string; pid: number }
  // 직전에 보이던 그룹의 루트가 종료됐다.
  | { kind: 'group-died'; key: string; pid: number }
  // 직전에 보이던 그룹의 자식이 종료됐다.
  | { kind: 'child-died'; key: string; pid: number };

type SnapshotLike = Pick<InterpolatedSnapshot, 'seq' | 'groups' | 'lifecycle'>;

interface Membership {
  key: string;
  isRoot: boolean;
}

function membershipOf(snapshot: SnapshotLike): Map<number, Membership> {
  const map = new Map<number, Membership>();
  for (const group of snapshot.groups) {
    map.set(group.root_pid, { key: group.key, isRoot: true });
    for (const child of group.children) {
      map.set(child.pid, { key: group.key, isRoot: false });
    }
  }
  return map;
}

export class LifecycleConsumer {
  private lastSeq: number | null = null;
  // 직전에 소비한 스냅샷의 pid → 그룹. 종료된 프로세스는 현재 스냅샷에 없으므로
  // 어느 그룹이었는지는 직전 스냅샷에서 찾아야 한다.
  private previous = new Map<number, Membership>();

  consume(snapshot: SnapshotLike | null): LifecycleEvent[] {
    if (snapshot === null) {
      this.reset();
      return [];
    }
    if (snapshot.seq === this.lastSeq) {
      return [];
    }

    const current = membershipOf(snapshot);
    const isFirst = this.lastSeq === null;
    const events: LifecycleEvent[] = [];

    // 첫 스냅샷은 기준선이다. 직전 스냅샷이 없어 종료를 그룹에 대응시킬 수 없고,
    // 앱을 연 순간 이미 있던 것들을 "생성" 으로 연출하지도 않는다.
    if (!isFirst) {
      for (const spawned of snapshot.lifecycle.spawned) {
        const member = current.get(spawned.pid);
        if (member?.isRoot) {
          events.push({ kind: 'group-born', key: member.key, pid: spawned.pid });
        } else if (member !== undefined) {
          events.push({ kind: 'child-born', key: member.key, pid: spawned.pid });
        }
        // 목록 밖 프로세스의 생성은 연출하지 않는다.
      }
      for (const pid of snapshot.lifecycle.terminated) {
        const member = this.previous.get(pid);
        if (member === undefined) {
          continue;
        }
        events.push({
          kind: member.isRoot ? 'group-died' : 'child-died',
          key: member.key,
          pid,
        });
      }
    }

    this.lastSeq = snapshot.seq;
    this.previous = current;
    return events;
  }

  reset(): void {
    this.lastSeq = null;
    this.previous = new Map();
  }
}
