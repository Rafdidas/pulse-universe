import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { ProcessGroup, Snapshot } from '../src/protocol/schema';
import { FocusPanel } from '../src/scene/FocusPanel';
import { useFocusStore } from '../src/scene/focusStore';
import { useSnapshotStore } from '../src/state/snapshotStore';
import { emptyNetwork } from './fixtures/network';

function group(): ProcessGroup {
  return {
    key: 'whale.exe:8784',
    name: 'whale.exe',
    root_pid: 8784,
    cpu_pct: 1.5,
    mem_mb: 2755,
    proc_count: 3,
    thread_count: 752,
    started_at: 0,
    account: 'user',
    image_path: 'C:\\Program Files\\Whale\\whale.exe',
    children: [
      { pid: 22180, name: 'whale.exe', role: 'child', cpu_pct: null, mem_mb: 351, threads: 20 },
      { pid: 11008, name: 'whale.exe', role: 'child', cpu_pct: 0, mem_mb: 412, threads: 30 },
    ],
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
    network: emptyNetwork,
  };
}

describe('focusStore', () => {
  beforeEach(() => useFocusStore.getState().clear());

  it('focuses, moves and clears', () => {
    const store = useFocusStore.getState();
    store.focus('a.exe:1');
    expect(useFocusStore.getState().focusedKey).toBe('a.exe:1');
    store.focus('b.exe:2');
    expect(useFocusStore.getState().focusedKey).toBe('b.exe:2');
    store.clear();
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });

  it('toggles off when the same key is chosen again', () => {
    const store = useFocusStore.getState();
    store.toggle('a.exe:1');
    store.toggle('a.exe:1');
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });

  it('moves the focus when another key is toggled', () => {
    const store = useFocusStore.getState();
    store.toggle('a');
    store.toggle('b');
    expect(useFocusStore.getState().focusedKey).toBe('b');
  });
});

describe('FocusPanel', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useSnapshotStore.getState().reset();
    useFocusStore.getState().clear();
    useSnapshotStore.getState().pushSnapshot(snapshot([group()]), 0);
  });

  afterEach(() => {
    vi.useRealTimers();
    useFocusStore.getState().clear();
  });

  // useInterpolated 는 100 ms 마다 한 번 값을 넣는다.
  function tick() {
    act(() => {
      vi.advanceTimersByTime(150);
    });
  }

  it('renders nothing without a focus', () => {
    const { container } = render(<FocusPanel />);
    tick();
    expect(container).toBeEmptyDOMElement();
  });

  it('shows the focused group and its children sorted by memory', () => {
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    tick();

    expect(screen.getByText('whale.exe', { selector: 'strong' })).toBeInTheDocument();
    expect(screen.getByText('2,755 MB')).toBeInTheDocument();
    expect(screen.getByText('1.5%')).toBeInTheDocument();
    const rows = screen.getAllByRole<HTMLTableRowElement>('row').slice(1);
    expect(rows.map((row) => row.cells[1].textContent)).toEqual(['11008', '22180']);
  });

  it('keeps an unknown child cpu as "-" and a measured 0 as "0.0"', () => {
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    tick();

    const rows = screen.getAllByRole<HTMLTableRowElement>('row').slice(1);
    expect(rows[0].cells[3].textContent).toBe('0.0');
    expect(rows[1].cells[3].textContent).toBe('-');
  });

  it('clears the focus from its close button', async () => {
    vi.useRealTimers();
    const user = userEvent.setup();
    useFocusStore.getState().focus('whale.exe:8784');
    render(<FocusPanel />);
    await screen.findByText('whale.exe', { selector: 'strong' });

    await user.click(screen.getByRole('button', { name: 'close focus' }));
    expect(useFocusStore.getState().focusedKey).toBeNull();
  });

  it('renders nothing when the focused group is not in the snapshot', () => {
    useFocusStore.getState().focus('gone.exe:1');
    const { container } = render(<FocusPanel />);
    tick();
    expect(container).toBeEmptyDOMElement();
  });
});
