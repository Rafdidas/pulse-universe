import { describe, expect, it } from 'vitest';

import { isShortcut } from '../src/shell/shortcut';

function key(init: KeyboardEventInit, target?: EventTarget): KeyboardEvent {
  const event = new KeyboardEvent('keydown', init);
  if (target !== undefined) {
    Object.defineProperty(event, 'target', { value: target });
  }
  return event;
}

describe('isShortcut', () => {
  it('matches the physical key and the letter in either case', () => {
    expect(isShortcut(key({ code: 'KeyP', key: 'p' }), 'p')).toBe(true);
    expect(isShortcut(key({ code: 'KeyP', key: 'P' }), 'p')).toBe(true);
    // 한글 IME: 글자는 'ㅔ' 지만 물리 키는 P 다.
    expect(isShortcut(key({ code: 'KeyP', key: 'ㅔ' }), 'p')).toBe(true);
    // AZERTY·Dvorak: 물리 키는 다르지만 글자가 p 다.
    expect(isShortcut(key({ code: 'KeyR', key: 'p' }), 'p')).toBe(true);
    expect(isShortcut(key({ code: 'KeyD', key: 'd' }), 'p')).toBe(false);
  });

  it('ignores modifiers, repeats, composition and typing in a field', () => {
    expect(isShortcut(key({ code: 'KeyP', key: 'p', ctrlKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', altKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', metaKey: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', repeat: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p', isComposing: true }), 'p')).toBe(false);
    expect(isShortcut(key({ code: 'KeyP', key: 'p' }, document.createElement('input')), 'p')).toBe(
      false,
    );
    const p = { code: 'KeyP', key: 'p' };
    expect(isShortcut(key(p, document.createElement('textarea')), 'p')).toBe(false);
    expect(isShortcut(key(p, document.createElement('select')), 'p')).toBe(false);
    const editable = document.createElement('div');
    Object.defineProperty(editable, 'isContentEditable', { value: true });
    expect(isShortcut(key(p, editable), 'p')).toBe(false);
  });
});
