# 이름표·별 정보·범례 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 우주 화면을 처음 보는 사람이 읽을 수 있게 한다 — 가운데 별에 시스템 요약 툴팁, 가장 안쪽 궤도의 큰 천체 이름표, 읽는 법을 알려 주는 범례.

**Architecture:** 이름표 대상 고르기(`labelKeys`)와 별 툴팁 문자열(`systemDetail`)은 `web/src/visual/labels.ts` 의 순수 함수다. 별이 호버를 받아(`Hovered` 에 `system`) 기존 `Tooltip` 이 시스템 요약을 보여 준다. `BodyLabels` 가 궤도 계획의 첫 궤도 천체에 drei `Html` 이름표를 붙인다. 범례는 React 컴포넌트(`shell/Legend.tsx`)로 `H` 키로 접고 펼치며 상태를 `localStorage` 에 기억한다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / drei 10.7 / React 19 / Vitest 5 + Testing Library

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-30-labels-legend-design.md`.
- 작업 브랜치는 `feature/labels-legend` 다. `engine/` 은 건드리지 않는다.
- **이 계획의 코드 블록은 시제품으로 실제 엔진에 붙여 검증한 것이다(마지막 수정 뒤 전체 검사 포함). 한국어 주석까지 한 글자도 바꾸지 않고 옮긴다.** 번역·요약·재배치하지 않는다. 브리프의 코드 블록을 프로그램으로 추출해 쓰는 것이 가장 안전하다. "전체 교체" 는 파일 전체를 블록 내용으로 바꾼다는 뜻이다. "파일 끝에 붙인다" 는 기존 마지막 줄 뒤에 빈 줄 하나를 두고 블록을 그대로 붙인다는 뜻이다.
- **`web/src/visual/orbits.ts` 는 Focus 위성 궤도 모듈이다. 건드리지 않는다.**
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `postprocessing`, `@react-three/*`, `zustand`, `gsap`, `snapshotStore`, `stream/` 을 import 하지 않는다. `protocol/` 의 타입은 쓸 수 있다.
- 프레임 값은 React 상태에 넣지 않는다. UI 글은 영어다 (기존 화면과 같다).
- oxlint 에 새 경고가 생기면 안 된다 (특히 `react(immutability)`).
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료, 또는 계획의 코드로 테스트·타입체크·린트·빌드가 실패하면 BLOCKED 로 보고한다.** 코드나 기대값을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 빈 줄과 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 를 붙인다 (실행 세션의 모델이 다르면 그 모델 이름으로 — 컨트롤러가 지정한다). `.claude/` 는 절대 스테이징하지 않는다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
명령은 C:\dev\pulse-uni\web 에서 실행한다.
```

기준선(작업 전 `main` + 스펙 커밋): 웹 284 tests 통과.

## File Structure

```
web/src/visual/labels.ts       MAX_LABELS, labelKeys, systemDetail
web/src/shell/Legend.tsx       범례
web/src/shell/Shell.tsx        (전체 교체) 우주 화면에 Legend
web/src/shell/shell.css        (끝에 붙임) .legend*, .universe-label
web/src/scene/sceneContext.ts  (전체 교체) Hovered 에 { kind: 'system' }
web/src/scene/FlowStreams.tsx  (전체 교체) system 은 호버 없음처럼
web/src/scene/SystemStar.tsx   (전체 교체) 호버 받기
web/src/scene/Tooltip.tsx      (전체 교체) system 처리
web/src/scene/BodyLabels.tsx   안쪽 궤도 천체의 이름표
web/src/scene/SceneRoot.tsx    (전체 교체) BodyLabels 마운트
web/tests/visual/labels.test.ts
web/tests/legend.test.tsx
```

---

## Task 1: 이름표 대상과 별 요약 (순수)

**Files:**
- Create: `web/src/visual/labels.ts`
- Test: `web/tests/visual/labels.test.ts`

**Interfaces:**
- Consumes: `SystemTotals` (`protocol/schema.ts`, 타입), `OrbitPlan` (`visual/solar.ts`, 타입), `planOrbits` (테스트).
- Produces:
  - `MAX_LABELS = 8`
  - `labelKeys(plan: OrbitPlan, limit?: number): string[]` — 첫 궤도의 key 를 최대 `limit`(기본 `MAX_LABELS`) 개. 궤도가 없거나 `limit <= 0` 이면 빈 배열.
  - `systemDetail(system: SystemTotals): string` — `CPU 12.3% · Mem 26.3 / 32.5 GB · 406 procs · 8,481 threads`. CPU 가 `null` 이면 `CPU -%`.

- [ ] **Step 1: 실패하는 테스트 — `web/tests/visual/labels.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import type { SystemTotals } from '../../src/protocol/schema';
import { MAX_LABELS, labelKeys, systemDetail } from '../../src/visual/labels';
import { planOrbits } from '../../src/visual/solar';

function system(overrides: Partial<SystemTotals> = {}): SystemTotals {
  return {
    cpu_pct: 12.34,
    mem_used_mb: 26.3 * 1024,
    mem_total_mb: 32.5 * 1024,
    process_total: 406,
    thread_total: 8481,
    ...overrides,
  };
}

describe('labelKeys', () => {
  it('labels only the bodies of the innermost orbit', () => {
    const nodes = [
      { key: 'big.exe:1', radius: 4 },
      { key: 'mid.exe:2', radius: 3.9 },
      ...Array.from({ length: 30 }, (_, i) => ({ key: `small${String(i).padStart(2, '0')}.exe:${i}`, radius: 0.9 })),
    ];
    const plan = planOrbits(nodes);
    expect(plan.rings.length).toBeGreaterThan(1);
    expect(labelKeys(plan)).toEqual(plan.rings[0].keys.slice(0, MAX_LABELS));
    for (const key of labelKeys(plan)) {
      expect(plan.rings[0].keys).toContain(key);
    }
  });

  it('caps the number of labels', () => {
    const nodes = Array.from({ length: 6 }, (_, i) => ({ key: `k${i}`, radius: 2 }));
    const plan = planOrbits(nodes);
    expect(labelKeys(plan, 3)).toHaveLength(3);
    expect(labelKeys(plan, 0)).toEqual([]);
    expect(labelKeys(plan, -2)).toEqual([]);
  });

  it('labels nothing when there are no bodies', () => {
    expect(labelKeys(planOrbits([]))).toEqual([]);
  });
});

describe('systemDetail', () => {
  it('shows CPU, memory in GB, processes and threads', () => {
    expect(systemDetail(system())).toBe('CPU 12.3% · Mem 26.3 / 32.5 GB · 406 procs · 8,481 threads');
  });

  it('shows a dash for an unknown CPU reading and keeps a measured zero', () => {
    expect(systemDetail(system({ cpu_pct: null }))).toContain('CPU -%');
    expect(systemDetail(system({ cpu_pct: 0 }))).toContain('CPU 0.0%');
  });

  it('separates thousands in the process and thread counts', () => {
    expect(systemDetail(system({ process_total: 1234, thread_total: 1234567 }))).toContain(
      '1,234 procs · 1,234,567 threads',
    );
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project node tests/visual/labels.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/labels"`.

- [ ] **Step 3: `web/src/visual/labels.ts`**

```ts
import type { SystemTotals } from '../protocol/schema';
import type { OrbitPlan } from './solar';

// 이름표·별 정보 스펙 4절. 이름표는 가장 안쪽 궤도(= 가장 큰 천체들)에만 항상 보인다.
// 40 개 전부에 띄우면 글자로 덮인다.
export const MAX_LABELS = 8;

// 궤도 안의 자리는 key 순으로 고정되어 있으므로 대상이 깜빡이지 않는다.
export function labelKeys(plan: OrbitPlan, limit: number = MAX_LABELS): string[] {
  const first = plan.rings[0];
  if (first === undefined) {
    return [];
  }
  return first.keys.slice(0, Math.max(0, limit));
}

const MB_PER_GB = 1024;

function gb(mb: number): string {
  return (mb / MB_PER_GB).toFixed(1);
}

// 별 툴팁의 세부. 숫자는 대시보드와 같은 방식(CPU 는 소수 한 자리, 모름은 '-')이다.
export function systemDetail(system: SystemTotals): string {
  const cpu = system.cpu_pct === null ? '-' : system.cpu_pct.toFixed(1);
  return (
    `CPU ${cpu}% · Mem ${gb(system.mem_used_mb)} / ${gb(system.mem_total_mb)} GB` +
    ` · ${system.process_total.toLocaleString('en-US')} procs` +
    ` · ${system.thread_total.toLocaleString('en-US')} threads`
  );
}
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `npx vitest run --project node tests/visual/labels.test.ts`
Expected: PASS, `Tests  6 passed (6)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  290 passed (290)`, typecheck 출력 없음, lint 는 기존 `src/main.tsx` 경고 1개뿐.

- [ ] **Step 6: 커밋**

```
git add src/visual/labels.ts tests/visual/labels.test.ts
git commit -m "feat(web): pick the labelled bodies from the innermost orbit and summarise the system for the star tooltip"
```

---

## Task 2: 범례

**Files:**
- Create: `web/src/shell/Legend.tsx`
- Modify (전체 교체): `web/src/shell/Shell.tsx`
- Modify (끝에 붙임): `web/src/shell/shell.css`
- Test: `web/tests/legend.test.tsx`

**Interfaces:**
- Consumes: `isShortcut(event: KeyboardEvent, letter: string): boolean` (`shell/shortcut.ts`).
- Produces: `Legend()` — 우주 화면에서만 마운트된다 (`Shell` 이 `<UniverseBadge />` 다음에). `localStorage['pulse.legend']` 는 `'hidden'` 이면 접힘, 그 밖(없음·`'shown'`)이면 펼침. 저장이 막힌 환경에서도 동작한다.

- [ ] **Step 1: 실패하는 테스트 — `web/tests/legend.test.tsx`**

```tsx
import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { Legend } from '../src/shell/Legend';

const KEY = 'pulse.legend';

function pressH(init: KeyboardEventInit = {}, target: Window | Element = window) {
  act(() => {
    fireEvent.keyDown(target, { key: 'h', code: 'KeyH', ...init });
  });
}

describe('Legend', () => {
  beforeEach(() => {
    window.localStorage.clear();
  });
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('starts open on the first visit and explains the encoding', () => {
    render(<Legend />);
    const legend = screen.getByRole('complementary', { name: 'Legend' });
    expect(legend).toHaveTextContent('Size');
    expect(legend).toHaveTextContent('memory');
    expect(legend).toHaveTextContent('measured');
  });

  it('collapses and expands with H and remembers the choice', () => {
    render(<Legend />);
    pressH();
    expect(screen.queryByRole('complementary', { name: 'Legend' })).toBeNull();
    expect(screen.getByRole('button', { name: /Legend/ })).toBeInTheDocument();
    expect(window.localStorage.getItem(KEY)).toBe('hidden');

    pressH();
    expect(screen.getByRole('complementary', { name: 'Legend' })).toBeInTheDocument();
    expect(window.localStorage.getItem(KEY)).toBe('shown');
  });

  it('collapses with the button and expands from the collapsed chip', () => {
    render(<Legend />);
    fireEvent.click(screen.getByRole('button', { name: 'Hide legend' }));
    expect(screen.queryByRole('complementary', { name: 'Legend' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: /Legend/ }));
    expect(screen.getByRole('complementary', { name: 'Legend' })).toBeInTheDocument();
  });

  it('starts collapsed when the choice was stored', () => {
    window.localStorage.setItem(KEY, 'hidden');
    render(<Legend />);
    expect(screen.queryByRole('complementary', { name: 'Legend' })).toBeNull();
  });

  it('ignores H while typing in a field, with a modifier, or while repeating', () => {
    render(
      <>
        <Legend />
        <input aria-label="field" />
      </>,
    );
    pressH({}, screen.getByLabelText('field'));
    pressH({ ctrlKey: true });
    pressH({ repeat: true });
    expect(screen.getByRole('complementary', { name: 'Legend' })).toBeInTheDocument();
  });

  it('still works when storage throws', () => {
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
      throw new Error('blocked');
    });
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('blocked');
    });
    render(<Legend />);
    expect(screen.getByRole('complementary', { name: 'Legend' })).toBeInTheDocument();
    pressH();
    expect(screen.queryByRole('complementary', { name: 'Legend' })).toBeNull();
  });
});
```

- [ ] **Step 2: 실패 확인 (RED)**

Run: `npx vitest run --project jsdom tests/legend.test.tsx`
Expected: FAIL — `Failed to resolve import "../src/shell/Legend"`.

- [ ] **Step 3: `web/src/shell/Legend.tsx`**

```tsx
import { useEffect, useState } from 'react';

import { isShortcut } from './shortcut';

// 이름표·별 정보 스펙 5절. 우주 화면을 읽는 법. 접은 상태는 브라우저에 기억한다.
const STORAGE_KEY = 'pulse.legend';

function readHidden(): boolean {
  try {
    return window.localStorage.getItem(STORAGE_KEY) === 'hidden';
  } catch {
    // 저장이 막힌 환경(사생활 보호 창 등)에서는 펼침으로 시작한다.
    return false;
  }
}

function writeHidden(hidden: boolean): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, hidden ? 'hidden' : 'shown');
  } catch {
    // 저장은 건너뛴다. 범례는 그대로 동작한다.
  }
}

export function Legend() {
  const [hidden, setHidden] = useState(readHidden);

  const toggle = (): void => setHidden((current) => !current);
  // 상태가 바뀔 때마다 기억한다. 상태 갱신 함수 안에서 저장하지 않는다 (순수해야 한다).
  useEffect(() => writeHidden(hidden), [hidden]);

  // H 로 접고 편다 (IME·수정 키·입력창 규칙은 isShortcut 이 처리한다).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (isShortcut(event, 'h')) {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (hidden) {
    return (
      <button type="button" className="legend legend-collapsed" onClick={toggle}>
        Legend [H]
      </button>
    );
  }

  return (
    <aside className="legend" aria-label="Legend">
      <header>
        <span>Legend</span>
        <button type="button" onClick={toggle} aria-label="Hide legend">
          [H]
        </button>
      </header>
      <dl>
        <dt>Size</dt>
        <dd>memory</dd>
        <dt>Glow, pulse</dt>
        <dd>CPU</dd>
        <dt>Inner orbit</dt>
        <dd>larger memory</dd>
        <dt>Outer ring</dt>
        <dd>CPU cores (blue → orange → white = load)</dd>
        <dt>Line</dt>
        <dd>group → core it runs on; sharp = measured, faint = estimated</dd>
      </dl>
    </aside>
  );
}
```

- [ ] **Step 4: `web/src/shell/Shell.tsx` 전체 교체**

```tsx
import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { Universe } from '../scene/Universe';
import { Legend } from './Legend';
import { SceneErrorBoundary } from './SceneErrorBoundary';
import { isShortcut } from './shortcut';
import { UniverseBadge } from './UniverseBadge';
import { toggledHash, viewFromHash } from './view';
import './shell.css';

function subscribeToHash(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

function currentHash(): string {
  return window.location.hash;
}

// 우주와 대시보드 중 하나만 마운트한다. 숨긴 쪽을 남겨 두면 대시보드의
// 100 ms 타이머와 WebGL 렌더 루프가 함께 돈다.
// Shell 은 스냅샷마다 바뀌는 값(status, seq)을 구독하지 않는다 — 구독하면
// <Universe/> 까지 매초 재렌더된다(스펙 4절 규칙 3). 배지는 UniverseBadge 가
// 따로 구독한다.
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      // 자동 반복을 무시하지 않으면 키를 누르고 있는 동안 초당 30번씩 뷰가
      // 뒤집히며 WebGL 컨텍스트를 매번 새로 만든다 (isShortcut 참조).
      if (isShortcut(event, 'd')) {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [toggle]);

  return (
    <>
      {view === 'dashboard' ? (
        <App />
      ) : (
        <>
          <SceneErrorBoundary>
            <Universe />
          </SceneErrorBoundary>
          <UniverseBadge />
          <Legend />
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
```

- [ ] **Step 5: `web/src/shell/shell.css` 끝에 붙인다** (기존 마지막 줄 뒤에 빈 줄 하나, 그다음 이 블록)

```css
/* 이름표·별 정보 스펙 5절. 범례. */
.legend {
  position: fixed;
  right: 12px;
  bottom: 12px;
  max-width: 300px;
  padding: 8px 10px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
  color: #c9d3de;
  font-size: 12px;
  line-height: 1.5;
}

