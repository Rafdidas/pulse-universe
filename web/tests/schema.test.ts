import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { parseMessage, PROTOCOL_VERSION, SnapshotSchema } from '../src/protocol/schema';

const fixturePath = fileURLToPath(new URL('./fixtures/snapshot.json', import.meta.url));
const fixtureText = readFileSync(fixturePath, 'utf8');

function helloText(overrides: Record<string, unknown> = {}): string {
  return JSON.stringify({
    type: 'hello',
    v: PROTOCOL_VERSION,
    interval_ms: 1000,
    core_count: 28,
    capabilities: { thread_mapping: 'estimated' },
    host: { os: 'Windows', elevated: false },
    session: 'abc123deadbeef01',
    ...overrides,
  });
}

describe('snapshot schema', () => {
  it('accepts a real engine payload', () => {
    const outcome = parseMessage(fixtureText);
    expect(outcome.kind).toBe('message');
  });

  it('exposes the contract fields of a real payload', () => {
    const snapshot = SnapshotSchema.parse(JSON.parse(fixtureText));

    expect(snapshot.type).toBe('snapshot');
    expect(snapshot.v).toBe(PROTOCOL_VERSION);
    expect(snapshot.groups.length).toBeGreaterThan(0);
    expect(snapshot.cores.length).toBeGreaterThan(0);
    expect(snapshot.system.mem_total_mb).toBeGreaterThan(0);
    expect(snapshot.ambient.service_proc_count).toBeGreaterThanOrEqual(0);
  });

  it('keeps null distinct from zero', () => {
    const base = JSON.parse(fixtureText);
    base.system.cpu_pct = null;
    base.groups[0].cpu_pct = 0;

    const snapshot = SnapshotSchema.parse(base);

    expect(snapshot.system.cpu_pct).toBeNull();
    expect(snapshot.groups[0].cpu_pct).toBe(0);
  });

  it('rejects a payload missing a required field', () => {
    const broken = JSON.parse(fixtureText);
    delete broken.system.thread_total;

    const outcome = parseMessage(JSON.stringify(broken));

    expect(outcome.kind).toBe('invalid');
  });

  it('rejects a payload with a wrong field type', () => {
    const broken = JSON.parse(fixtureText);
    broken.groups[0].proc_count = 'many';

    const outcome = parseMessage(JSON.stringify(broken));

    expect(outcome.kind).toBe('invalid');
  });

  it('reports a version mismatch separately from a bad shape', () => {
    // 버전이 다르면 재연결해도 같은 버전이 온다. 호출자가 다르게 처리해야 한다.
    const outcome = parseMessage(JSON.stringify({ type: 'snapshot', v: 2 }));

    expect(outcome).toEqual({ kind: 'version-mismatch', received: 2 });
  });

  it('rejects text that is not json', () => {
    const outcome = parseMessage('not json at all');

    expect(outcome.kind).toBe('invalid');
  });

  it('rejects a message with no type or v', () => {
    const outcome = parseMessage(JSON.stringify({ hello: 'there' }));

    expect(outcome.kind).toBe('invalid');
  });
});

describe('hello schema', () => {
  it('accepts a well formed hello', () => {
    const outcome = parseMessage(helloText());

    expect(outcome.kind).toBe('message');
    if (outcome.kind === 'message' && outcome.message.type === 'hello') {
      expect(outcome.message.interval_ms).toBe(1000);
      expect(outcome.message.core_count).toBe(28);
      expect(outcome.message.host.elevated).toBe(false);
      expect(outcome.message.capabilities.thread_mapping).toBe('estimated');
    } else {
      throw new Error('expected a hello message');
    }
  });

  it('rejects a hello with an unknown thread mapping', () => {
    const outcome = parseMessage(helloText({ capabilities: { thread_mapping: 'guessed' } }));

    expect(outcome.kind).toBe('invalid');
  });
});
