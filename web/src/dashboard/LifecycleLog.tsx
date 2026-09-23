import type { LifecycleEntry } from '../state/snapshotStore';

interface Props {
  entries: LifecycleEntry[];
}

export function LifecycleLog({ entries }: Props) {
  if (entries.length === 0) {
    return <p className="empty">nothing started or stopped yet</p>;
  }

  return (
    <ul className="lifecycle-log">
      {[...entries].reverse().map((entry, index) => (
        <li key={`${entry.seq}:${entry.label}:${index}`} className={`entry-${entry.kind}`}>
          <span className="seq">{entry.seq}</span>
          <span className="kind">{entry.kind}</span>
          <span>{entry.label}</span>
        </li>
      ))}
    </ul>
  );
}
