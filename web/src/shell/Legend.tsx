import { useEffect, useState } from 'react';

import { isShortcut } from './shortcut';

// 이름표·별 정보 스펙 5절. 우주 화면을 읽는 법. 접은 상태는 브라우저에 기억한다.
const STORAGE_KEY = 'pulse.legend';

function readHidden(): boolean {
  try {
    return window.localStorage.getItem(STORAGE_KEY) === 'hidden';
  } catch {
    // 저장이 막힌 환경(사생활 보호 창 등)에서는 펼침으로 시작한다.
    return false;
  }
}

function writeHidden(hidden: boolean): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, hidden ? 'hidden' : 'shown');
  } catch {
    // 저장은 건너뛴다. 범례는 그대로 동작한다.
  }
}

export function Legend() {
  const [hidden, setHidden] = useState(readHidden);

  const toggle = (): void => setHidden((current) => !current);
  // 상태가 바뀔 때마다 기억한다. 상태 갱신 함수 안에서 저장하지 않는다 (순수해야 한다).
  useEffect(() => writeHidden(hidden), [hidden]);

  // H 로 접고 편다 (IME·수정 키·입력창 규칙은 isShortcut 이 처리한다).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (isShortcut(event, 'h')) {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (hidden) {
    return (
      <button type="button" className="legend legend-collapsed" onClick={toggle}>
        Legend [H]
      </button>
    );
  }

  return (
    <aside className="legend" aria-label="Legend">
      <header>
        <span>Legend</span>
        <button type="button" onClick={toggle} aria-label="Hide legend">
          [H]
        </button>
      </header>
      <dl>
        <dt>Size</dt>
        <dd>memory</dd>
        <dt>Glow, pulse</dt>
        <dd>CPU</dd>
        <dt>Inner orbit</dt>
        <dd>larger memory</dd>
        <dt>Outer ring</dt>
        <dd>CPU cores (blue → orange → white = load)</dd>
        <dt>Line</dt>
        <dd>group → core it runs on; sharp = measured, faint = estimated</dd>
      </dl>
    </aside>
  );
}