.legend header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
  color: #e8edf3;
}

.legend header button,
.legend-collapsed {
  font: inherit;
  color: #d7dde5;
  background: none;
  border: none;
  cursor: pointer;
}

.legend-collapsed {
  padding: 4px 8px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
}

.legend dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 1px 10px;
  margin: 0;
}

.legend dt {
  color: #8f9aa8;
}

.legend dd {
  margin: 0;
}
```

- [ ] **Step 6: 통과 확인 (GREEN)**

Run: `npx vitest run --project jsdom tests/legend.test.tsx tests/shell.test.tsx`
Expected: PASS, `legend.test.tsx` 6 개 + 기존 `shell.test.tsx` 전부.

- [ ] **Step 7: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: `Tests  296 passed (296)`, typecheck 깨끗, lint 기존 경고 1개.

- [ ] **Step 8: 커밋**

```
git add src/shell/Legend.tsx src/shell/Shell.tsx src/shell/shell.css tests/legend.test.tsx
git commit -m "feat(web): add a collapsible legend that explains size, glow, orbits, the core ring and flow lines"
```

---

## Task 3: 장면 — 별 호버, 시스템 툴팁, 이름표

**Files:**
- Create: `web/src/scene/BodyLabels.tsx`
- Modify (전체 교체): `web/src/scene/sceneContext.ts`, `web/src/scene/FlowStreams.tsx`, `web/src/scene/SystemStar.tsx`, `web/src/scene/Tooltip.tsx`, `web/src/scene/SceneRoot.tsx`
- Modify (끝에 붙임): `web/src/shell/shell.css`

**Interfaces:**
- Consumes: Task 1 의 `labelKeys`, `systemDetail`. 기존 `OrbitLayout.plan()`, `floatingPosition`, `radiusFor`, `STAR_RADIUS`, `FRAME_PRIORITY.tooltip`, `useSceneContext` (`hover`, `focus`, `presence`, `layout`, `cache`, `setHovered`).
- Produces: `Hovered` 에 `{ kind: 'system' }`. `BodyLabels()` — `SceneRoot` 가 `<OrbitRings />` 다음에 마운트한다.

이 Task 는 R3F 장면 코드라 jsdom 에서 자동 테스트하지 않는다(계약서 10절). 컨트롤러가 실제 엔진에 붙여 확인한다.

- [ ] **Step 1: `web/src/scene/sceneContext.ts` 전체 교체**

```ts
import { createContext, useContext } from 'react';

