import { beforeEach, describe, expect, it, vi } from 'vitest';

import {
  BACKOFF_MAX_MS,
  BACKOFF_START_MS,
  CLEAN_RETRY_MS,
  SystemStream,
  type SocketLike,
  type StreamStatus,
} from '../src/stream/SystemStream';
import { PROTOCOL_VERSION, type Snapshot } from '../src/protocol/schema';

class FakeSocket implements SocketLike {
  onopen: ((ev: unknown) => void) | null = null;
  onmessage: ((ev: { data: string }) => void) | null = null;
  onclose: ((ev: { code: number }) => void) | null = null;
  onerror: ((ev: unknown) => void) | null = null;
  closedWith: number | null = null;

  close(code?: number): void {
    this.closedWith = code ?? 1000;
  }
}

interface ScheduledTimer {
  fn: () => void;
  ms: number;
  handle: number;
}

class Harness {
  sockets: FakeSocket[] = [];
  timers: ScheduledTimer[] = [];
  snapshots: Snapshot[] = [];
  statuses: StreamStatus[] = [];
  private nextHandle = 1;

  readonly stream: SystemStream;

  constructor() {
    this.stream = new SystemStream({
      url: 'ws://test',
      createSocket: () => {
        const socket = new FakeSocket();
        this.sockets.push(socket);
        return socket;
      },
      setTimer: (fn, ms) => {
        const handle = this.nextHandle++;
        this.timers.push({ fn, ms, handle });
        return handle;
      },
      clearTimer: (handle) => {
        this.timers = this.timers.filter((t) => t.handle !== handle);
      },
      onSnapshot: (snapshot) => this.snapshots.push(snapshot),
      onStatus: (status) => this.statuses.push(status),
      random: () => 1, // 지터를 최대치로 고정해 결정적으로 만든다
    });
  }

  get socket(): FakeSocket {
    return this.sockets[this.sockets.length - 1];
  }

  get status(): StreamStatus {
    return this.statuses[this.statuses.length - 1];
  }

  get pendingTimer(): ScheduledTimer | undefined {
    return this.timers[this.timers.length - 1];
  }

  fireTimer(): void {
    const timer = this.timers.pop();
    if (!timer) {
      throw new Error('no timer scheduled');
    }
    timer.fn();
  }

  open(): void {
    this.socket.onopen?.({});
  }

  deliver(text: string): void {
    this.socket.onmessage?.({ data: text });
  }

  closeWith(code: number): void {
    this.socket.onclose?.({ code });
  }
}

function helloText(): string {
  return JSON.stringify({
    type: 'hello',
    v: PROTOCOL_VERSION,
    interval_ms: 1000,
    core_count: 28,
    capabilities: { thread_mapping: 'estimated' },
    host: { os: 'Windows', elevated: true },
  });
}

function snapshotText(seq: number): string {
  return JSON.stringify({
    type: 'snapshot',
    v: PROTOCOL_VERSION,
    seq,
    t: 1790000000000,
    system: {
      cpu_pct: 10,
      mem_used_mb: 100,
      mem_total_mb: 200,
      process_total: 5,
      thread_total: 50,
    },
    cores: [{ id: 0, pct: 12.5 }],
    groups: [],
    flows: [],
    lifecycle: { spawned: [], terminated: [] },
    ambient: { service_proc_count: 0, service_mem_mb: 0 },
  });
}

