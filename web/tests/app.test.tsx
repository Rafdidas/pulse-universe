import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';

import { App } from '../src/dashboard/App';
import { useSnapshotStore } from '../src/state/snapshotStore';

describe('App', () => {
  it('renders without starting a stream', () => {
    // App 이 스트림을 띄우지 않으므로 소켓 없이도 렌더된다.
    // 스트림 기동은 main.tsx 의 Root 가 한다.
    useSnapshotStore.getState().reset();

    render(<App />);

    expect(screen.getByText(/Pulse Universe/)).toBeInTheDocument();
    expect(screen.getByText(/waiting for the first snapshot/)).toBeInTheDocument();
  });
});