import type { ChildProcess, ProcessGroup } from '../protocol/schema';
import type { InterpolatedGroup } from '../state/interpolator';
import type { FrameCache } from '../visual/frameCache';
import type { OrbitLayout, Vec3 } from '../visual/layout';
import type { LifecycleEvent } from '../visual/lifecycleEvents';
import type { PresenceEntry, PresenceTracker } from '../visual/presence';

export type Hovered =
  | { kind: 'group'; key: string }
  | { kind: 'satellite'; pid: number }
  | { kind: 'core'; index: number }
  | { kind: 'system' }
  | null;

// CameraRig 가 매 프레임 채운다. 노드는 이것으로 어두워지고, 위성은 펼쳐진다.
// 장면 곳곳이 같은 인스턴스를 읽으므로 값은 메서드로만 바꾼다.
export class FocusFrame {
  // 지금 초점 그룹. 없으면 null.
  key: string | null = null;
  // 직전 초점 그룹. 초점을 푸는 동안 위성이 이 그룹으로 접혀 들어간다.
  previousKey: string | null = null;
  // 카메라 전환 진행도 0~1.
  t = 1;
  // 초점 강도 0~1. 초점이 잡힐수록 1, 풀릴수록 0.
  weight = 0;

  begin(key: string | null): void {
    this.previousKey = this.key;
    this.key = key;
  }

