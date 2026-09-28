import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Snapshot } from '../src/protocol/schema';
import { Shell } from '../src/shell/Shell';
import { toggledHash, viewFromHash } from '../src/shell/view';
import { useSnapshotStore } from '../src/state/snapshotStore';

// jsdom 에는 WebGL 이 없다. 장면 자체는 자동 테스트 대상이 아니므로 (계약서 10절)
// 여기서는 어느 화면이 선택되는지만 본다.
let universeRenderCount = 0;
let universeShouldThrow = false;

vi.mock('../src/scene/Universe', () => ({
  Universe: () => {
    universeRenderCount += 1;
    if (universeShouldThrow) {
      throw new Error('scene boom');
    }
    return <div>universe-stub</div>;
  },
}));

function setHash(hash: string) {
  act(() => {
    window.location.hash = hash;
    window.dispatchEvent(new HashChangeEvent('hashchange'));
  });
}

// tests/nodeList.test.ts 의 최소 스냅샷 모양을 그대로 쓴다.
function minimalSnapshot(seq: number): Snapshot {
  return {
    type: 'snapshot',
    v: 1,
    seq,
    t: seq,
    system: { cpu_pct: 0, mem_used_mb: 0, mem_total_mb: 0, process_total: 0, thread_total: 0 },
    cores: [],
    groups: [],
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  };
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
    universeRenderCount = 0;
    universeShouldThrow = false;
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
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD' });
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
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', ctrlKey: true });
    fireEvent.keyDown(screen.getByLabelText('field'), { key: 'd', code: 'KeyD' });
    expect(window.location.hash).toBe('');
  });

  // F3: 한글 IME 로 조합 중인 'ㅇ' 은 code 로만 잡는다. keyup 없이 계속 눌려
  // 반복 입력되는 D(자동 반복)와, 조합 중인 D 는 토글하지 않는다.
  it('toggles on a Korean IME keydown when the physical key is D', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'ㅇ', code: 'KeyD' });
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
  });

  it('ignores an auto-repeated D keydown', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', repeat: true });
    expect(window.location.hash).toBe('');
  });

  it('ignores D while composing (IME)', () => {
    render(<Shell />);
    fireEvent.keyDown(window, { key: 'd', code: 'KeyD', isComposing: true });
    expect(window.location.hash).toBe('');
  });

  // F1: seq 는 1 Hz 로 바뀐다. Shell 이 그것을 구독하면 매초 <Universe/> 까지
  // 재렌더된다 (스펙 4절 규칙 3 위반). 배지만 구독해야 한다.
  it('does not re-render Universe when a new snapshot arrives', () => {
    render(<Shell />);
    const before = universeRenderCount;

    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(1), performance.now());
    });
    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(2), performance.now());
    });

    expect(universeRenderCount).toBe(before);
  });

  it('shows the connection state in the badge', () => {
    render(<Shell />);
    expect(screen.getByText('closed')).toBeInTheDocument();
  });

  it('shows the latest seq in the badge after a snapshot arrives', () => {
    render(<Shell />);
    act(() => {
      useSnapshotStore.getState().pushSnapshot(minimalSnapshot(7), performance.now());
    });
    expect(screen.getByText(/seq 7/)).toBeInTheDocument();
  });
});

// F2: 장면이 렌더 중 던지면 R3F 가 에러를 바깥으로 다시 던진다. 경계가 없으면
// 리액트가 루트 전체를 언마운트해 토글 버튼과 D 키까지 사라진다.
describe('Shell scene error boundary', () => {
  let consoleErrorSpy: ReturnType<typeof vi.spyOn>;

  beforeEach(() => {
    useSnapshotStore.getState().reset();
    universeRenderCount = 0;
    universeShouldThrow = true;
    setHash('');
    consoleErrorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });

  afterEach(() => {
    setHash('');
    universeShouldThrow = false;
    consoleErrorSpy.mockRestore();
  });

  it('falls back to a dashboard link and keeps the toggle button working', async () => {
    const user = userEvent.setup();
    render(<Shell />);

    expect(screen.getByText(/3D scene failed/)).toBeInTheDocument();
    expect(screen.getByRole('link', { name: '#dashboard' })).toHaveAttribute('href', '#dashboard');

    const toggle = screen.getByRole('button', { name: /dashboard/ });
    expect(toggle).toBeInTheDocument();

    await user.click(toggle);
    act(() => {
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(window.location.hash).toBe('#dashboard');
    expect(screen.getByText(/data check/)).toBeInTheDocument();
  });
});
