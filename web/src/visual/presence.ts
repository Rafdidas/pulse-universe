import { clamp01, easeInCubic, easeOutCubic } from './easing';

// M5 스펙 5절. 화면에 무엇이 있는지를 스토어의 key 목록이 아니라 이 추적기가
// 정한다. 떠나는 항목은 연출이 끝날 때까지 마지막 값을 고정한 채 남는다.
// 그룹(key = 그룹 key)과 Focus 위성(key = 자식 pid)이 같은 추적기를 쓴다.

export type Phase = 'forming' | 'fading-in' | 'present' | 'fading-out' | 'collapsing';

export const FORM_SEC = 1.2;
export const FADE_SEC = 0.6;
export const COLLAPSE_SEC = 1.0;

const DURATION: Record<Exclude<Phase, 'present'>, number> = {
  forming: FORM_SEC,
  'fading-in': FADE_SEC,
  'fading-out': FADE_SEC,
  collapsing: COLLAPSE_SEC,
};

export interface PresenceEntry<T> {
  key: string;
  phase: Phase;
  // 현재 phase 의 진행도 0~1. present 는 항상 1.
  progress: number;
  // 떠나는 항목은 마지막으로 본 값이 고정되어 있다.
  value: T;
  startSec: number;
}

export interface PresenceInput<T> {
  key: string;
  value: T;
}

function isLeaving(phase: Phase): boolean {
  return phase === 'fading-out' || phase === 'collapsing';
}

// 5절 표. 크기 배율과 불투명도.
export function presenceVisual(entry: Pick<PresenceEntry<unknown>, 'phase' | 'progress'>): {
  scale: number;
  opacity: number;
} {
  const p = clamp01(entry.progress);
  switch (entry.phase) {
    case 'forming':
      return { scale: easeOutCubic(p), opacity: p };
    case 'fading-in':
      return { scale: 1, opacity: p };
    case 'present':
      return { scale: 1, opacity: 1 };
    case 'fading-out':
      return { scale: 1, opacity: 1 - p };
    case 'collapsing':
      return { scale: 1 - easeInCubic(p), opacity: 1 - p * p };
  }
}

export class PresenceTracker<T> {
  private readonly items = new Map<string, PresenceEntry<T>>();
  private initialized = false;

  // born: 이번 프레임에 실제로 생성된 key. died: 실제로 종료된 key.
  // 두 집합은 lifecycle 을 소비한 프레임에만 비어 있지 않다.
  update(
    current: readonly PresenceInput<T>[],
    born: ReadonlySet<string>,
    died: ReadonlySet<string>,
    nowSec: number,
  ): void {
    // 첫 입력은 이미 있던 것들이다. 앱을 열 때 전부가 한꺼번에 페이드인하지 않는다.
    if (!this.initialized) {
      this.initialized = true;
      for (const { key, value } of current) {
        this.items.set(key, { key, phase: 'present', progress: 1, value, startSec: nowSec });
      }
      return;
    }

    const seen = new Set<string>();
    for (const { key, value } of current) {
      seen.add(key);
      const existing = this.items.get(key);
      if (existing === undefined) {
        this.items.set(key, {
          key,
          phase: born.has(key) ? 'forming' : 'fading-in',
          progress: 0,
          value,
          startSec: nowSec,
        });
        continue;
      }
      existing.value = value;
      if (existing.phase === 'fading-out') {
        // 사라지던 것이 다시 나타났다. 지금 불투명도에서 이어서 밝아진다.
        const opacity = 1 - existing.progress;
        existing.phase = 'fading-in';
        existing.startSec = nowSec - opacity * FADE_SEC;
      } else if (existing.phase === 'collapsing') {
        // 종료된 key 가 다시 나타나는 것은 pid 재사용뿐이다. 새로 들어온 것으로 본다.
        existing.phase = 'fading-in';
        existing.startSec = nowSec;
      }
    }

    for (const entry of this.items.values()) {
      if (seen.has(entry.key) || isLeaving(entry.phase)) {
        continue;
      }
      if (died.has(entry.key)) {
        entry.phase = 'collapsing';
        entry.startSec = nowSec;
      } else {
        // 순위 이탈. 지금 불투명도에서 이어서 어두워진다.
        const opacity = presenceVisual(entry).opacity;
        entry.phase = 'fading-out';
        entry.startSec = nowSec - (1 - opacity) * FADE_SEC;
      }
    }

    for (const entry of [...this.items.values()]) {
      if (entry.phase === 'present') {
        entry.progress = 1;
        continue;
      }
      entry.progress = clamp01((nowSec - entry.startSec) / DURATION[entry.phase]);
      if (entry.progress >= 1) {
        if (isLeaving(entry.phase)) {
          this.items.delete(entry.key);
        } else {
          entry.phase = 'present';
        }
      }
    }
  }

  entries(): PresenceEntry<T>[] {
    return [...this.items.values()];
  }

  get(key: string): PresenceEntry<T> | undefined {
    return this.items.get(key);
  }

  // 세션이 바뀌거나 스토어가 비었을 때. 다음 입력은 다시 "이미 있던 것" 이 된다.
  reset(): void {
    this.items.clear();
    this.initialized = false;
  }
}