  sync(t: number, weight: number): void {
    this.t = t;
    this.weight = weight;
  }
}

// 이번 프레임에 소비한 lifecycle 이벤트. 대부분의 프레임에서 비어 있다.
export class FrameEvents {
  list: readonly LifecycleEvent[] = [];

  replace(events: readonly LifecycleEvent[]): void {
    this.list = events;
  }
}

// 지금 호버 중인 대상. SceneRoot 의 React 상태를 프레임 루프가 읽을 수 있게 비춘다
// (M7 flow 강조). 값은 메서드로만 바꾼다.
export class HoverFrame {
  current: Hovered = null;

  set(hovered: Hovered): void {
    this.current = hovered;
  }
}

// Satellites 가 매 프레임 채운다. 툴팁과 입자가 위성 위치를 읽는다.
export interface SatelliteView {
  position: Vec3;
  radius: number;
  child: ChildProcess;
  entry: PresenceEntry<ChildProcess>;
  account: ProcessGroup['account'];
}

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: OrbitLayout;
  presence: PresenceTracker<InterpolatedGroup>;
  events: FrameEvents;
  focus: FocusFrame;
  hover: HoverFrame;
  satellites: Map<number, SatelliteView>;
  setHovered: (update: (current: Hovered) => Hovered) => void;
}

