# M4 — R3F 장면과 Process Node Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 브라우저를 열면 프로세스 그룹 40개가 메모리만큼 크고, CPU만큼 빠르게 숨 쉬고 밝게 빛나는 천체로 떠 있다. `D` 키로 M3 대시보드와 오간다.

**Architecture:** 매핑·배치·프레임 캐시는 `web/src/visual/` 의 순수 TypeScript 모듈이다 (React·three·zustand 금지, node 환경에서 테스트). `web/src/scene/` 의 R3F 컴포넌트는 장면 루트가 프레임당 한 번 `sample()` 을 호출해 `FrameCache` 에 채우고, 각 노드는 `useFrame` 에서 key 로 읽어 ref 를 직접 바꾼다 — 프레임 값은 React 상태를 거치지 않는다. `web/src/shell/` 이 URL 해시로 우주와 대시보드 중 하나만 마운트한다.

**Tech Stack:** three 0.186 / @react-three/fiber 9.8 / @react-three/drei 10 / React 19 / Zustand 5 / Vitest 5

## Global Constraints

- 스펙 원문: `docs/superpowers/specs/2026-09-28-m4-universe-scene-design.md`. 계약서: `docs/superpowers/specs/2026-09-22-pulse-universe-contract-design.md`.
- 작업 브랜치는 `feature/m4-universe-scene` 이다. `engine/` 은 건드리지 않는다.
- `cpu_pct` 계열은 `number | null` 이다. **`null`(모름)과 `0`(측정된 0)을 절대 같게 취급하지 않는다.** `activityFor(null)` 은 `null` 이다.
- `web/src/visual/**` 는 `react`, `react-dom`, `three`, `@react-three/*`, `zustand`, `snapshotStore`, `stream/` 을 import 하지 않는다. `protocol/` 과 `state/interpolator` 의 **타입**만 쓴다.
- `web/src/scene/**` 와 `web/src/dashboard/**` 는 `stream/` 을 import 하지 않는다.
- 프레임 값(보간된 cpu·mem, 위치, 맥박)은 React 상태에 넣지 않는다. `useFrame` 에서 ref 로 바꾼다.
- 시계는 `performance.now()` 다. R3F 의 `state.clock` 이나 `useFrame` 의 `delta` 를 쓰지 않는다 — `arrivedAt` 이 `performance.now()` 로 찍힌다.
- 이 Task 들에서 **GSAP, 포스트프로세싱, 셰이더, InstancedMesh 를 쓰지 않는다** (M5~M9).
- drei 의 `Text`/`Text3D` 를 쓰지 않는다 — 기본 폰트를 CDN 에서 받는다. 글자는 `Html` 툴팁 하나뿐이다.
- 테스트 출력에 React key 경고나 `act()` 경고가 남으면 안 된다.
- **충돌·abort·행(hang)·테스트 보고 없는 비정상 종료는 BLOCKED 로 보고한다.** 파라미터·sleep·타임아웃을 바꿔 피해 가지 않는다. 재현 명령을 함께 적는다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` 을 붙인다.

### 환경

```
node v24.12.0   npm 11.6.2   (PATH 에 있음)
작업 디렉터리: C:\dev\pulse-uni\web
```

명령은 모두 `C:\dev\pulse-uni\web` 에서 실행한다.

## File Structure

```
web/
  package.json               + three, @react-three/fiber, @react-three/drei, @types/three, "typecheck" 스크립트
  tsconfig.test.json         신규. src + tests 를 타입체크 (M3 에서 미룬 항목)
  vite.config.ts             node 프로젝트에 tests/visual 추가, chunkSizeWarningLimit
  .oxlintrc.json             visual/ 순수성, scene/ ↛ stream/
  src/
    visual/                  순수 TS (node 환경 테스트)
      hash.ts                key → [0,1)
      mapping.ts             radius, activity, pulse, glow, color
      layout.ts              LayoutSim, floatOffset, floatingPosition
      frameCache.ts          프레임당 보간 결과를 key 로 조회
    scene/                   R3F
      sceneContext.ts
      nodeList.ts            스토어 → 노드 id 배열
      ProcessNode.tsx
      Tooltip.tsx
      SceneRoot.tsx
      Universe.tsx
    shell/
      view.ts                해시 ↔ 화면
      Shell.tsx
      shell.css
    main.tsx                 App → Shell
  tests/
    visual/mapping.test.ts   hash + mapping
    visual/layout.test.ts
    visual/frameCache.test.ts
    shell.test.tsx
    nodeList.test.ts
  README.md                  레이어 구조 갱신
```

---

## Task 1: 의존성, 테스트 타입체크, lint 경계

**Files:**
- Modify: `web/package.json`, `web/package-lock.json` (npm 이 갱신)
- Create: `web/tsconfig.test.json`
- Modify: `web/vite.config.ts`
- Modify: `web/.oxlintrc.json`

**Interfaces:**
- Produces: `npm run typecheck` (앱 + 테스트 타입체크). vitest 프로젝트 이름 `node` (기존 `schema` 에서 개명) 가 `tests/schema.test.ts` 와 `tests/visual/**/*.test.ts` 를 node 환경에서 돌린다. 이후 Task 는 `npx vitest run --project node ...` 로 순수 모듈 테스트를 돌린다.

- [ ] **Step 1: 의존성 설치**

```
npm install three@^0.186.0 @react-three/fiber@^9.8.1 @react-three/drei@^10.7.9
npm install -D @types/three@^0.186.0
```

Expected: `found 0 vulnerabilities`. `package.json` 의 dependencies 에 `three`, `@react-three/fiber`, `@react-three/drei` 가, devDependencies 에 `@types/three` 가 생긴다.

- [ ] **Step 2: `package.json` 에 typecheck 스크립트 추가**

`"scripts"` 의 `"build"` 줄 바로 아래에 추가한다:

```json
    "typecheck": "tsc -b && tsc -p tsconfig.test.json",