describe('SystemStream', () => {
  let harness: Harness;

  beforeEach(() => {
    harness = new Harness();
    vi.spyOn(console, 'error').mockImplementation(() => undefined);
  });

  it('opens a socket on start', () => {
    harness.stream.start();

    expect(harness.sockets).toHaveLength(1);
    expect(harness.status.state).toBe('connecting');
  });

  it('reports open once the socket opens', () => {
    harness.stream.start();
    harness.open();

    expect(harness.status.state).toBe('open');
  });

  it('keeps the hello for later use', () => {
    // interval_ms 는 보간기의 분모가 된다. 하드코딩하지 않는다.
    harness.stream.start();
    harness.open();
    harness.deliver(helloText());

    expect(harness.status.hello?.interval_ms).toBe(1000);
    expect(harness.status.hello?.core_count).toBe(28);
    expect(harness.status.hello?.host.elevated).toBe(true);
  });

  it('forwards snapshots', () => {
    harness.stream.start();
    harness.open();
    harness.deliver(snapshotText(7));

    expect(harness.snapshots).toHaveLength(1);
    expect(harness.snapshots[0].seq).toBe(7);
  });

  it('drops an invalid message and keeps the connection', () => {
    // 한 프레임 때문에 스트림을 끊지 않는다 — 계약서 7.2 절.
    harness.stream.start();
    harness.open();
    harness.deliver('{ not json');

    expect(harness.status.invalidCount).toBe(1);
    expect(harness.status.state).toBe('open');
    expect(harness.socket.closedWith).toBeNull();
  });

  it('stops for good on a version mismatch', () => {
    // 다시 붙어도 같은 버전이 온다.
    harness.stream.start();
    harness.open();
    harness.deliver(JSON.stringify({ type: 'snapshot', v: 2 }));

    expect(harness.status.state).toBe('version-mismatch');
    expect(harness.status.versionReceived).toBe(2);
    expect(harness.socket.closedWith).not.toBeNull();

    harness.closeWith(1000);
    expect(harness.timers).toHaveLength(0);
    expect(harness.sockets).toHaveLength(1);
  });

  it('retries at a steady interval after a clean close', () => {
    // 1000 은 엔진이 의도적으로 꺼진 것이다. 곧 다시 켜질 수 있으니 빠르게 붙는다.
    harness.stream.start();
    harness.open();
    harness.closeWith(1000);

    expect(harness.pendingTimer?.ms).toBe(CLEAN_RETRY_MS);

    harness.fireTimer();
    expect(harness.sockets).toHaveLength(2);
  });

  it('backs off exponentially after an unclean close', () => {
    // 1006 은 원인을 모른다. 두드리지 않는다.
    harness.stream.start();
    harness.open();

    harness.closeWith(1006);
    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS);
    harness.fireTimer();

    harness.closeWith(1006);
    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS * 2);
    harness.fireTimer();

    harness.closeWith(1006);
    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS * 4);
  });

  it('caps the backoff', () => {
    harness.stream.start();
    harness.open();

    for (let i = 0; i < 10; i += 1) {
      harness.closeWith(1006);
      harness.fireTimer();
    }
    harness.closeWith(1006);

    expect(harness.pendingTimer?.ms).toBe(BACKOFF_MAX_MS);
  });

  it('applies jitter below the full delay', () => {
    // random() 이 0 이면 지연이 절반이 된다. 여러 클라이언트가 동시에
    // 재접속을 시도하는 것을 흩뜨리기 위한 것이다.
    let scheduled = 0;
    let socket!: FakeSocket;

    const jittered = new SystemStream({
      url: 'ws://test',
      createSocket: () => {
        socket = new FakeSocket();
        return socket;
      },
      setTimer: (_fn, ms) => {
        scheduled = ms;
        return 1;
      },
      clearTimer: () => undefined,
      onSnapshot: () => undefined,
      onStatus: () => undefined,
      random: () => 0,
    });

    jittered.start();
    socket.onclose?.({ code: 1006 });

    expect(scheduled).toBe(BACKOFF_START_MS / 2);
  });

  it('resets the backoff after a successful open', () => {
    harness.stream.start();
    harness.open();

    harness.closeWith(1006);
    harness.fireTimer();
    harness.closeWith(1006);
    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS * 2);
    harness.fireTimer();

    harness.open();
    harness.closeWith(1006);

    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS);
  });

  it('cancels a pending retry when stopped', () => {
    harness.stream.start();
    harness.open();
    harness.closeWith(1006);
    expect(harness.timers).toHaveLength(1);

    harness.stream.stop();

    expect(harness.timers).toHaveLength(0);
  });

  it('does not reconnect after stop', () => {
    harness.stream.start();
    harness.open();
    harness.stream.stop();
    harness.closeWith(1006);

    expect(harness.timers).toHaveLength(0);
    expect(harness.sockets).toHaveLength(1);
  });

  it('does not open a second socket when start is called with a retry pending', () => {
    harness.stream.start();
    harness.open();
    harness.closeWith(1006);
    expect(harness.timers).toHaveLength(1);

    harness.stream.start();

    expect(harness.timers).toHaveLength(0);
    expect(harness.sockets).toHaveLength(2);
  });

  it('ignores a message arriving on the old socket after a version mismatch', () => {
    harness.stream.start();
    harness.open();
    const stale = harness.socket;
    harness.deliver(JSON.stringify({ type: 'snapshot', v: 2 }));

    stale.onmessage?.({ data: snapshotText(9) });

    expect(harness.snapshots).toHaveLength(0);
  });

  it('ignores a message arriving after stop', () => {
    harness.stream.start();
    harness.open();
    const stale = harness.socket;
    harness.stream.stop();

    stale.onmessage?.({ data: snapshotText(9) });

    expect(harness.snapshots).toHaveLength(0);
  });

  it('starts from the base delay after a stop and restart', () => {
    harness.stream.start();
    harness.open();
    harness.closeWith(1006);
    harness.fireTimer();
    harness.closeWith(1006);
    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS * 2);

    harness.stream.stop();
    harness.stream.start();
    // 여기서 open() 을 부르지 않는다. onopen 도 백오프를 리셋하므로
    // 부르면 stop() 쪽 리셋이 검증되지 않는다.
    harness.closeWith(1006);

    expect(harness.pendingTimer?.ms).toBe(BACKOFF_START_MS);
  });

  it('discards an open socket when start is called again', () => {
    harness.stream.start();
    harness.open();
    const first = harness.socket;

    harness.stream.start();

    expect(first.closedWith).not.toBeNull();
    expect(harness.sockets).toHaveLength(2);
  });

  it('ignores events from a socket that has been replaced', () => {
    // 버려진 소켓의 핸들러는 여전히 같은 인스턴스를 가리킨다.
    // 뒤늦은 이벤트가 현재 연결의 상태를 건드리면 안 된다.
    harness.stream.start();
    harness.open();
    const stale = harness.socket;

    harness.stream.start();
    harness.open();

    stale.onclose?.({ code: 1006 });
    stale.onmessage?.({ data: snapshotText(3) });

    expect(harness.timers).toHaveLength(0);
    expect(harness.snapshots).toHaveLength(0);
    expect(harness.status.state).toBe('open');
  });
});
