import { ConnectionBadge } from '../dashboard/ConnectionBadge';
import { useSnapshotStore } from '../state/snapshotStore';

// status·seq 구독을 Shell 에서 분리한 이유: seq 는 1 Hz 로 바뀌는데 Shell 이
// 그것을 구독하면 <Universe/> 까지 매초 재렌더된다 (스펙 4절 규칙 3 위반).
// 배지만 따로 이 컴포넌트에서 구독해 우주 트리를 건드리지 않는다.
export function UniverseBadge() {
  const status = useSnapshotStore((state) => state.status);
  const seq = useSnapshotStore((state) => state.current?.seq ?? null);

  return (
    <div className="shell-badge">
      <ConnectionBadge status={status} seq={seq} />
    </div>
  );
}