```

- [ ] **Step 3: `web/tsconfig.test.json` 작성**

```json
{
  "extends": "./tsconfig.app.json",
  "compilerOptions": {
    "tsBuildInfoFile": "./node_modules/.tmp/tsconfig.test.tsbuildinfo",
    "types": ["vite/client", "vitest/globals", "node"],
    "resolveJsonModule": true
  },
  "include": ["src", "tests"]
}
```

`tsconfig.app.json` 은 `src` 만 포함하므로 지금까지 테스트 파일은 타입체크되지 않았다 (vitest 는 타입을 무시하고 변환만 한다).

- [ ] **Step 4: typecheck 가 현재 코드에서 통과하는지 확인**

Run: `npm run typecheck`
Expected: 출력 없이 종료 코드 0.

- [ ] **Step 5: `web/vite.config.ts` 전체를 다음으로 교체**

```ts
import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  build: {
    // three + R3F + drei 가 한 청크에 1.26 MB(gzip 349 KB)가 된다. 이 앱은 엔진이
    // 로컬 디스크에서 서빙하므로 크기가 로딩을 막지 않는다. 코드 분할은 M9 에서
    // 측정한 뒤 판단한다.
    chunkSizeWarningLimit: 1600,
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./tests/setup.ts'],
    projects: [
      {
        extends: true,
        test: {
          name: 'node',
          environment: 'node',
          // 순수 모듈의 테스트. DOM 이 없는 환경에서 돌아야 순수하다는 것이 증명된다.
          include: ['tests/schema.test.ts', 'tests/visual/**/*.test.ts'],
        },
      },
      {
        extends: true,
        test: {
          name: 'jsdom',
          environment: 'jsdom',
          include: ['tests/**/*.test.{ts,tsx}'],
          exclude: ['tests/schema.test.ts', 'tests/visual/**'],
        },
      },
    ],
  },
});
```

- [ ] **Step 6: `web/.oxlintrc.json` 전체를 다음으로 교체**

```json
{
  "$schema": "./node_modules/oxlint/configuration_schema.json",
  "plugins": ["react", "typescript", "oxc"],
  "rules": {
    "react/rules-of-hooks": "error",
    "react/only-export-components": ["warn", { "allowConstantExport": true }]
  },
  "overrides": [
    {
      "files": ["src/dashboard/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "patterns": [
              {
                "group": ["*stream/*", "*/stream", "**/stream/**"],
                "message": "dashboard/ reads only state/, never stream/ directly."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/scene/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "patterns": [
              {
                "group": ["*stream/*", "*/stream", "**/stream/**"],
                "message": "scene/ reads only state/, never stream/ directly."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/visual/**"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "paths": [
              { "name": "react", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "react-dom", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "three", "message": "visual/ is pure: no react, react-dom, three or zustand." },
              { "name": "zustand", "message": "visual/ is pure: no react, react-dom, three or zustand." }
            ],
            "patterns": [
              {
                "group": ["@react-three/*", "*snapshotStore*", "*stream/*", "**/stream/**"],
                "message": "visual/ is pure: it may import types from state/interpolator and protocol/ only."
              }
            ]
          }
        ]
      }
    },
    {
      "files": ["src/state/interpolator.ts", "src/stream/SystemStream.ts"],
      "rules": {
        "no-restricted-imports": [
          "error",
          {
            "paths": [
              {
                "name": "react",
                "message": "This file must not import react or zustand."
              },
              {
                "name": "zustand",
                "message": "This file must not import react or zustand."
              }
            ]
          }
        ]
      }
    }
  ]
}
```

- [ ] **Step 7: lint 경계가 실제로 막는지 확인**

임시 파일로 확인하고 지운다 (PowerShell):

```powershell
New-Item -ItemType Directory -Force src\visual, src\scene | Out-Null
Set-Content -Encoding utf8 src\visual\_probe.ts "import 'three';"
npx oxlint src/visual/_probe.ts
Set-Content -Encoding utf8 src\visual\_probe.ts "import '@react-three/fiber';"
npx oxlint src/visual/_probe.ts
Set-Content -Encoding utf8 src\scene\_probe.ts "import { SystemStream } from '../stream/SystemStream'; void SystemStream;"
npx oxlint src/scene/_probe.ts
Remove-Item src\visual\_probe.ts, src\scene\_probe.ts
```

Expected: 세 번 모두 `error eslint(no-restricted-imports)` 가 나온다. 각각 `'three' import is restricted`, `'@react-three/fiber' import is restricted from being used by a pattern`, `'../stream/SystemStream' import is restricted from being used by a pattern`. 빈 디렉터리 `src/visual`, `src/scene` 은 남아도 된다 (git 은 빈 디렉터리를 추적하지 않는다).

- [ ] **Step 8: 전체 확인**

Run: `npm test; npm run lint; npm run build`
Expected:
- `npm test`: `Tests  74 passed (74)`. 프로젝트 이름이 `|node|` 와 `|jsdom|` 으로 나온다.
- `npm run lint`: 경고 1개 — `src/main.tsx` 의 `react(only-export-components)`. M3 부터 있던 것이다. 오류 0.
- `npm run build`: 성공. 청크 크기 경고 없음 (아직 three 를 import 하는 코드가 없다).

- [ ] **Step 9: 커밋**

```
git add package.json package-lock.json tsconfig.test.json vite.config.ts .oxlintrc.json
git commit -m "build(web): add three and R3F, type-check tests, fence visual/ and scene/"
```

---

## Task 2: 해시와 시각 매핑

**Files:**
- Create: `web/src/visual/hash.ts`
- Create: `web/src/visual/mapping.ts`
- Test: `web/tests/visual/mapping.test.ts`

**Interfaces:**
- Consumes: `ProcessGroup` 타입 (`web/src/protocol/schema.ts`, `account: 'user' | 'system'`).
- Produces:
  - `hash01(key: string, salt?: number): number` — [0,1), 결정적.
  - `radiusFor(memMb: number): number`, `MIN_RADIUS = 0.3`, `RADIUS_SCALE = 0.25`
  - `activityFor(cpuPct: number | null, coreCount: number): number | null`, `SATURATION_CORES = 4`
  - `interface PulseParams { freqHz: number; amplitude: number }`, `pulseFor(activity: number | null): PulseParams`
  - `advancePhase(phase: number, freqHz: number, dtSec: number): number`
  - `interface Glow { emissiveIntensity: number; haloOpacity: number }`, `glowFor(activity: number | null): Glow`, `HALO_SCALE = 1.35`
  - `interface Hsl { h: number; s: number; l: number }` (h 는 도 단위), `colorFor(account: ProcessGroup['account'], key: string): Hsl`, `HUE_USER = 185`, `HUE_SYSTEM = 270`, `HUE_JITTER = 12`

- [ ] **Step 1: 실패하는 테스트 작성 — `web/tests/visual/mapping.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import { hash01 } from '../../src/visual/hash';
import {
  HUE_JITTER,
  HUE_SYSTEM,
  HUE_USER,
  MIN_RADIUS,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../../src/visual/mapping';

describe('hash01', () => {
  it('returns the same value for the same key and salt', () => {
    expect(hash01('whale.exe:1234', 3)).toBe(hash01('whale.exe:1234', 3));
  });

  it('stays in [0, 1)', () => {
    for (let i = 0; i < 2000; i += 1) {
      const value = hash01(`app.exe:${i}`, i % 7);
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThan(1);
    }
  });

  it('gives different values for different salts of the same key', () => {
    expect(hash01('app.exe:100', 11)).not.toBe(hash01('app.exe:100', 12));
  });

  it('spreads keys that differ only in their last digit', () => {
    // FNV-1a 만 쓰면 이런 key 들이 좁은 구간에 뭉친다.
    const values = Array.from({ length: 1000 }, (_, i) => hash01(`app.exe:${1000 + i}`));
    const mean = values.reduce((sum, v) => sum + v, 0) / values.length;
    const buckets = new Array<number>(10).fill(0);
    for (const v of values) {
      buckets[Math.floor(v * 10)] += 1;
    }

    expect(mean).toBeGreaterThan(0.45);
    expect(mean).toBeLessThan(0.55);
    for (const count of buckets) {
      expect(count).toBeGreaterThan(60);
    }
  });
});

describe('radiusFor', () => {
  it('matches the spec table (r = 0.25 · ∛mem)', () => {
    expect(radiusFor(4237)).toBeCloseTo(4.05, 2);
    expect(radiusFor(1221)).toBeCloseTo(2.67, 2);
    expect(radiusFor(92)).toBeCloseTo(1.13, 2);
    expect(radiusFor(45)).toBeCloseTo(0.89, 2);
  });

  it('never goes below the minimum radius', () => {
    expect(radiusFor(0)).toBe(MIN_RADIUS);
    expect(radiusFor(0.5)).toBe(MIN_RADIUS);
    expect(radiusFor(-10)).toBe(MIN_RADIUS);
  });

  it('is absolute: doubling memory doubles volume no matter what else is on screen', () => {
    const ratio = radiusFor(2000) / radiusFor(1000);
    expect(ratio ** 3).toBeCloseTo(2, 6);
  });
});

describe('activityFor', () => {
  it('matches the spec table on a 28-core machine', () => {
    expect(activityFor(0.1, 28)).toBeCloseTo(0.017, 3);
    // 한 코어 꽉 참 = 100 / 28 %.
    expect(activityFor(100 / 28, 28)).toBeCloseTo(0.43, 2);
    expect(activityFor(200 / 28, 28)).toBeCloseTo(0.68, 2);
    expect(activityFor(400 / 28, 28)).toBeCloseTo(1, 6);
  });

  it('saturates at 1', () => {
    expect(activityFor(100, 28)).toBe(1);
  });

  it('keeps null as null instead of turning it into 0', () => {
    expect(activityFor(null, 28)).toBeNull();
  });

  it('keeps a measured 0 as 0', () => {
    expect(activityFor(0, 28)).toBe(0);
  });

  it('treats a zero core count as one core rather than dividing it away', () => {
    expect(activityFor(100, 0)).toBeCloseTo(activityFor(100, 1)!, 6);
  });
});

describe('pulseFor', () => {
  it('breathes slowly at rest', () => {
    expect(pulseFor(0)).toEqual({ freqHz: 0.15, amplitude: 0.02 });
  });

  it('draws null with the resting breath', () => {
    expect(pulseFor(null)).toEqual(pulseFor(0));
  });

  it('beats faster and deeper at full activity', () => {
    const full = pulseFor(1);
    expect(full.freqHz).toBeCloseTo(1.35, 6);
    expect(full.amplitude).toBeCloseTo(0.08, 6);
  });
});

describe('advancePhase', () => {
  it('accumulates 2π·freq·dt', () => {
    expect(advancePhase(0, 0.5, 0.5)).toBeCloseTo(Math.PI / 2, 6);
  });

  it('continues from the current phase when the frequency changes', () => {
    // 주파수가 바뀌어도 위상은 이어진다 — sin(2π·f·t) 방식이라면 튄다.
    const before = advancePhase(1, 0.2, 0.1);
    const after = advancePhase(before, 1.2, 0);
    expect(after).toBe(before);
  });

  it('wraps into [0, 2π)', () => {
    const phase = advancePhase(6, 1, 1);
    expect(phase).toBeGreaterThanOrEqual(0);
    expect(phase).toBeLessThan(2 * Math.PI);
  });

  it('ignores negative time', () => {
    expect(advancePhase(1, 1, -5)).toBe(1);
  });
});

describe('glowFor', () => {
  it('matches the spec at rest and at full activity', () => {
    expect(glowFor(0)).toEqual({ emissiveIntensity: 0.15, haloOpacity: 0.05 });
    const full = glowFor(1);
    expect(full.emissiveIntensity).toBeCloseTo(1.75, 6);
    expect(full.haloOpacity).toBeCloseTo(0.4, 6);
  });

  it('draws null with the resting glow', () => {
    expect(glowFor(null)).toEqual(glowFor(0));
  });
});

describe('colorFor', () => {
  it('keeps user and system hues in their own families', () => {
    for (let i = 0; i < 200; i += 1) {
      const key = `app.exe:${i}`;
      expect(Math.abs(colorFor('user', key).h - HUE_USER)).toBeLessThanOrEqual(HUE_JITTER);
      expect(Math.abs(colorFor('system', key).h - HUE_SYSTEM)).toBeLessThanOrEqual(HUE_JITTER);
    }
  });

  it('gives the same key the same color every time', () => {
    expect(colorFor('user', 'Code.exe:4321')).toEqual(colorFor('user', 'Code.exe:4321'));
  });

  it('varies the hue between keys of the same account', () => {
    const hues = new Set(
      Array.from({ length: 20 }, (_, i) => colorFor('user', `app.exe:${i}`).h.toFixed(3)),
    );
    expect(hues.size).toBeGreaterThan(15);
  });
});
```

- [ ] **Step 2: 실패 확인**

Run: `npx vitest run --project node tests/visual/mapping.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/hash"` (또는 `mapping`).

- [ ] **Step 3: `web/src/visual/hash.ts` 작성**

```ts
// key 를 [0, 1) 의 수로 바꾼다. 같은 key 는 언제나 같은 수가 되므로
// 새로고침해도 초기 배치·색조·위상이 같다. salt 를 바꾸면 같은 key 에서
// 서로 독립적인 수를 여러 개 뽑을 수 있다.
//
// FNV-1a 로 섞은 뒤 murmur3 의 마무리 단계로 비트를 한 번 더 흩는다.
// FNV-1a 만으로는 끝 글자만 다른 key(`app.exe:100`, `app.exe:101`)가
// 비슷한 수로 뭉친다.
export function hash01(key: string, salt = 0): number {
  let h = (0x811c9dc5 ^ salt) >>> 0;
  for (let i = 0; i < key.length; i += 1) {
    h ^= key.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  h ^= h >>> 16;
  h = Math.imul(h, 0x85ebca6b);
  h ^= h >>> 13;
  h = Math.imul(h, 0xc2b2ae35);
  h ^= h >>> 16;
  return (h >>> 0) / 0x100000000;
}
```

- [ ] **Step 4: `web/src/visual/mapping.ts` 작성**

```ts
import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from './hash';

// 스펙 5절. 모든 상수는 여기 한 곳에 있다 — 룩을 바꾸려면 이 파일만 고친다.

// 5.1 반지름. 부피가 메모리에 비례한다 (r ∝ ∛mem). 최댓값으로 정규화하지
// 않는다 — 가장 큰 프로세스가 사라져도 다른 천체의 크기가 튀지 않는다.
export const RADIUS_SCALE = 0.25;
export const MIN_RADIUS = 0.3;

export function radiusFor(memMb: number): number {
  return Math.max(MIN_RADIUS, RADIUS_SCALE * Math.cbrt(Math.max(0, memMb)));
}

// 5.2 활동도. cpu_pct 는 전체 코어 수로 나눈 값이라(28 코어에서 한 코어를
// 꽉 채워도 3.6%) 그대로 쓰면 모든 천체가 어둡다. 사용 중인 코어 개수로
// 환산한 뒤 로그로 누르고, SATURATION_CORES 개에서 1 이 된다.
export const SATURATION_CORES = 4;

export function activityFor(cpuPct: number | null, coreCount: number): number | null {
  // null 은 모름이다. 0 으로 바꾸지 않는다.
  if (cpuPct === null) {
    return null;
  }
  const cores = (Math.max(0, cpuPct) * Math.max(1, coreCount)) / 100;
  return Math.min(1, Math.log2(1 + cores) / Math.log2(1 + SATURATION_CORES));
}

// 5.3 맥박. 활동이 없어도 느리게 숨 쉰다. null 은 기본 호흡으로 그린다.
export interface PulseParams {
  freqHz: number;
  // 반지름 대비 비율.
  amplitude: number;
}

export function pulseFor(activity: number | null): PulseParams {
  const a = activity ?? 0;
  return { freqHz: 0.15 + 1.2 * a, amplitude: 0.02 + 0.06 * a };
}

// 위상은 누적한다. sin(2π·freq·t) 로 계산하면 freq 가 바뀌는 순간 위상이 튄다.
export function advancePhase(phase: number, freqHz: number, dtSec: number): number {
  return (phase + 2 * Math.PI * freqHz * Math.max(0, dtSec)) % (2 * Math.PI);
}

// 5.4 발광. 진짜 Bloom 은 M8 이다. 헤일로는 그때까지 발광을 읽게 해 주는 장치다.
export const HALO_SCALE = 1.35;

export interface Glow {
  emissiveIntensity: number;
  haloOpacity: number;
}

export function glowFor(activity: number | null): Glow {
  const a = activity ?? 0;
  return { emissiveIntensity: 0.15 + 1.6 * a, haloOpacity: 0.05 + 0.35 * a };
}

// 5.5 색. account 가 색 계열을, key 해시가 계열 안의 작은 흔들림을 정한다.
export const HUE_USER = 185;
export const HUE_SYSTEM = 270;
export const HUE_JITTER = 12;
const SALT_HUE = 1;

export interface Hsl {
  // 도 단위.
  h: number;
  // 0~1.
  s: number;
  l: number;
}

export function colorFor(account: ProcessGroup['account'], key: string): Hsl {
  const base = account === 'user' ? HUE_USER : HUE_SYSTEM;
  const jitter = (hash01(key, SALT_HUE) * 2 - 1) * HUE_JITTER;
  return { h: base + jitter, s: 0.65, l: 0.55 };
}
```

- [ ] **Step 5: 통과 확인**

Run: `npx vitest run --project node tests/visual/mapping.test.ts`
Expected: PASS, `Tests  24 passed (24)`.

- [ ] **Step 6: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: 테스트 전부 통과, typecheck 출력 없음, lint 오류 0 (기존 main.tsx 경고 1개).

- [ ] **Step 7: 커밋**

```
git add src/visual/hash.ts src/visual/mapping.ts tests/visual/mapping.test.ts
git commit -m "feat(web): map memory to radius and cpu to pulse and glow"
```

---

## Task 3: 배치 시뮬레이션

**Files:**
- Create: `web/src/visual/layout.ts`
- Test: `web/tests/visual/layout.test.ts`

**Interfaces:**
- Consumes: `hash01` (Task 2), `radiusFor` (Task 2, 테스트에서만), `SnapshotSchema` (`web/src/protocol/schema.ts`), 픽스처 `web/tests/fixtures/snapshot.json`.
- Produces:
  - `interface Vec3 { x: number; y: number; z: number }`
  - `interface LayoutNode { key: string; radius: number }`
  - `class LayoutSim { get size(): number; position(key: string): Vec3 | undefined; step(nodes: readonly LayoutNode[], dtSec: number): void }`
  - 상수 `FIXED_STEP = 1/60`, `MAX_DT = 1/30`, `SETTLE_STEPS = 600`
  - `floatOffset(key: string, timeSec: number): Vec3`
  - `floatingPosition(sim: LayoutSim, key: string, timeSec: number): Vec3 | undefined`

상수(`REPULSION 3`, `GRAVITY 0.05`, `COLLISION_STIFFNESS 8`, `DAMPING 0.9`)는 실제 픽스처 40개로 돌려 정한 값이다. 테스트가 실패하면 상수를 조정하지 말고 BLOCKED 로 보고한다 — 조정은 스펙의 결정이다.

- [ ] **Step 1: 실패하는 테스트 작성 — `web/tests/visual/layout.test.ts`**

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import {
  FIXED_STEP,
  LayoutSim,
  MAX_DT,
  floatOffset,
  floatingPosition,
  type LayoutNode,
  type Vec3,
} from '../../src/visual/layout';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처의 40 개 그룹. 반지름 0.89 ~ 4.05.
const nodes: LayoutNode[] = fixture.groups.map((group) => ({
  key: group.key,
  radius: radiusFor(group.mem_mb),
}));

function length(v: Vec3): number {
  return Math.hypot(v.x, v.y, v.z);
}

function positionsOf(sim: LayoutSim, list: readonly LayoutNode[]): Vec3[] {
  return list.map((node) => ({ ...sim.position(node.key)! }));
}

// 첫 호출의 사전 수렴 뒤에 10 초를 더 돌린다.
function settled(list: readonly LayoutNode[]): LayoutSim {
  const sim = new LayoutSim();
  for (let i = 0; i < 600; i += 1) {
    sim.step(list, FIXED_STEP);
  }
  return sim;
}

// 어떤 두 구도 서로를 파고들지 않는다. COLLISION_GAP 은 여유분이다.
function expectApart(sim: LayoutSim, list: readonly LayoutNode[]): void {
  const positions = positionsOf(sim, list);
  for (let i = 0; i < list.length; i += 1) {
    for (let j = i + 1; j < list.length; j += 1) {
      const d = length({
        x: positions[i].x - positions[j].x,
        y: positions[i].y - positions[j].y,
        z: positions[i].z - positions[j].z,
      });
      expect(d).toBeGreaterThan(list[i].radius + list[j].radius);
    }
  }
}

describe('LayoutSim', () => {
  it('keeps every pair of spheres apart once settled', () => {
    expectApart(settled(nodes), nodes);
  });

  it('keeps the centroid near the origin', () => {
    const positions = positionsOf(settled(nodes), nodes);
    const centroid = positions.reduce(
      (sum, p) => ({
        x: sum.x + p.x / positions.length,
        y: sum.y + p.y / positions.length,
        z: sum.z + p.z / positions.length,
      }),
      { x: 0, y: 0, z: 0 },
    );
    expect(length(centroid)).toBeLessThan(2);
  });

  it('pulls the ten largest bodies closer to the center than the ten smallest', () => {
    const sim = settled(nodes);
    const bySize = [...nodes].sort((a, b) => b.radius - a.radius);
    const meanDistance = (list: LayoutNode[]) =>
      list.reduce((sum, node) => sum + length(sim.position(node.key)!), 0) / list.length;

    expect(meanDistance(bySize.slice(0, 10))).toBeLessThan(meanDistance(bySize.slice(-10)));
  });

  it('is already settled after the very first step', () => {
    // 첫 화면이 한 점에서 퍼져 나오지 않는다 — 첫 호출이 SETTLE_STEPS 만큼 미리 돈다.
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expectApart(sim, nodes);
  });

  it('settles to the same shape on the first step regardless of the frame time', () => {
    // 새로고침해도 같은 모양 — 첫 호출은 dt 와 무관하다.
    const a = new LayoutSim();
    const b = new LayoutSim();
    a.step(nodes, 1 / 144);
    b.step(nodes, 1 / 24);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('advances at most MAX_DT per call', () => {
    // 백그라운드 탭에서 돌아온 5 초짜리 프레임을 한 번에 따라잡지 않는다.
    const a = settled(nodes);
    const b = settled(nodes);
    a.step(nodes, 5);
    b.step(nodes, MAX_DT);

    expect(positionsOf(a, nodes)).toEqual(positionsOf(b, nodes));
  });

  it('treats a NaN or negative frame time as no time at all', () => {
    const reference = settled(nodes);
    const sim = settled(nodes);
    sim.step(nodes, Number.NaN);
    sim.step(nodes, -1);
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));

    // 잘못된 dt 가 누적기를 오염시키면 이후 정상 프레임에서도 멈춰 버린다.
    for (let i = 0; i < 10; i += 1) {
      reference.step(nodes, FIXED_STEP);
      sim.step(nodes, FIXED_STEP);
    }
    expect(positionsOf(sim, nodes)).toEqual(positionsOf(reference, nodes));
  });

  it('produces identical positions for identical inputs', () => {
    expect(positionsOf(settled(nodes), nodes)).toEqual(positionsOf(settled(nodes), nodes));
  });

  it('adds bodies for new keys and forgets keys that disappear', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    expect(sim.size).toBe(40);

    const fewer = nodes.slice(0, 30);
    sim.step(fewer, FIXED_STEP);
    expect(sim.size).toBe(30);
    expect(sim.position(nodes[35].key)).toBeUndefined();

    const newcomer: LayoutNode = { key: 'newcomer.exe:9999', radius: 1 };
    sim.step([...fewer, newcomer], FIXED_STEP);
    expect(sim.size).toBe(31);
    expect(sim.position(newcomer.key)).toBeDefined();
  });

  it('stays finite and bounded under a long run of huge frame times', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    for (let i = 0; i < 200; i += 1) {
      sim.step(nodes, 5);
    }

    for (const p of positionsOf(sim, nodes)) {
      expect(Number.isFinite(p.x) && Number.isFinite(p.y) && Number.isFinite(p.z)).toBe(true);
      expect(length(p)).toBeLessThan(40);
    }
  });

  it('keeps drawing a lone body toward the center', () => {
    const lone: LayoutNode[] = [{ key: 'solo.exe:1', radius: 1 }];
    const sim = new LayoutSim();
    sim.step(lone, FIXED_STEP);
    const before = length(sim.position('solo.exe:1')!);
    for (let i = 0; i < 600; i += 1) {
      sim.step(lone, FIXED_STEP);
    }
    expect(length(sim.position('solo.exe:1')!)).toBeLessThan(before);
  });

  it('does nothing with an empty node list', () => {
    const sim = new LayoutSim();
    sim.step([], FIXED_STEP);
    expect(sim.size).toBe(0);
  });
});

describe('floatOffset', () => {
  it('stays within the float amplitude on every axis', () => {
    for (let t = 0; t < 30; t += 0.37) {
      const offset = floatOffset('app.exe:100', t);
      for (const value of [offset.x, offset.y, offset.z]) {
        expect(Math.abs(value)).toBeLessThanOrEqual(0.2 + 1e-9);
      }
    }
  });

  it('differs between keys at the same moment', () => {
    expect(floatOffset('a.exe:1', 3)).not.toEqual(floatOffset('b.exe:2', 3));
  });
});

describe('floatingPosition', () => {
  it('adds the float offset to the simulated position', () => {
    const sim = new LayoutSim();
    sim.step(nodes, FIXED_STEP);
    const key = nodes[0].key;
    const base = sim.position(key)!;
    const offset = floatOffset(key, 4);

    expect(floatingPosition(sim, key, 4)).toEqual({
      x: base.x + offset.x,
      y: base.y + offset.y,
      z: base.z + offset.z,
    });
  });

  it('returns undefined for an unknown key', () => {
    expect(floatingPosition(new LayoutSim(), 'nope', 0)).toBeUndefined();
  });
});
```

- [ ] **Step 2: 실패 확인**

Run: `npx vitest run --project node tests/visual/layout.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/layout"`.

- [ ] **Step 3: `web/src/visual/layout.ts` 작성**

```ts
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

// key 해시로 반지름 SPAWN_RADIUS 인 구 안의 한 점을 고른다.
function spawnPosition(key: string): Vec3 {
  const theta = 2 * Math.PI * hash01(key, SALT_X);
  const phi = Math.acos(2 * hash01(key, SALT_Y) - 1);
  const r = SPAWN_RADIUS * Math.cbrt(hash01(key, SALT_Z));
  return {
    x: r * Math.sin(phi) * Math.cos(theta),
    y: r * Math.cos(phi),
    z: r * Math.sin(phi) * Math.sin(theta),
  };
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
    this.sync(nodes);
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

  private sync(nodes: readonly LayoutNode[]): void {
    const present = new Set<string>();
    for (const node of nodes) {
      present.add(node.key);
      if (!this.bodies.has(node.key)) {
        this.bodies.set(node.key, {
          position: spawnPosition(node.key),
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
```

- [ ] **Step 4: 통과 확인**

Run: `npx vitest run --project node tests/visual/layout.test.ts`
Expected: PASS, `Tests  16 passed (16)`. 1초 이내.

- [ ] **Step 5: 테스트가 실제로 구분하는지 확인 (변형 검사)**

각 변형을 적용 → 테스트 실행 → 되돌리기. 되돌린 뒤 `git diff src/visual/layout.ts` 가 비어 있어야 한다 (파일이 아직 추적되지 않으므로 변형 전에 복사본을 만들어 두고 복원한다).

| 변형 | 실패해야 하는 테스트 |
|---|---|
| `COLLISION_STIFFNESS = 8` → `0` | `keeps every pair of spheres apart once settled` |
| `(0.5 + r * r)` → `(0.5)` | `pulls the ten largest bodies closer…` |
| `SETTLE_STEPS = 600` → `0` | `is already settled after the very first step` |
| `Math.min(Math.max(dtSec, 0), MAX_DT)` → `Math.max(dtSec, 0)` | `advances at most MAX_DT per call` |
| `Number.isFinite(dtSec) ? … : 0` 제거 | `treats a NaN or negative frame time as no time at all` |

하나라도 실패하지 않으면 테스트나 구현이 계획과 다른 것이다. 보고서에 표 형태로 결과를 적는다.

- [ ] **Step 6: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: 전부 통과, lint 오류 0.

- [ ] **Step 7: 커밋**

```
git add src/visual/layout.ts tests/visual/layout.test.ts
git commit -m "feat(web): lay out groups with a fixed-step force simulation"
```

---

## Task 4: 프레임 캐시

**Files:**
- Create: `web/src/visual/frameCache.ts`
- Test: `web/tests/visual/frameCache.test.ts`

**Interfaces:**
- Consumes: `InterpolatedGroup`, `InterpolatedSnapshot`, `sample` (`web/src/state/interpolator.ts`), `LayoutNode` (Task 3), `radiusFor` (Task 2).
- Produces:
  - `interface FrameCache { snapshot: InterpolatedSnapshot | null; byKey: Map<string, InterpolatedGroup>; coreCount: number; timeSec: number | null; dtSec: number }`
  - `createFrameCache(): FrameCache`
  - `updateFrameCache(cache: FrameCache, snapshot: InterpolatedSnapshot | null, nowMs: number): void` — 제자리 갱신
  - `layoutNodesFrom(cache: FrameCache): LayoutNode[]`

- [ ] **Step 1: 실패하는 테스트 작성 — `web/tests/visual/frameCache.test.ts`**

```ts
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { SnapshotSchema } from '../../src/protocol/schema';
import { sample } from '../../src/state/interpolator';
import {
  createFrameCache,
  layoutNodesFrom,
  updateFrameCache,
} from '../../src/visual/frameCache';
import { radiusFor } from '../../src/visual/mapping';

const fixturePath = fileURLToPath(new URL('../fixtures/snapshot.json', import.meta.url));
const fixture = SnapshotSchema.parse(JSON.parse(readFileSync(fixturePath, 'utf8')));

// 실제 픽스처를 보간기에 한 번 통과시킨 프레임.
const frame = sample({ previous: null, current: fixture, arrivedAt: 0, intervalMs: 1000 }, 0)!;

describe('frameCache', () => {
  it('looks up every group of the real fixture by key', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);

    expect(cache.byKey.size).toBe(fixture.groups.length);
    for (const group of fixture.groups) {
      expect(cache.byKey.get(group.key)?.name).toBe(group.name);
    }
  });

  it('takes the core count from the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    expect(cache.coreCount).toBe(fixture.cores.length);
  });

  it('never reports fewer than one core', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, { ...frame, cores: [] }, 1000);
    expect(cache.coreCount).toBe(1);
  });

  it('keeps a null cpu_pct as null', () => {
    const cache = createFrameCache();
    const withNull = {
      ...frame,
      groups: frame.groups.map((g, i) => (i === 0 ? { ...g, cpu_pct: null } : g)),
    };
    updateFrameCache(cache, withNull, 1000);
    expect(cache.byKey.get(frame.groups[0].key)?.cpu_pct).toBeNull();
  });

  it('empties the lookup when the snapshot goes away', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, null, 1016);

    expect(cache.snapshot).toBeNull();
    expect(cache.byKey.size).toBe(0);
  });

  it('drops groups that are no longer in the snapshot', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, { ...frame, groups: frame.groups.slice(0, 5) }, 1016);
    expect(cache.byKey.size).toBe(5);
  });

  it('reuses the same Map instead of allocating a new one each frame', () => {
    const cache = createFrameCache();
    const map = cache.byKey;
    updateFrameCache(cache, frame, 1000);
    updateFrameCache(cache, frame, 1016);
    expect(cache.byKey).toBe(map);
  });

  it('reports seconds and the gap since the previous update', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    expect(cache.timeSec).toBe(2);
    expect(cache.dtSec).toBe(0);

    updateFrameCache(cache, frame, 2016);
    expect(cache.timeSec).toBeCloseTo(2.016, 9);
    expect(cache.dtSec).toBeCloseTo(0.016, 9);
  });

  it('never reports a negative gap', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 2000);
    updateFrameCache(cache, frame, 1000);
    expect(cache.dtSec).toBe(0);
  });

  it('turns groups into layout nodes with their radius', () => {
    const cache = createFrameCache();
    updateFrameCache(cache, frame, 1000);
    const nodes = layoutNodesFrom(cache);

    expect(nodes).toHaveLength(fixture.groups.length);
    expect(nodes[0]).toEqual({
      key: fixture.groups[0].key,
      radius: radiusFor(fixture.groups[0].mem_mb),
    });
  });
});
```

- [ ] **Step 2: 실패 확인**

Run: `npx vitest run --project node tests/visual/frameCache.test.ts`
Expected: FAIL — `Failed to resolve import "../../src/visual/frameCache"`.

- [ ] **Step 3: `web/src/visual/frameCache.ts` 작성**

```ts
import type { InterpolatedGroup, InterpolatedSnapshot } from '../state/interpolator';
import type { LayoutNode } from './layout';
import { radiusFor } from './mapping';

// 스펙 4절. 장면 루트가 프레임마다 한 번 채우고, 노드들은 key 로 읽는다.
// React 상태가 아니라 가변 객체다 — 매 프레임 새로 만들지 않는다.
export interface FrameCache {
  snapshot: InterpolatedSnapshot | null;
  byKey: Map<string, InterpolatedGroup>;
  // activityFor 의 코어 수. 현재 스냅샷의 cores.length, 최소 1.
  coreCount: number;
  // performance.now() 기준 초. 노드의 부유와 툴팁이 같은 시각을 쓰게 한다.
  // 한 번도 갱신되지 않았으면 null.
  timeSec: number | null;
  // 직전 갱신과의 간격(초). 첫 갱신은 0.
  dtSec: number;
}

export function createFrameCache(): FrameCache {
  return { snapshot: null, byKey: new Map(), coreCount: 1, timeSec: null, dtSec: 0 };
}

export function updateFrameCache(
  cache: FrameCache,
  snapshot: InterpolatedSnapshot | null,
  nowMs: number,
): void {
  const timeSec = nowMs / 1000;
  cache.dtSec = cache.timeSec === null ? 0 : Math.max(0, timeSec - cache.timeSec);
  cache.timeSec = timeSec;

  cache.snapshot = snapshot;
  cache.byKey.clear();
  if (snapshot === null) {
    return;
  }
  for (const group of snapshot.groups) {
    cache.byKey.set(group.key, group);
  }
  cache.coreCount = Math.max(1, snapshot.cores.length);
}

// 레이아웃 시뮬레이션의 입력. 순서는 스냅샷의 그룹 순서다.
export function layoutNodesFrom(cache: FrameCache): LayoutNode[] {
  const nodes: LayoutNode[] = [];
  for (const group of cache.byKey.values()) {
    nodes.push({ key: group.key, radius: radiusFor(group.mem_mb) });
  }
  return nodes;
}
```

- [ ] **Step 4: 통과 확인**

Run: `npx vitest run --project node tests/visual/frameCache.test.ts`
Expected: PASS, `Tests  10 passed (10)`.

- [ ] **Step 5: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint`
Expected: 전부 통과. `|node|` 프로젝트에 테스트 파일 4개(schema, mapping, layout, frameCache).

- [ ] **Step 6: 커밋**

```
git add src/visual/frameCache.ts tests/visual/frameCache.test.ts
git commit -m "feat(web): cache each frame's interpolated groups by key"
```

---

## Task 5: 화면 전환 (Shell)

**Files:**
- Create: `web/src/shell/view.ts`
- Create: `web/src/shell/Shell.tsx`
- Create: `web/src/shell/shell.css`
- Create: `web/src/scene/Universe.tsx` (임시 자리표시자 — Task 6 이 교체한다)
- Modify: `web/src/main.tsx`
- Test: `web/tests/shell.test.tsx`

**Interfaces:**
- Consumes: `App` (`web/src/dashboard/App.tsx`), `ConnectionBadge` (`web/src/dashboard/ConnectionBadge.tsx`, props `{ status: StreamStatus; seq: number | null }`), `useSnapshotStore`.
- Produces:
  - `type View = 'universe' | 'dashboard'`, `DASHBOARD_HASH = '#dashboard'`, `viewFromHash(hash: string): View`, `toggledHash(view: View): string`
  - `Shell()` 컴포넌트
  - `Universe()` 컴포넌트 (`web/src/scene/Universe.tsx`, named export) — Task 6 이 같은 이름·같은 경로로 교체한다. 테스트는 이 모듈을 `vi.mock` 한다.
  - CSS 클래스 `universe`, `universe-message`, `universe-tooltip`, `universe-tooltip-name`, `shell-badge`, `shell-toggle`

- [ ] **Step 1: 실패하는 테스트 작성 — `web/tests/shell.test.tsx`**

```tsx
import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { Shell } from '../src/shell/Shell';
import { toggledHash, viewFromHash } from '../src/shell/view';
import { useSnapshotStore } from '../src/state/snapshotStore';

// jsdom 에는 WebGL 이 없다. 장면 자체는 자동 테스트 대상이 아니므로 (계약서 10절)
// 여기서는 어느 화면이 선택되는지만 본다.
vi.mock('../src/scene/Universe', () => ({
  Universe: () => <div>universe-stub</div>,
}));

function setHash(hash: string) {
  act(() => {
    window.location.hash = hash;
    window.dispatchEvent(new HashChangeEvent('hashchange'));
  });
}

describe('viewFromHash', () => {
  it('shows the universe by default', () => {
    expect(viewFromHash('')).toBe('universe');
    expect(viewFromHash('#')).toBe('universe');
    expect(viewFromHash('#something-else')).toBe('universe');
  });

  it('shows the dashboard for #dashboard', () => {
    expect(viewFromHash('#dashboard')).toBe('dashboard');
  });

  it('toggles to the other view', () => {
    expect(toggledHash('universe')).toBe('#dashboard');
    expect(toggledHash('dashboard')).toBe('');
  });
});

describe('Shell', () => {
  beforeEach(() => {
    useSnapshotStore.getState().reset();
    setHash('');
  });

  afterEach(() => {
    setHash('');
  });

  it('renders the universe and a connection badge without a hash', () => {
    render(<Shell />);
    expect(screen.getByText('universe-stub')).toBeInTheDocument();
    expect(screen.getByText('closed')).toBeInTheDocument();
    expect(screen.queryByText(/data check/)).not.toBeInTheDocument();
  });

  it('renders only the dashboard for #dashboard', () => {
    setHash('#dashboard');
    render(<Shell />);
    expect(screen.getByText(/data check/)).toBeInTheDocument();
    expect(screen.queryByText('universe-stub')).not.toBeInTheDocument();
  });

  it('follows the hash when it changes', () => {
    render(<Shell />);
    setHash('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
    setHash('');
    expect(screen.getByText('universe-stub')).toBeInTheDocument();
  });

  it('switches views with the toggle button', async () => {
    const user = userEvent.setup();
    render(<Shell />);

    await user.click(screen.getByRole('button', { name: /dashboard/ }));
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
  });

  it('switches views with the D key', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
  });

  it('ignores D with a modifier or while typing', () => {
    render(
      <>
        <Shell />
        <input aria-label="field" />
      </>,
    );
    fireEvent.keyDown(window, { key: 'd', ctrlKey: true });
    fireEvent.keyDown(screen.getByLabelText('field'), { key: 'd' });
    expect(window.location.hash).toBe('');
  });
});
```

- [ ] **Step 2: 실패 확인**

Run: `npx vitest run --project jsdom tests/shell.test.tsx`
Expected: FAIL — `Failed to resolve import "../src/shell/Shell"`.

- [ ] **Step 3: `web/src/shell/view.ts` 작성**

```ts
// 스펙 8절. 어느 화면을 보여줄지는 URL 해시가 정한다 — 새로고침과 북마크에
// 유지되고, 엔진의 SPA 폴백과 충돌하지 않는다.
export type View = 'universe' | 'dashboard';

export const DASHBOARD_HASH = '#dashboard';

export function viewFromHash(hash: string): View {
  return hash === DASHBOARD_HASH ? 'dashboard' : 'universe';
}

// 반대쪽 화면의 해시. 우주로 돌아갈 때는 해시를 비운다.
export function toggledHash(view: View): string {
  return view === 'dashboard' ? '' : DASHBOARD_HASH;
}
```

- [ ] **Step 4: `web/src/shell/shell.css` 작성**

```css
.universe {
  position: fixed;
  inset: 0;
}

.universe-message {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0;
  color: #8f9aa8;
  pointer-events: none;
}

.universe-message a {
  pointer-events: auto;
  color: #6ee7a8;
  margin-left: 0.5ch;
}

.universe-tooltip {
  padding: 6px 9px;
  background: rgba(20, 24, 29, 0.85);
  border: 1px solid #2a313a;
  border-radius: 4px;
  white-space: nowrap;
}

.universe-tooltip-name {
  color: #e8edf3;
}

.shell-badge {
  position: fixed;
  top: 12px;
  left: 12px;
  right: 160px;
}

.shell-toggle {
  position: fixed;
  top: 12px;
  right: 12px;
  padding: 6px 12px;
  font: inherit;
  color: #d7dde5;
  background: #14181d;
  border: 1px solid #2a313a;
  border-radius: 4px;
  cursor: pointer;
}

.shell-toggle:hover {
  border-color: #6ee7a8;
}
```

- [ ] **Step 5: 임시 `web/src/scene/Universe.tsx` 작성**

```tsx
// Task 6 이 R3F 장면으로 교체한다. 이름과 경로는 그대로 유지된다.
export function Universe() {
  return (
    <div className="universe">
      <p className="universe-message">universe goes here</p>
    </div>
  );
}
```

- [ ] **Step 6: `web/src/shell/Shell.tsx` 작성**

```tsx
import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { ConnectionBadge } from '../dashboard/ConnectionBadge';
import { Universe } from '../scene/Universe';
import { useSnapshotStore } from '../state/snapshotStore';
import { toggledHash, viewFromHash } from './view';
import './shell.css';

function subscribeToHash(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

function currentHash(): string {
  return window.location.hash;
}

// 입력 중인 글자를 단축키로 가로채지 않는다.
function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))
  );
}

// 우주와 대시보드 중 하나만 마운트한다. 숨긴 쪽을 남겨 두면 대시보드의
// 100 ms 타이머와 WebGL 렌더 루프가 함께 돈다.
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));
  const status = useSnapshotStore((state) => state.status);
  const seq = useSnapshotStore((state) => state.current?.seq ?? null);

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
        return;
      }
      if (event.key === 'd' || event.key === 'D') {
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
          <Universe />
          <div className="shell-badge">
            <ConnectionBadge status={status} seq={seq} />
          </div>
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
```

- [ ] **Step 7: `web/src/main.tsx` 를 Shell 로 연결**

전체를 다음으로 교체한다:

```tsx
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { Shell } from './shell/Shell';
import { useSystemStream } from './stream/useSystemStream';

// 스트림을 띄우는 일은 화면 컴포넌트 밖에 둔다. 그래야 Shell 과 그 아래가
// 스토어만 읽는 순수한 화면으로 남고, 스트림 없이도 렌더된다.
function Root() {
  useSystemStream();
  return <Shell />;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Root />
  </StrictMode>,
);
```

- [ ] **Step 8: 통과 확인**

Run: `npx vitest run --project jsdom tests/shell.test.tsx`
Expected: PASS, `Tests  9 passed (9)`. 출력에 `act(` 경고나 `Warning:` 이 없어야 한다:

Run: `npx vitest run --project jsdom tests/shell.test.tsx 2>&1 | Select-String -Pattern "act\(|Warning"`
Expected: 출력 없음.

- [ ] **Step 9: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected: 전부 통과. lint 는 기존 main.tsx 경고 1개. 기존 `tests/app.test.tsx` 도 그대로 통과한다 (App 은 바뀌지 않았다).

- [ ] **Step 10: 커밋**

```
git add src/shell src/scene/Universe.tsx src/main.tsx tests/shell.test.tsx
git commit -m "feat(web): switch between the universe and the dashboard by URL hash"
```

---

## Task 6: R3F 장면 — Process Node, 툴팁, 대기·실패 안내

**Files:**
- Create: `web/src/scene/sceneContext.ts`
- Create: `web/src/scene/nodeList.ts`
- Create: `web/src/scene/ProcessNode.tsx`
- Create: `web/src/scene/Tooltip.tsx`
- Create: `web/src/scene/SceneRoot.tsx`
- Modify: `web/src/scene/Universe.tsx` (자리표시자를 전체 교체)
- Modify: `web/README.md`
- Test: `web/tests/nodeList.test.ts`

**Interfaces:**
- Consumes: `sample` (`state/interpolator`), `useSnapshotStore` (`state/snapshotStore`), `FrameCache`/`createFrameCache`/`updateFrameCache`/`layoutNodesFrom` (Task 4), `LayoutSim`/`floatingPosition` (Task 3), `radiusFor`/`activityFor`/`pulseFor`/`advancePhase`/`glowFor`/`colorFor`/`HALO_SCALE` (Task 2), `hash01` (Task 2), `formatMb`/`formatPct` (`dashboard/format.ts`, `formatPct(null) === '-'`), CSS 클래스 (Task 5).
- Produces:
  - `interface SceneContextValue { cache: FrameCache; layout: LayoutSim; setHovered: (update: (current: string | null) => string | null) => void }`, `SceneContext`, `useSceneContext()`
  - `interface NodeSpec { key: string; account: 'user' | 'system' }`, `selectNodeIds(state: { current: Snapshot | null }): string[]`, `parseNodeId(id: string): NodeSpec`
  - `Universe()` — Task 5 의 자리표시자와 같은 이름·경로

R3F 주의사항 (이 Task 의 코드가 이미 반영하고 있다):
- `useFrame(cb, priority)` 에서 **양수 우선순위는 R3F 의 자동 렌더를 끈다.** 장면 루트는 `-1` 을 써서 노드(0)보다 먼저 돈다.
- drei `Html` 은 DOM 이라 부모 `group.visible = false` 로 숨겨지지 않는다. DOM 요소의 `style.display` 를 직접 바꾼다.
- `zustand` v5 의 `useShallow` 는 `zustand/react/shallow` 에서 import 한다.

- [ ] **Step 1: 실패하는 테스트 작성 — `web/tests/nodeList.test.ts`**

```ts
import { describe, expect, it } from 'vitest';

import type { ProcessGroup, Snapshot } from '../src/protocol/schema';
import { parseNodeId, selectNodeIds } from '../src/scene/nodeList';

function group(key: string, account: ProcessGroup['account']): ProcessGroup {
  return {
    key,
    name: key.split(':')[0],
    root_pid: 1,
    cpu_pct: 0,
    mem_mb: 100,
    proc_count: 1,
    thread_count: 1,
    started_at: 0,
    account,
    image_path: '',
    children: [],
  };
}

function snapshot(groups: ProcessGroup[]): Snapshot {
  return {
    type: 'snapshot',
    v: 1,
    seq: 1,
    t: 0,
    system: { cpu_pct: 0, mem_used_mb: 0, mem_total_mb: 0, process_total: 0, thread_total: 0 },
    cores: [],
    groups,
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  };
}

describe('selectNodeIds', () => {
  it('returns one id per group in snapshot order', () => {
    const ids = selectNodeIds({
      current: snapshot([group('a.exe:1', 'user'), group('b.exe:2', 'system')]),
    });
    expect(ids).toEqual(['user|a.exe:1', 'system|b.exe:2']);
  });

  it('returns the same empty array every time there is no snapshot', () => {
    expect(selectNodeIds({ current: null })).toBe(selectNodeIds({ current: null }));
    expect(selectNodeIds({ current: null })).toEqual([]);
  });
});

describe('parseNodeId', () => {
  it('splits the account from the key', () => {
    expect(parseNodeId('system|svc.exe:44')).toEqual({ account: 'system', key: 'svc.exe:44' });
  });

  it('keeps a | inside the key', () => {
    // 파싱이 key 에 | 가 없다는 가정에 기대지 않는다.
    expect(parseNodeId('user|odd|name.exe:7')).toEqual({ account: 'user', key: 'odd|name.exe:7' });
  });

  it('round-trips every id selectNodeIds produces', () => {
    const groups = [group('a.exe:1', 'user'), group('b b.exe:2', 'system')];
    const parsed = selectNodeIds({ current: snapshot(groups) }).map(parseNodeId);
    expect(parsed).toEqual(groups.map((g) => ({ key: g.key, account: g.account })));
  });
});
```

- [ ] **Step 2: 실패 확인**

Run: `npx vitest run --project jsdom tests/nodeList.test.ts`
Expected: FAIL — `Failed to resolve import "../src/scene/nodeList"`.

- [ ] **Step 3: `web/src/scene/nodeList.ts` 작성**

```ts
import type { ProcessGroup, Snapshot } from '../protocol/schema';

// 장면이 React 로 다시 그려야 하는 때는 그룹 key 집합이 바뀔 때뿐이다.
// 스토어 selector 가 문자열 배열을 돌려주면 useShallow 가 원소별로 비교해
// 1 Hz 스냅샷마다 재렌더되지 않는다. account 는 색을 정하므로 함께 싣는다.
// account 에는 '|' 가 없으므로 첫 '|' 에서 자르면 key 에 '|' 가 있어도 안전하다.
export interface NodeSpec {
  key: string;
  account: ProcessGroup['account'];
}

const EMPTY: string[] = [];

export function selectNodeIds(state: { current: Snapshot | null }): string[] {
  if (state.current === null) {
    return EMPTY;
  }
  return state.current.groups.map((group) => `${group.account}|${group.key}`);
}

export function parseNodeId(id: string): NodeSpec {
  const bar = id.indexOf('|');
  return {
    account: id.slice(0, bar) as ProcessGroup['account'],
    key: id.slice(bar + 1),
  };
}
```

- [ ] **Step 4: 통과 확인**

Run: `npx vitest run --project jsdom tests/nodeList.test.ts`
Expected: PASS, `Tests  5 passed (5)`.

- [ ] **Step 5: `web/src/scene/sceneContext.ts` 작성**

```ts
import { createContext, useContext } from 'react';

import type { FrameCache } from '../visual/frameCache';
import type { LayoutSim } from '../visual/layout';

// 장면 루트가 소유하는 가변 객체들. context 값 자체는 장면이 살아 있는 동안
// 바뀌지 않는다 — 안의 내용만 프레임마다 바뀌므로 context 구독자가 재렌더되지 않는다.
export interface SceneContextValue {
  cache: FrameCache;
  layout: LayoutSim;
  setHovered: (update: (current: string | null) => string | null) => void;
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

- [ ] **Step 6: `web/src/scene/ProcessNode.tsx` 작성**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  type Group,
  type Mesh,
  type MeshBasicMaterial,
  type MeshStandardMaterial,
} from 'three';

import type { ProcessGroup } from '../protocol/schema';
import { hash01 } from '../visual/hash';
import { floatingPosition } from '../visual/layout';
import {
  HALO_SCALE,
  activityFor,
  advancePhase,
  colorFor,
  glowFor,
  pulseFor,
  radiusFor,
} from '../visual/mapping';
import { useSceneContext } from './sceneContext';

const SALT_PHASE = 2;

interface Props {
  nodeKey: string;
  account: ProcessGroup['account'];
}

// 그룹 하나. 프레임 값은 React 상태를 거치지 않는다 — useFrame 에서
// FrameCache 를 key 로 읽어 ref 를 직접 바꾼다 (스펙 4절).
export function ProcessNode({ nodeKey, account }: Props) {
  const { cache, layout, setHovered } = useSceneContext();

  const root = useRef<Group>(null);
  const body = useRef<Mesh>(null);
  const bodyMaterial = useRef<MeshStandardMaterial>(null);
  const halo = useRef<Mesh>(null);
  const haloMaterial = useRef<MeshBasicMaterial>(null);
  // 40 개가 동시에 숨 쉬지 않도록 초기 위상을 key 로 흩는다.
  const phase = useRef(hash01(nodeKey, SALT_PHASE) * 2 * Math.PI);

  const color = useMemo(() => {
    const hsl = colorFor(account, nodeKey);
    return new Color().setHSL(hsl.h / 360, hsl.s, hsl.l);
  }, [account, nodeKey]);

  useFrame(() => {
    if (
      root.current === null ||
      body.current === null ||
      bodyMaterial.current === null ||
      halo.current === null ||
      haloMaterial.current === null
    ) {
      return;
    }
    const group = cache.byKey.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    // 첫 프레임 전이나 그룹이 막 사라진 프레임에는 원점에 크기 1 로 뜨지 않게 숨긴다.
    if (group === undefined || position === undefined) {
      root.current.visible = false;
      return;
    }
    root.current.visible = true;

    const activity = activityFor(group.cpu_pct, cache.coreCount);
    const pulse = pulseFor(activity);
    phase.current = advancePhase(phase.current, pulse.freqHz, cache.dtSec);
    const scale = radiusFor(group.mem_mb) * (1 + pulse.amplitude * Math.sin(phase.current));

    root.current.position.set(position.x, position.y, position.z);
    body.current.scale.setScalar(scale);
    halo.current.scale.setScalar(scale * HALO_SCALE);

    const glow = glowFor(activity);
    bodyMaterial.current.emissiveIntensity = glow.emissiveIntensity;
    haloMaterial.current.opacity = glow.haloOpacity;
  });

  return (
    <group ref={root} visible={false}>
      <mesh
        ref={body}
        onPointerOver={(event) => {
          event.stopPropagation();
          setHovered(() => nodeKey);
        }}
        onPointerOut={() => setHovered((current) => (current === nodeKey ? null : current))}
      >
        <sphereGeometry args={[1, 32, 32]} />
        <meshStandardMaterial
          ref={bodyMaterial}
          color={color}
          emissive={color}
          roughness={0.55}
          metalness={0.1}
        />
      </mesh>
      {/* 헤일로는 호버 대상이 아니다. 광선 검사에서 빼야 뒤쪽 천체를 가리지 않는다. */}
      <mesh ref={halo} raycast={() => null}>
        <sphereGeometry args={[1, 24, 24]} />
        <meshBasicMaterial
          ref={haloMaterial}
          color={color}
          transparent
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
```

- [ ] **Step 7: `web/src/scene/Tooltip.tsx` 작성**

```tsx
import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import type { Group } from 'three';

import { formatMb, formatPct } from '../dashboard/format';
import { floatingPosition } from '../visual/layout';
import { radiusFor } from '../visual/mapping';
import { useSceneContext } from './sceneContext';

interface Props {
  nodeKey: string;
}

// 호버한 천체 위에 이름·메모리·CPU 를 띄운다. 값은 1 Hz 가 아니라 보간된
// 프레임 값이므로 React 상태를 거치지 않고 DOM 을 직접 바꾼다.
// 숫자는 대시보드와 같은 formatMb·formatPct 로 쓴다 — 두 화면이 일치해야 한다.
export function Tooltip({ nodeKey }: Props) {
  const { cache, layout } = useSceneContext();
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
    const group = cache.byKey.get(nodeKey);
    const position =
      cache.timeSec === null ? undefined : floatingPosition(layout, nodeKey, cache.timeSec);
    // Html 은 DOM 이라 group.visible 로는 숨겨지지 않는다. DOM 쪽을 직접 숨긴다.
    if (group === undefined || position === undefined) {
      box.current.style.display = 'none';
      return;
    }
    box.current.style.display = '';
    anchor.current.position.set(position.x, position.y + radiusFor(group.mem_mb) * 1.2, position.z);
    name.current.textContent = group.name;
    detail.current.textContent = `${formatMb(group.mem_mb)} MB · CPU ${formatPct(group.cpu_pct)}%`;
  });

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

- [ ] **Step 8: `web/src/scene/SceneRoot.tsx` 작성**

```tsx
import { useFrame } from '@react-three/fiber';
import { useMemo, useState } from 'react';
import { useShallow } from 'zustand/react/shallow';

import { sample } from '../state/interpolator';
import { useSnapshotStore } from '../state/snapshotStore';
import { createFrameCache, layoutNodesFrom, updateFrameCache } from '../visual/frameCache';
import { LayoutSim } from '../visual/layout';
import { parseNodeId, selectNodeIds } from './nodeList';
import { ProcessNode } from './ProcessNode';
import { SceneContext, type SceneContextValue } from './sceneContext';
import { Tooltip } from './Tooltip';

// 노드들의 useFrame(우선순위 0)보다 먼저 돈다. 양수 우선순위는 R3F 의 자동
// 렌더를 끄므로 음수를 쓴다.
const BEFORE_NODES = -1;

// 스펙 4절. 프레임당 한 번 보간하고 레이아웃을 한 스텝 진행한 뒤, 노드들이
// key 로 읽을 수 있게 FrameCache 에 둔다.
export function SceneRoot() {
  const ids = useSnapshotStore(useShallow(selectNodeIds));
  const [hovered, setHovered] = useState<string | null>(null);

  const context = useMemo<SceneContextValue>(
    () => ({ cache: createFrameCache(), layout: new LayoutSim(), setHovered }),
    [],
  );

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
    updateFrameCache(context.cache, frame, now);
    context.layout.step(layoutNodesFrom(context.cache), context.cache.dtSec);
  }, BEFORE_NODES);

  return (
    <SceneContext.Provider value={context}>
      {ids.map((id) => {
        const { key, account } = parseNodeId(id);
        return <ProcessNode key={key} nodeKey={key} account={account} />;
      })}
      {hovered !== null && <Tooltip nodeKey={hovered} />}
    </SceneContext.Provider>
  );
}
```

- [ ] **Step 9: `web/src/scene/Universe.tsx` 전체 교체**

```tsx
import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useState } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
import { SceneRoot } from './SceneRoot';

function webglAvailable(): boolean {
  try {
    const canvas = document.createElement('canvas');
    return (canvas.getContext('webgl2') ?? canvas.getContext('webgl')) !== null;
  } catch {
    return false;
  }
}

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  const [webgl] = useState(webglAvailable);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);

  if (!webgl) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas dpr={[1, 2]} camera={{ fov: 50, position: [0, 10, 58] }}>
        <color attach="background" args={['#03040a']} />
        <ambientLight intensity={0.2} />
        {/* 중심 점광원은 가운데의 큰 천체 안에 묻힌다. 방향광을 쓴다. */}
        <directionalLight position={[20, 30, 25]} intensity={1.1} />
        <Stars radius={120} depth={60} count={4000} factor={4} fade />
        <SceneRoot />
        <OrbitControls
          enableDamping
          autoRotate={autoRotate}
          autoRotateSpeed={0.3}
          onStart={() => setAutoRotate(false)}
        />
      </Canvas>
      {!hasSnapshot && <p className="universe-message">waiting for the first snapshot…</p>}
    </div>
  );
}
```

- [ ] **Step 10: `web/README.md` 갱신**

1행 아래 소개 문단 (`Pulse Universe 검증 대시보드의 프런트엔드다. …보여준다.`) 을 다음으로 교체한다:

```markdown
Pulse Universe 의 프런트엔드다. C++ 엔진이 WebSocket 으로 1Hz 로 보내는 JSON
스냅샷을 받아, 프로세스 그룹을 천체로 그리는 3D 우주와 숫자 검증 대시보드로
보여준다. 기본 화면은 우주이고 `D` 키나 우측 상단 버튼(`#dashboard`)으로
대시보드와 오간다.
```

`## 레이어 구조` 의 코드 블록을 다음으로 교체한다:

```
protocol/schema.ts        zod 스키마와 타입, parseMessage
stream/SystemStream.ts    순수 클래스, 소켓·타이머를 주입받는다; 재연결 담당; React 를 모른다
stream/useSystemStream.ts stream/ 을 React 에 연결하는 유일한 다리, 실제 WebSocket 을 어댑팅한다
state/snapshotStore.ts    Zustand: previous, current, arrivedAt, intervalMs, status, lifecycleLog
state/interpolator.ts     sample(input, now) — 순수 함수, React 도 Zustand 도 모른다
state/useInterpolated.ts  대시보드를 위해 100ms 마다 sample() 을 호출한다
visual/                   순수 TS: 크기·맥박·발광·색 매핑, 배치 시뮬레이션, 프레임 캐시
scene/                    R3F: 프레임당 sample() 한 번 → FrameCache → 노드가 key 로 읽는다
dashboard/                React 컴포넌트; state/ 만 읽는다
shell/                    URL 해시로 우주와 대시보드 중 하나만 마운트한다
main.tsx                  스트림을 시작하는 루트 래퍼, Shell 을 렌더링한다
```

그 아래 문단 (`` `dashboard/` 는 `stream/` 을 직접 참조하지 않는다 … 강제된다. ``) 을 다음으로 교체한다:

```markdown
`dashboard/` 와 `scene/` 은 `stream/` 을 직접 참조하지 않는다 — 스트림 상태는
항상 `state/` 를 거쳐서만 들어온다. `visual/` 은 React·three·Zustand 를 모르는
순수 모듈이다. 이 경계들은 `.oxlintrc.json` 의 `no-restricted-imports` 규칙으로
강제된다. 장면의 프레임 값(보간된 CPU·메모리, 위치, 맥박)은 React 상태를 거치지
않고 `useFrame` 안에서 ref 로 바뀐다.
```

`## 테스트와 린트` 절 전체를 다음으로 교체한다:

````markdown
## 테스트와 린트

```
npm test
npm run typecheck
npm run lint
```

`npm test` 는 `tests/schema.test.ts` 와 `tests/visual/` 을 `node` 환경에서,
나머지는 `jsdom` 환경에서 실행한다 (`vite.config.ts` 의 `projects` 설정).
`visual/` 의 테스트가 DOM 없이 도는 것 자체가 그 모듈이 순수하다는 증거다.
출력에 React key 경고나 `act()` 경고가 남으면 안 된다.

`npm run typecheck` 는 앱과 테스트 파일을 모두 타입체크한다 (`tsconfig.test.json`).

3D 장면 자체는 자동 테스트 대상이 아니다. 엔진에 붙여 브라우저에서 확인한다.
콘솔의 `THREE.Clock: This module has been deprecated` 경고는 R3F 내부에서
나오는 것이다.
````

- [ ] **Step 11: 전체 확인**

Run: `npm test; npm run typecheck; npm run lint; npm run build`
Expected:
- 테스트 전부 통과 (M3 74 + Task 2~6 신규).
- typecheck 출력 없음.
- lint 오류 0, 경고는 기존 main.tsx 1개.
- build 성공, 청크 크기 경고 없음 (`index-*.js` 약 1.26 MB).

- [ ] **Step 12: 브라우저 확인 (가능하면)**

엔진과 dev 서버를 띄울 수 있으면 확인한다 — 브라우저 도구가 없으면 이 단계는 건너뛰고 보고서에 그렇게 적는다. 컨트롤러가 최종 확인을 따로 한다.

```powershell
# 터미널 1
cd C:\dev\pulse-uni\engine; .\build\Debug\pulse-engine.exe --serve
# 터미널 2
cd C:\dev\pulse-uni\web; npm run dev
```

`http://localhost:5173/` 에서: 배지가 `open` 이 되고 천체 40개가 보인다. 천체에 마우스를 올리면 이름·MB·CPU 툴팁이 뜬다. `D` 를 누르면 대시보드로 바뀐다. 콘솔 오류 없음 (위의 Clock 경고와 StrictMode 이중 연결로 인한 WebSocket 경고 1개는 예상된 것이다).

**엔진을 반드시 종료한다** (`Ctrl+C` 또는 `taskkill /IM pulse-engine.exe /F`). 남아 있으면 다음 빌드가 `LNK1168` 로 실패한다.

- [ ] **Step 13: 커밋**

```
git add src/scene tests/nodeList.test.ts README.md
git commit -m "feat(web): render process groups as pulsing, glowing bodies in an R3F scene"
```

---

## M4 완료 조건 (컨트롤러가 확인)

- [ ] `npm test`, `npm run typecheck`, `npm run lint`, `npm run build` 가 통과한다. 테스트 출력에 `act(`·key 경고가 없다.
- [ ] `src/visual/` 이 `react`·`three`·`zustand`·`@react-three/*` 를 import 하지 않는다:

```powershell
cd C:\dev\pulse-uni\web; Select-String -Path src\visual\*.ts -Pattern "from '(react|react-dom|three|zustand|@react-three/.*)'"
```

Expected: 출력 없음.

- [ ] dev 서버 + 엔진에서:
  - 천체 40개가 서로 겹치지 않고 큰 것이 중심 쪽에 있다.
  - CPU 를 쓰는 프로세스가 더 빨리 숨 쉬고 더 밝다 (`node -e "const e=Date.now()+20000;while(Date.now()<e){}"` 로 한 코어를 20초 태우면 node.exe 그룹이 밝아진다).
  - 호버 툴팁의 MB·CPU 가 같은 순간 대시보드의 값과 일치한다.
  - 새로고침해도 배치가 같다 (같은 그룹 집합일 때).
  - `D` 와 버튼으로 전환되고, 해시가 새로고침 뒤에도 유지된다.
  - 조작하면 자동 회전이 멈춘다.
- [ ] `npm run build` 후 `pulse-engine --serve --web-root ..\web\dist` 가 서빙한 페이지에서도 같다. 콘솔에는 R3F 의 `THREE.Clock` 경고 외에 아무것도 없다.

## 다음 단계

M5 가 생성·종료 연출(파티클 수렴, 붕괴), Focus(클릭 시 자식 위성), 카메라 전환을 GSAP 으로 올린다. `lifecycle` 은 seq 가 바뀔 때 한 번 소비해야 한다 (M3 스펙: lifecycle 은 한 주기 내내 그대로 통과한다). 새 그룹이 즉시 나타나는 지금의 동작이 M5 에서 연출로 바뀐다.
