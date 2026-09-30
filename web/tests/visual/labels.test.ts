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
