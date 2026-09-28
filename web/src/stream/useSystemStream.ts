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
      // 진짜 WebSocket 은 SocketLike 에 구조적으로 대입되지 않는다(핸들러 타입이
      // 반공변으로 검사됨). 어댑터로 감싸 컴파일러가 나머지 필드는 그대로
      // 검사하게 하고, 검증 불가능한 가정 한 줄만 명시적으로 단언한다.
      createSocket: (target) => {
        const socket = new WebSocket(target);

        const adapter: SocketLike = {
          onopen: null,
          onmessage: null,
          onclose: null,
          onerror: null,
          close: (code) => socket.close(code),
        };

        socket.onopen = (ev) => adapter.onopen?.(ev);
        // 엔진은 텍스트 프레임만 보낸다. 이 한 줄이 유일하게 컴파일러가
        // 확인해 줄 수 없는 가정이고, 나머지는 그대로 검사된다.
        socket.onmessage = (ev) => adapter.onmessage?.({ data: ev.data as string });
        socket.onclose = (ev) => adapter.onclose?.({ code: ev.code });
        socket.onerror = (ev) => adapter.onerror?.(ev);

        return adapter;
      },
      setTimer: (fn, ms) => window.setTimeout(fn, ms),
      clearTimer: (handle) => window.clearTimeout(handle),
      onSnapshot: (snapshot) =>
        useSnapshotStore.getState().pushSnapshot(snapshot, performance.now()),
      onStatus: (status) => {
        useSnapshotStore.getState().setStatus(status);
        if (status.hello !== null) {
          useSnapshotStore.getState().setIntervalMs(status.hello.interval_ms);
          // hello 는 언제나 스냅샷보다 먼저 도착한다 — 새 세션의 첫 스냅샷이
          // 오기 전에 스토어가 비워져 있어야 한다.
          useSnapshotStore.getState().beginSession(status.hello.session);
        }
        // 계약서 7.2 절: 버전이 맞지 않으면 데이터를 버리고 사용자에게 알린다.
        // 배너 밑에 마지막 유효 프레임이 계속 보이면 안 된다.
        if (status.state === 'version-mismatch') {
          useSnapshotStore.getState().clearSnapshots();
        }
      },
    });

    store.reset();
    stream.start();

    return () => stream.stop();
  }, []);
}
