import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import type { WebGLRenderer } from 'three';

import { FrameStats, formatPerf, type FrameSummary } from '../visual/perf';
import { FRAME_PRIORITY } from './framePriority';

// 표시를 이 간격(초)마다 갱신한다. 매 프레임 바꾸면 숫자를 읽을 수 없다.
const REFRESH_SEC = 0.5;

// M9 스펙 6절. 성능 표시. 켜져 있을 때만 마운트된다 (P 키, Universe).
// 값은 React 상태를 거치지 않고 body 에 붙인 요소의 글자를 직접 바꾼다.
// draw call·삼각형은 composer 가 한 프레임에 여러 번 렌더하므로, 자동 초기화를 끄고
// 프레임 시작에 앞 프레임의 합계를 읽은 뒤 비운다.
export function PerfMeter() {
  const stats = useMemo(() => new FrameStats(), []);
  const element = useRef<HTMLDivElement | null>(null);
  const renderer = useRef<WebGLRenderer | null>(null);
  const last = useRef<FrameSummary | null>(null);

  useEffect(() => {
    const div = document.createElement('div');
    div.className = 'perf-meter';
    div.textContent = formatPerf(null, 1, { calls: 0, triangles: 0 });
    document.body.appendChild(div);
    element.current = div;
    return () => {
      div.remove();
      element.current = null;
      // 표시를 끄면 three 의 기본 동작(렌더마다 초기화)으로 되돌린다.
      const gl = renderer.current;
      if (gl !== null) {
        gl.info.autoReset = true;
        gl.info.reset();
      }
    };
  }, []);

  useFrame((state, delta) => {
    const gl = state.gl;
    renderer.current = gl;
    const info = gl.info;
    if (info.autoReset) {
      // 첫 프레임: 이번에는 합계가 없다. 다음 프레임부터 센다.
      info.autoReset = false;
      info.reset();
      return;
    }
    const counts = { calls: info.render.calls, triangles: info.render.triangles };
    info.reset();
    stats.add(delta);
    if (stats.elapsedSec < REFRESH_SEC) {
      return;
    }
    last.current = stats.take() ?? last.current;
    if (element.current !== null) {
      element.current.textContent = formatPerf(last.current, gl.getPixelRatio(), counts);
    }
  }, FRAME_PRIORITY.meter);

  return null;
}
