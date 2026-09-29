import { useSnapshotStore } from '../state/snapshotStore';
import { CoreOrb } from './CoreOrb';

// M6 스펙 9절. 코어 수가 바뀔 때만 다시 그린다 — selector 가 숫자 하나를 돌려준다.
export function CoreRing() {
  const count = useSnapshotStore((state) => state.current?.cores.length ?? 0);
  return (
    <>
      {Array.from({ length: count }, (_, index) => (
        <CoreOrb key={index} index={index} count={count} />
      ))}
    </>
  );
}
