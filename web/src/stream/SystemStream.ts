import { parseMessage, type Hello, type Snapshot } from '../protocol/schema';

export const CLEAN_RETRY_MS = 2000;
export const BACKOFF_START_MS = 500;
export const BACKOFF_MAX_MS = 8000;

export type ConnectionState = 'connecting' | 'open' | 'closed' | 'version-mismatch';

export interface StreamStatus {
  state: ConnectionState;
  hello: Hello | null;
  invalidCount: number;
  versionReceived: number | null;
}

export interface SocketLike {
  onopen: ((ev: unknown) => void) | null;
  onmessage: ((ev: { data: string }) => void) | null;
  onclose: ((ev: { code: number }) => void) | null;
  onerror: ((ev: unknown) => void) | null;
  close(code?: number): void;
}

export interface StreamOptions {
  url: string;
  createSocket: (url: string) => SocketLike;
  setTimer: (fn: () => void, ms: number) => number;
  clearTimer: (handle: number) => void;
  onSnapshot: (snapshot: Snapshot) => void;
  onStatus: (status: StreamStatus) => void;
  // 지터용. 주입하면 테스트가 결정적이 된다.
  random?: () => number;
}

// 연결·재연결·검증을 담당한다 — 계약서 7.2 절.
// React 를 import 하지 않는다. 소켓과 타이머를 주입받으므로 엔진 없이 테스트된다.
export class SystemStream {
  private readonly options: StreamOptions;
  private readonly random: () => number;

  private socket: SocketLike | null = null;
  private timer: number | null = null;
  private backoffMs = BACKOFF_START_MS;
  private stopped = false;

  private status: StreamStatus = {
    state: 'closed',
    hello: null,
    invalidCount: 0,
    versionReceived: null,
  };

  constructor(options: StreamOptions) {
    this.options = options;
    this.random = options.random ?? Math.random;
  }

  start(): void {
    this.stopped = false;
    // 재연결 타이머가 이미 걸려 있는데 그대로 connect() 하면 타이머가 나중에
    // 또 붙어 소켓이 둘이 된다. 먼저 취소한다.
    this.cancelTimer();
    this.connect();
  }

  stop(): void {
    this.stopped = true;
    this.backoffMs = BACKOFF_START_MS;
    this.cancelTimer();

    const socket = this.socket;
    this.socket = null;
    socket?.close();
  }

  private connect(): void {
    this.emit({ state: 'connecting' });

    const socket = this.options.createSocket(this.options.url);
    this.socket = socket;

    socket.onopen = () => {
      // 한 번이라도 붙었으면 백오프를 처음으로 되돌린다.
      this.backoffMs = BACKOFF_START_MS;
      this.emit({ state: 'open' });
    };

    socket.onmessage = (ev) => this.handleMessage(ev.data);

    // 브라우저는 error 뒤에 반드시 close 를 보낸다. 재연결은 close 에서만 건다.
    socket.onerror = () => undefined;

    socket.onclose = (ev) => this.handleClose(ev.code);
  }

  private handleMessage(text: string): void {
    // 닫기 직전에 큐에 들어가 있던 메시지가 뒤늦게 도착할 수 있다.
    // 이미 끝난 스트림이면 소비자에게 흘리지 않는다.
    if (this.stopped || this.status.state === 'version-mismatch') {
      return;
    }

    const outcome = parseMessage(text);

    if (outcome.kind === 'version-mismatch') {
      // 다시 붙어도 같은 버전이 온다. 재연결하지 않는다.
      this.emit({ state: 'version-mismatch', versionReceived: outcome.received });
      const socket = this.socket;
      this.socket = null;
      socket?.close();
      return;
    }

    if (outcome.kind === 'invalid') {
      // 한 프레임 때문에 스트림을 끊지 않는다. 개수만 남긴다.
      this.emit({ invalidCount: this.status.invalidCount + 1 });
      return;
    }

    if (outcome.message.type === 'hello') {
      this.emit({ hello: outcome.message });
      return;
    }

    this.options.onSnapshot(outcome.message);
  }

  private handleClose(code: number): void {
    if (this.status.state === 'version-mismatch' || this.stopped) {
      return;
    }

    this.socket = null;
    this.emit({ state: 'closed' });

    // 1000 은 엔진이 의도적으로 꺼진 것이다. 곧 다시 켜질 수 있으니 고정 간격으로
    // 빠르게 붙는다. 그 외는 원인을 모르므로 지수 백오프로 물러선다.
    let delay: number;
    if (code === 1000) {
      delay = CLEAN_RETRY_MS;
    } else {
      delay = this.jitter(this.backoffMs);
      this.backoffMs = Math.min(this.backoffMs * 2, BACKOFF_MAX_MS);
    }

    this.cancelTimer();
    this.timer = this.options.setTimer(() => {
      this.timer = null;
      if (!this.stopped) {
        this.connect();
      }
    }, delay);
  }

  // 여러 클라이언트가 같은 순간에 몰리지 않도록 지연의 50~100% 사이로 흩뜨린다.
  private jitter(base: number): number {
    return Math.round(base * (0.5 + this.random() * 0.5));
  }

  private cancelTimer(): void {
    if (this.timer !== null) {
      this.options.clearTimer(this.timer);
      this.timer = null;
    }
  }

  private emit(patch: Partial<StreamStatus>): void {
    this.status = { ...this.status, ...patch };
    this.options.onStatus(this.status);
  }
}
