import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';

import { GroupTable } from '../src/dashboard/GroupTable';
import type { InterpolatedGroup } from '../src/state/interpolator';

function makeGroup(overrides: Partial<InterpolatedGroup> = {}): InterpolatedGroup {
  return {
    key: 'app.exe:100',
    name: 'app.exe',
    root_pid: 100,
    cpu_pct: 12.3,
    mem_mb: 1024,
    proc_count: 3,
    thread_count: 30,
    started_at: 1,
    account: 'user',
    image_path: 'C:/app.exe',
    children: [],
    ...overrides,
  };
}

describe('GroupTable', () => {
  it('renders a row per group', () => {
    render(<GroupTable groups={[makeGroup(), makeGroup({ key: 'b:2', name: 'b.exe' })]} />);

    expect(screen.getByText('app.exe')).toBeInTheDocument();
    expect(screen.getByText('b.exe')).toBeInTheDocument();
  });

  it('shows a dash for an unknown cpu reading', () => {
    render(<GroupTable groups={[makeGroup({ cpu_pct: null })]} />);

    expect(screen.getByText('-')).toBeInTheDocument();
  });

  it('shows a measured zero as zero', () => {
    render(<GroupTable groups={[makeGroup({ cpu_pct: 0 })]} />);

    expect(screen.getByText('0.0')).toBeInTheDocument();
    expect(screen.queryByText('-')).toBeNull();
  });

  it('shows the account', () => {
    render(<GroupTable groups={[makeGroup({ account: 'system' })]} />);

    expect(screen.getByText('system')).toBeInTheDocument();
  });
});
