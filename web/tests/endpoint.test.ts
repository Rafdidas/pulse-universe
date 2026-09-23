import { describe, expect, it } from 'vitest';

import { resolveEndpoint } from '../src/stream/endpoint';

describe('endpoint discovery', () => {
  it('uses the configured url when one is given', () => {
    const url = resolveEndpoint(
      { VITE_PULSE_WS_URL: 'ws://127.0.0.1:9000' },
      { protocol: 'http:', host: 'localhost:5173' },
    );

    expect(url).toBe('ws://127.0.0.1:9000');
  });

  it('derives from the page origin when nothing is configured', () => {
    // 배포 빌드에서는 엔진이 프론트엔드를 서빙하므로 same-origin 이다.
    const url = resolveEndpoint({}, { protocol: 'http:', host: '127.0.0.1:9000' });

    expect(url).toBe('ws://127.0.0.1:9000');
  });

  it('uses wss when the page is https', () => {
    const url = resolveEndpoint({}, { protocol: 'https:', host: 'example.test' });

    expect(url).toBe('wss://example.test');
  });

  it('treats an empty configured value as unset', () => {
    const url = resolveEndpoint(
      { VITE_PULSE_WS_URL: '' },
      { protocol: 'http:', host: '127.0.0.1:9000' },
    );

    expect(url).toBe('ws://127.0.0.1:9000');
  });
});
