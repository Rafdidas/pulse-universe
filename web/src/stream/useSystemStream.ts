import { useEffect } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
import { resolveEndpoint } from './endpoint';
import { SystemStream, type SocketLike } from './SystemStream';

// SystemStream 을 컴포넌트 수명에 묶는 얇은 훅. 로직은 전부 SystemStream 에 있다.
export function useSystemStream(): void {
  useEffect(() => {
    const store = useSnapshotStore.getState();
    // Vite 가 주입하는 env 는 인덱스 시그니처를 갖고 있어 그대로 넘기면
    // 타입이 넓다. 필요한 키만 뽑아 좁힌다.
    const url = resolveEndpoint(
      import.meta.env as { VITE_PULSE_WS_URL?: string },
      window.location,
    );

    const stream = new SystemStream({
      url,
      // WebSocket 의 onopen/onmessage/... 이벤트 타입(Event 등)은 SocketLike 가
      // 요구하는 unknown 보다 좁아 구조적으로 대입되지 않는다. 실제 런타임 형태는
      // SocketLike 와 호환되므로 여기서만 단언한다.
      createSocket: (target) => new WebSocket(target) as unknown as SocketLike,
      setTimer: (fn, ms) => window.setTimeout(fn, ms),
      clearTimer: (handle) => window.clearTimeout(handle),
      onSnapshot: (snapshot) =>
        useSnapshotStore.getState().pushSnapshot(snapshot, performance.now()),
      onStatus: (status) => {
        useSnapshotStore.getState().setStatus(status);
        if (status.hello !== null) {
          useSnapshotStore.getState().setIntervalMs(status.hello.interval_ms);
        }
      },
    });

    store.reset();
    stream.start();

    return () => stream.stop();
  }, []);
}
