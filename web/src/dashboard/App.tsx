import { useSnapshotStore } from '../state/snapshotStore';
import { useInterpolated } from '../state/useInterpolated';
import { ConnectionBadge } from './ConnectionBadge';
import { CoreGrid } from './CoreGrid';
import { FlowList } from './FlowList';
import { GroupTable } from './GroupTable';
import { LifecycleLog } from './LifecycleLog';
import { RawJson } from './RawJson';
import { SystemBar } from './SystemBar';
import './dashboard.css';

export function App() {
  const frame = useInterpolated();
  const status = useSnapshotStore((state) => state.status);
  const lifecycleLog = useSnapshotStore((state) => state.lifecycleLog);
  const current = useSnapshotStore((state) => state.current);

  return (
    <main className="dashboard">
      <h1>Pulse Universe — data check</h1>
      <ConnectionBadge status={status} seq={frame?.seq ?? null} />

      {frame === null ? (
        <p className="empty">waiting for the first snapshot…</p>
      ) : (
        <>
          <SystemBar frame={frame} />

          <h2>cores</h2>
          <CoreGrid cores={frame.cores} />

          <h2>groups</h2>
          <GroupTable groups={frame.groups} />

          <h2>flows</h2>
          <FlowList flows={frame.flows} />

          <h2>ambient services</h2>
          <p>
            {frame.ambient.service_proc_count} procs, {Math.round(frame.ambient.service_mem_mb)} MB
          </p>
        </>
      )}

      <h2>lifecycle</h2>
      <LifecycleLog entries={lifecycleLog} />

      <RawJson snapshot={current} />
    </main>
  );
}
