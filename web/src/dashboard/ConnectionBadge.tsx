import type { StreamStatus } from '../state/snapshotStore';

interface Props {
  status: StreamStatus;
  seq: number | null;
}

export function ConnectionBadge({ status, seq }: Props) {
  const hello = status.hello;

  return (
    <div className={`badge badge-${status.state}`}>
      <strong>{status.state}</strong>
      <span>seq {seq ?? '-'}</span>
      <span>interval {hello?.interval_ms ?? '-'} ms</span>
      <span>cores {hello?.core_count ?? '-'}</span>
      <span>{hello?.host.os ?? '-'}</span>
      <span>{hello === null ? 'elevated -' : hello.host.elevated ? 'elevated' : 'not elevated'}</span>
      <span>dropped {status.invalidCount}</span>
      {status.state === 'version-mismatch' && (
        <strong className="warn">
          protocol v{status.versionReceived} is not supported — reconnecting would not help
        </strong>
      )}
      {hello !== null && !hello.host.elevated && (
        <span className="warn">
          running unelevated: some processes report 0 MB because their handle cannot be opened
        </span>
      )}
    </div>
  );
}
