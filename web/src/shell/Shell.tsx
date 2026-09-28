import { useCallback, useEffect, useSyncExternalStore } from 'react';

import { App } from '../dashboard/App';
import { ConnectionBadge } from '../dashboard/ConnectionBadge';
import { Universe } from '../scene/Universe';
import { useSnapshotStore } from '../state/snapshotStore';
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
export function Shell() {
  const view = viewFromHash(useSyncExternalStore(subscribeToHash, currentHash));
  const status = useSnapshotStore((state) => state.status);
  const seq = useSnapshotStore((state) => state.current?.seq ?? null);

  const toggle = useCallback(() => {
    window.location.hash = toggledHash(view);
  }, [view]);

  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
        return;
      }
      if (event.key === 'd' || event.key === 'D') {
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
          <Universe />
          <div className="shell-badge">
            <ConnectionBadge status={status} seq={seq} />
          </div>
        </>
      )}
      <button type="button" className="shell-toggle" onClick={toggle}>
        {view === 'dashboard' ? 'universe' : 'dashboard'} (D)
      </button>
    </>
  );
}
