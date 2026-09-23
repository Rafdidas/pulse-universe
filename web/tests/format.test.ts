import { describe, expect, it } from 'vitest';

import { formatMb, formatPct } from '../src/dashboard/format';

describe('format', () => {
  it('shows a dash for an unknown value', () => {
    // null(모름)과 0(측정된 0)이 같아 보이면 대시보드가 제 역할을 못 한다.
    expect(formatPct(null)).toBe('-');
  });

  it('shows a measured zero as zero', () => {
    expect(formatPct(0)).toBe('0.0');
  });

  it('shows one decimal place', () => {
    expect(formatPct(12.345)).toBe('12.3');
  });

  it('formats megabytes without decimals', () => {
    expect(formatMb(3626.4)).toBe('3,626');
  });
});
