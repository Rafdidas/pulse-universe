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