export const SceneContext = createContext<SceneContextValue | null>(null);

export function useSceneContext(): SceneContextValue {
  const value = useContext(SceneContext);
  if (value === null) {
    throw new Error('useSceneContext must be used inside <SceneRoot>');
  }
  return value;
}
```

- [ ] **Step 2: `web/src/scene/FlowStreams.tsx` 전체 교체**

```tsx
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
import { dimFor } from './interaction';
import { useSceneContext, type Hovered } from './sceneContext';

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

// 호버한 그룹·코어와 이어진 선만 밝다. 호버가 없거나 위성·별이면 전부 밝다.
function highlightFor(hovered: Hovered, edge: FlowEdge, coreIndex: number): number {
  if (hovered === null || hovered.kind === 'satellite' || hovered.kind === 'system') {
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
  // 이번 프레임에 남아 있는 선의 key. 위상 정리에 쓴다 — 프레임마다 비우고 다시 채운다.
  const edgeKeys = useRef(new Set<string>());

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
    edgeKeys.current.clear();
    for (const edge of edges) {
      edgeKeys.current.add(edge.key);
    }
    for (const key of phases.current.keys()) {
      if (!edgeKeys.current.has(key)) {
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
      const lineAlpha = Math.min(1, CLARITY[edge.source] * edge.intensity * (0.25 + 1.6 * edge.weight)) * shade;

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
```

- [ ] **Step 3: `web/src/scene/SystemStar.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import { AdditiveBlending, Color, SRGBColorSpace, type MeshBasicMaterial } from 'three';

import { STAR_RADIUS, starGain } from '../visual/solar';
import { DIM_DEPTH } from './interaction';
import { useSceneContext } from './sceneContext';

// 따뜻한 흰색 (sRGB).
const STAR_RGB: [number, number, number] = [1.0, 0.86, 0.62];
const HALO_SCALE = 1.6;
const HALO_OPACITY = 0.18;

// 태양계형 배치 스펙 6절. 궤도의 중심. 크기는 고정이고 밝기는 시스템 전체 CPU 사용률을
// 따른다. 초점이 잡히면 다른 천체처럼 어두워진다. 호버하면 시스템 요약 툴팁이 뜬다 (이름표·별
// 정보 스펙 3절). 클릭은 아무것도 하지 않는다. 별이 호버를 받아야 별 뒤의 안쪽 궤도 천체가
// 대신 잡히지 않는다.
export function SystemStar() {
  const { cache, focus, setHovered } = useSceneContext();
  const body = useRef<MeshBasicMaterial>(null);
  const halo = useRef<MeshBasicMaterial>(null);

  useFrame(() => {
    if (body.current === null || halo.current === null) {
      return;
    }
    const dim = 1 - DIM_DEPTH * focus.weight;
    const gain = starGain(cache.snapshot?.system.cpu_pct ?? null) * dim;
    body.current.color.setRGB(STAR_RGB[0], STAR_RGB[1], STAR_RGB[2], SRGBColorSpace).multiplyScalar(gain);
    halo.current.opacity = HALO_OPACITY * dim;
  });

  return (
    <group>
      <mesh
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => ({ kind: 'system' }));
        }}
        onPointerOut={() => setHovered((current) => (current?.kind === 'system' ? null : current))}
      >
        <sphereGeometry args={[STAR_RADIUS, 48, 48]} />
        <meshBasicMaterial ref={body} color={new Color(1, 1, 1)} />
      </mesh>
      <mesh raycast={() => null} scale={HALO_SCALE}>
        <sphereGeometry args={[STAR_RADIUS, 32, 32]} />
        <meshBasicMaterial
          ref={halo}
          color="#ffd9a0"
          transparent
          opacity={HALO_OPACITY}
          depthWrite={false}
          blending={AdditiveBlending}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 4: `web/src/scene/Tooltip.tsx` 전체 교체**

```tsx
import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import type { Group } from 'three';

import { formatMb, formatPct } from '../dashboard/format';
import { coreLoad, orbRadius } from '../visual/coreMapping';
import { corePosition } from '../visual/coreRing';
import { systemDetail } from '../visual/labels';
import { floatingPosition, type Vec3 } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { STAR_RADIUS } from '../visual/solar';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext, type Hovered, type SceneContextValue } from './sceneContext';

interface Label {
  position: Vec3;
  radius: number;
  title: string;
  detail: string;
}

// 숫자는 대시보드와 같은 formatMb·formatPct 로 쓴다 — 두 화면이 일치해야 한다.
function labelFor(target: NonNullable<Hovered>, context: SceneContextValue): Label | null {
  const { cache, layout, presence, satellites } = context;
  if (target.kind === 'system') {
    const system = cache.snapshot?.system;
    if (system === undefined) {
      return null;
    }
    return {
      position: { x: 0, y: 0, z: 0 },
      radius: STAR_RADIUS,
      title: 'System',
      detail: systemDetail(system),
    };
  }
  if (target.kind === 'core') {
    const cores = cache.snapshot?.cores;
    const core = cores?.[target.index];
    if (cores === undefined || core === undefined) {
      return null;
    }
    return {
      position: corePosition(target.index, cores.length),
      radius: orbRadius(coreLoad(core.pct)),
      title: `CPU ${core.id}`,
      detail: `${formatPct(core.pct)}%`,
    };
  }
  if (target.kind === 'satellite') {
    const view = satellites.get(target.pid);
    if (view === undefined) {
      return null;
    }
    return {
      position: view.position,
      radius: view.radius,
      title: `${view.child.name} · pid ${view.child.pid}`,
      detail: `${formatMb(view.child.mem_mb)} MB · CPU ${formatPct(view.child.cpu_pct)}%`,
    };
  }
  const entry = presence.get(target.key);
  const position =
    cache.timeSec === null ? undefined : floatingPosition(layout, target.key, cache.timeSec);
  // 떠나는 중인 천체의 값은 고정된 옛 값이다. 보여 주지 않는다.
  if (
    entry === undefined ||
    position === undefined ||
    entry.phase === 'fading-out' ||
    entry.phase === 'collapsing'
  ) {
    return null;
  }
  const group = entry.value;
  return {
    position,
    radius: radiusFor(group.mem_mb),
    title: group.name,
    detail: `${formatMb(group.mem_mb)} MB · CPU ${formatPct(group.cpu_pct)}%`,
  };
}

interface Props {
  target: NonNullable<Hovered>;
}

// 호버한 천체나 위성 위에 이름·메모리·CPU 를 띄운다. 값은 보간된 프레임
// 값이므로 React 상태를 거치지 않고 DOM 을 직접 바꾼다.
// anchor 를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저
// 돌아야 한 프레임 지연이나 원점 깜빡임이 없다 (framePriority 참조).
export function Tooltip({ target }: Props) {
  const context = useSceneContext();
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);
  const name = useRef<HTMLDivElement>(null);
  const detail = useRef<HTMLDivElement>(null);

  useFrame(() => {
    if (
      anchor.current === null ||
      box.current === null ||
      name.current === null ||
      detail.current === null
    ) {
      return;
    }
    const label = labelFor(target, context);
    // Html 은 DOM 이라 group.visible 로는 숨겨지지 않는다. DOM 쪽을 직접 숨긴다.
    if (label === null) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(
      label.position.x,
      label.position.y + label.radius * 1.2,
      label.position.z,
    );
    name.current.textContent = label.title;
    detail.current.textContent = label.detail;
  }, FRAME_PRIORITY.tooltip);

  return (
    <group ref={anchor}>
      <Html center style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-tooltip" style={{ display: 'none' }}>
          <div ref={name} className="universe-tooltip-name" />
          <div ref={detail} />
        </div>
      </Html>
    </group>
  );
}
```

- [ ] **Step 5: `web/src/scene/BodyLabels.tsx`**

```tsx
import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef, useState } from 'react';
import type { Group } from 'three';

import { floatingPosition } from '../visual/layout';
import { labelKeys } from '../visual/labels';
import { radiusFor } from '../visual/mapping';
import { FRAME_PRIORITY } from './framePriority';
import { useSceneContext } from './sceneContext';

// 초점이 이만큼 이상 잡히면 이름표를 숨긴다. 위성과 패널이 그 역할을 한다.
const HIDE_WEIGHT = 0.5;

// 이름표·별 정보 스펙 4절. 가장 안쪽 궤도의 큰 천체 이름을 항상 보여 준다.
// 대상 목록이 바뀔 때만 React 상태를 갱신한다 — 궤도 안의 자리가 key 순으로 고정되어
// 있어 목록은 그룹이 들고 날 때만 바뀐다.
export function BodyLabels() {
  const { layout } = useSceneContext();
  const [keys, setKeys] = useState<string[]>([]);
  const signature = useRef('');

  useFrame(() => {
    const next = labelKeys(layout.plan());
    const nextSignature = next.join('\u0000');
    if (nextSignature !== signature.current) {
      signature.current = nextSignature;
      setKeys(next);
    }
  }, FRAME_PRIORITY.tooltip);

  return (
    <>
      {keys.map((key) => (
        <BodyLabel key={key} nodeKey={key} />
      ))}
    </>
  );
}

interface Props {
  nodeKey: string;
}

// 앵커를 옮기는 이 useFrame 은 drei Html 의 투영(우선순위 0)보다 먼저 돌아야 한 프레임
// 지연이 없다 (Tooltip 과 같은 이유, framePriority 참조). 글자는 DOM 에 직접 쓴다.
function BodyLabel({ nodeKey }: Props) {
  const { cache, layout, presence, focus, hover } = useSceneContext();
  const anchor = useRef<Group>(null);
  const box = useRef<HTMLDivElement>(null);

  useFrame(() => {
    if (anchor.current === null || box.current === null) {
      return;
    }
    const entry = presence.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    const hovered = hover.current;
    const hide =
      entry === undefined ||
      position === undefined ||
      entry.phase === 'fading-out' ||
      entry.phase === 'collapsing' ||
      focus.weight > HIDE_WEIGHT ||
      // 호버 중인 천체는 툴팁이 이름을 보여 준다.
      (hovered !== null && hovered.kind === 'group' && hovered.key === nodeKey);
    if (hide) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(
      position.x,
      position.y + radiusFor(entry.value.mem_mb) * 1.25,
      position.z,
    );
    if (box.current.textContent !== entry.value.name) {
      box.current.textContent = entry.value.name;
    }
  }, FRAME_PRIORITY.tooltip);

  return (
    <group ref={anchor}>
      <Html center style={{ pointerEvents: 'none' }}>
        <div ref={box} className="universe-label" style={{ display: 'none' }} />
      </Html>
    </group>
  );
}
```

- [ ] **Step 6: `web/src/scene/SceneRoot.tsx` 전체 교체**

```tsx
import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
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
import { OrbitLayout } from '../visual/layout';
import { LifecycleConsumer, type LifecycleEvent } from '../visual/lifecycleEvents';
import { colorFor, radiusFor } from '../visual/mapping';
import { PresenceTracker } from '../visual/presence';
import { AmbientDust } from './AmbientDust';
import { BodyLabels } from './BodyLabels';
import { CameraRig } from './CameraRig';
import { CoreRing } from './CoreRing';
import { CoreSparks } from './CoreSparks';
import { FlowStreams } from './FlowStreams';
import { useFocusStore } from './focusStore';
import { FRAME_PRIORITY } from './framePriority';
import { nodeIdsOf, parseNodeId } from './nodeList';
import { OrbitRings } from './OrbitRings';
import { Particles } from './Particles';
import { PostEffects } from './PostEffects';
import { ProcessNode } from './ProcessNode';
import { Satellites } from './Satellites';
import { SystemStar } from './SystemStar';
import {
  FocusFrame,
  HoverFrame,
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
  // 마지막으로 React 에 넘긴 존재 추적기 version.
  const shownVersion = useRef(-1);
  // 마지막으로 본 세션. null 프레임을 못 보고 세션이 바뀌어도 장면을 비우기 위해.
  const lastSession = useRef<string | null>(useSnapshotStore.getState().session);

  const context = useMemo<SceneContextValue>(
    () => ({
      cache: createFrameCache(),
      layout: new OrbitLayout(),
      presence: new PresenceTracker<InterpolatedGroup>(),
      events: new FrameEvents(),
      focus: new FocusFrame(),
      hover: new HoverFrame(),
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
        // 레이아웃은 일부러 비우지 않는다. 엔진이 재시작해도 대부분의 name:pid key 가
        // 살아남으므로 천체가 있던 자리에 그대로 있다 (null 프레임 경로와 다른 점).
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

    // 노드 목록은 항목이 추가·삭제됐을 때만 다시 만든다 (M6 스펙 10절).
    if (presence.version !== shownVersion.current) {
      shownVersion.current = presence.version;
      setNodeIds(nodeIdsOf(entries));
    }
  }, FRAME_PRIORITY.scene);

  const keys = nodeIds.map((id) => parseNodeId(id).key);
  // 호버 중이던 천체가 사라지면 R3F 가 onPointerOut 없이 오브젝트를 지운다.
  // 지금도 있는 그룹일 때만 그룹 툴팁을 그린다. 위성·코어 툴팁은 대상이 없으면
  // 스스로 숨는다.
  const liveHovered =
    hovered !== null && (hovered.kind !== 'group' || keys.includes(hovered.key)) ? hovered : null;

  // 호버는 React 상태다. flow 강조가 프레임 루프에서 읽을 수 있게, 아직 마운트된
  // (살아 있는) 호버만 비춘다.
  useEffect(() => {
    context.hover.set(liveHovered);
  }, [context, liveHovered]);

  return (
    <SceneContext.Provider value={context}>
      <CameraRig />
      <SystemStar />
      <OrbitRings />
      <BodyLabels />
      {nodeIds.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      <Satellites />
      <CoreRing />
      <CoreSparks />
      <AmbientDust />
      <FlowStreams />
      <PostEffects />
      <Particles pool={pool} />
      {liveHovered !== null && <Tooltip target={liveHovered} />}
    </SceneContext.Provider>
  );
}
```

- [ ] **Step 7: `web/src/shell/shell.css` 끝에 붙인다** (Task 2 에서 붙인 블록 뒤에 빈 줄 하나, 그다음 이 블록)

```css
/* 이름표·별 정보 스펙 4절. 큰 천체의 이름표. */
.universe-label {
  color: #b8c2cf;
  font-size: 11px;
  white-space: nowrap;
  text-shadow: 0 0 4px #03040a, 0 0 2px #03040a;
}
```

- [ ] **Step 8: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- `Tests  296 passed (296)`, `act(`·key 경고 없음
- typecheck 출력 없음
- lint 오류 0, 경고는 기존 `src/main.tsx` 1개뿐
- build 성공, `index-*.js` 약 1.48 MB

- [ ] **Step 9: 브라우저 확인은 하지 않는다**

`pulse-engine` 도 dev 서버도 띄우지 않는다. 컨트롤러가 확인한다. 보고서에 건너뛰었다고 적는다.

- [ ] **Step 10: 커밋**

```
git add src/scene/BodyLabels.tsx src/scene/sceneContext.ts src/scene/FlowStreams.tsx src/scene/SystemStar.tsx src/scene/Tooltip.tsx src/scene/SceneRoot.tsx src/shell/shell.css
git commit -m "feat(web): show the system summary on the star and always label the largest bodies"
```

---

## 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`(296), `npm run typecheck`, `npm run lint`(기존 경고 1개), `npm run build` 통과.
- [ ] `web/src/visual/orbits.ts` 가 바뀌지 않았다.
- [ ] 브라우저 (엔진 + dev 서버 또는 `run.bat`):
  - 가운데 별에 올리면 `System` 툴팁에 CPU·메모리(GB)·프로세스·스레드 수가 뜨고, 별 뒤의 안쪽 천체가 대신 잡히지 않는다.
  - 안쪽 궤도의 큰 천체 이름이 항상 보이고 공전을 따라간다. 그 천체에 올리면 이름표는 사라지고 툴팁이 뜬다. 초점을 잡으면 이름표가 사라진다.
  - 오른쪽 아래에 범례가 펼쳐져 있고 `H` 로 접히고 펴진다. 새로 고침해도 접은 상태가 유지된다.
  - 콘솔 오류 없음.
