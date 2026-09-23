import { useEffect, useState } from 'react';

import { sample, type InterpolatedSnapshot } from './interpolator';
import { useSnapshotStore } from './snapshotStore';

// 보간기를 100ms 마다 한 번 호출해 결과만 React 상태에 넣는다.
// React 상태에 들어가는 것은 보간기의 출력이지 보간 과정이 아니다 —
// M4 는 같은 sample() 을 useFrame 안에서 상태 없이 부른다.
export function useInterpolated(intervalMs = 100): InterpolatedSnapshot | null {
  const [frame, setFrame] = useState<InterpolatedSnapshot | null>(null);

  useEffect(() => {
    const handle = window.setInterval(() => {
      const state = useSnapshotStore.getState();
      setFrame(
        sample(
          {
            previous: state.previous,
            current: state.current,
            arrivedAt: state.arrivedAt,
            intervalMs: state.intervalMs,
          },
          performance.now(),
        ),
      );
    }, intervalMs);

    return () => window.clearInterval(handle);
  }, [intervalMs]);

  return frame;
}
