import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { useFocusStore } from '../src/scene/focusStore';
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
    useFocusStore.getState().clear();
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

  it('hides while a group is focused so it never covers the focus panel', () => {
    render(<Legend />);
    act(() => useFocusStore.getState().toggle('a.exe:1'));
    expect(screen.queryByRole('complementary', { name: 'Legend' })).toBeNull();
    expect(screen.queryByRole('button', { name: /Legend/ })).toBeNull();

    act(() => useFocusStore.getState().clear());
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
