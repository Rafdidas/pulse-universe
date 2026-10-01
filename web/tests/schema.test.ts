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
    capabilities: { thread_mapping: 'estimated', network_traffic: 'unavailable' },
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
    const outcome = parseMessage(
      helloText({ capabilities: { thread_mapping: 'guessed', network_traffic: 'unavailable' } }),
    );

    expect(outcome.kind).toBe('invalid');
  });
});

describe('network block', () => {
  it('exposes the endpoints and connections of a real payload', () => {
    const snapshot = SnapshotSchema.parse(JSON.parse(fixtureText));

    expect(snapshot.network.traffic).toBe('measured');
    expect(snapshot.network.summary.endpoints).toBe(50);
    expect(snapshot.network.endpoints).toHaveLength(3);
    expect(snapshot.network.endpoints[0].ports).toEqual([443]);
    expect(snapshot.network.endpoints[1].private).toBe(true);
    expect(snapshot.network.connections).toHaveLength(4);
    expect(snapshot.network.connections[2].proto).toBe('udp');
  });

  it('links every connection to a group that is in the snapshot', () => {
    const snapshot = SnapshotSchema.parse(JSON.parse(fixtureText));
    const keys = new Set(snapshot.groups.map((group) => group.key));

    for (const connection of snapshot.network.connections) {
      expect(keys.has(connection.group)).toBe(true);
    }
    for (const endpoint of snapshot.network.endpoints) {
      for (const key of endpoint.groups) {
        expect(keys.has(key)).toBe(true);
      }
    }
  });

  it('keeps an unmeasured rate (null) distinct from a measured zero', () => {
    const base = JSON.parse(fixtureText);
    base.network.connections[0].down_bps = 0;
    base.network.connections[1].down_bps = null;

    const snapshot = SnapshotSchema.parse(base);

    expect(snapshot.network.connections[0].down_bps).toBe(0);
    expect(snapshot.network.connections[1].down_bps).toBeNull();
  });

  it('rejects a payload without a network block', () => {
    const broken = JSON.parse(fixtureText);
    delete broken.network;

    expect(parseMessage(JSON.stringify(broken)).kind).toBe('invalid');
  });

  it('rejects an unknown traffic state, protocol or wrong field types', () => {
    const traffic = JSON.parse(fixtureText);
    traffic.network.traffic = 'estimated';
    expect(parseMessage(JSON.stringify(traffic)).kind).toBe('invalid');

    const proto = JSON.parse(fixtureText);
    proto.network.connections[0].proto = 'icmp';
    expect(parseMessage(JSON.stringify(proto)).kind).toBe('invalid');

    const ports = JSON.parse(fixtureText);
    ports.network.endpoints[0].ports = '443';
    expect(parseMessage(JSON.stringify(ports)).kind).toBe('invalid');

    const rate = JSON.parse(fixtureText);
    rate.network.summary.down_bps = '12';
    expect(parseMessage(JSON.stringify(rate)).kind).toBe('invalid');
  });

  it('accepts the empty block of an engine without a scanner', () => {
    const base = JSON.parse(fixtureText);
    base.network = {
      traffic: 'unavailable',
      summary: { connections: 0, established: 0, endpoints: 0, udp_sockets: 0, down_bps: null, up_bps: null },
      endpoints: [],
      connections: [],
    };

    expect(parseMessage(JSON.stringify(base)).kind).toBe('message');
  });
});

describe('network capability in hello', () => {
  it('reads network_traffic', () => {
    const outcome = parseMessage(helloText({ capabilities: { thread_mapping: 'estimated', network_traffic: 'measured' } }));

    expect(outcome.kind).toBe('message');
    if (outcome.kind === 'message' && outcome.message.type === 'hello') {
      expect(outcome.message.capabilities.network_traffic).toBe('measured');
    } else {
      throw new Error('expected a hello message');
    }
  });

  it('rejects an unknown network_traffic value or a missing one', () => {
    expect(
      parseMessage(helloText({ capabilities: { thread_mapping: 'estimated', network_traffic: 'estimated' } })).kind,
    ).toBe('invalid');
    expect(parseMessage(helloText({ capabilities: { thread_mapping: 'estimated' } })).kind).toBe('invalid');
  });
});
