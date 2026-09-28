import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { Universe } from '../scene/Universe';
import { SceneErrorBoundary } from './SceneErrorBoundary';
import { UniverseBadge } from './UniverseBadge';
import { toggledHash, viewFromHash } from './view';
import './shell.css';

function subscribeToHash(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

function currentHash(): string {
  return window.location.hash;
}

// 입력 중인 글자를 단축키로 가로채지 않는다.
function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))
  );
}

// 우주와 대시보드 중 하나만 마운트한다. 숨긴 쪽을 남겨 두면 대시보드의
// 100 ms 타이머와 WebGL 렌더 루프가 함께 돈다.
// Shell 은 스냅샷마다 바뀌는 값(status, seq)을 구독하지 않는다 — 구독하면
// <Universe/> 까지 매초 재렌더된다(스펙 4절 규칙 3). 배지는 UniverseBadge 가
// 따로 구독한다.
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
        return;
      }
      // 한글 IME 가 켜져 있으면 event.key 는 'ㅇ' 이나 조합 중 'Process' 로
      // 읽혀 물리 키를 알 수 없다. event.code 는 자판 배열과 무관하므로
      // 이것으로 맞춘다. 조합 중(isComposing)이거나 키를 누르고 있어 자동
      // 반복(repeat)되는 입력은 무시한다 — 안 그러면 초당 30번씩 뷰가
      // 뒤집히며 WebGL 컨텍스트를 매번 새로 만든다.
      if (event.isComposing || event.repeat) {
        return;
      }
      if (event.code === 'KeyD') {
        toggle();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [toggle]);

  return (
    <>
      {view === 'dashboard' ? (
        <App />
      ) : (
        <>
          <SceneErrorBoundary>
            <Universe />
          </SceneErrorBoundary>
          <UniverseBadge />
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
