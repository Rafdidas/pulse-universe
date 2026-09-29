import { OrbitControls, PerformanceMonitor, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useEffect, useState } from 'react';

import { isShortcut } from '../shell/shortcut';
import { useSnapshotStore } from '../state/snapshotStore';
import { OVERVIEW_POSE } from '../visual/camera';
import { dprFor } from '../visual/perf';
import { FocusPanel } from './FocusPanel';
import { PerfMeter } from './PerfMeter';
import { useFocusStore } from './focusStore';
import { renderSupport } from './renderSupport';
import { SceneRoot } from './SceneRoot';

// 화면에 직접 그리는 것은 후처리의 전체화면 사각형 하나뿐이다. 캔버스 자체
// 안티에일리어싱은 효과가 없고 비용만 든다 (M9 스펙 2.2절). MSAA 는 composer 가 한다.
const GL_OPTIONS = { antialias: false };

// M9 스펙 5절. 실제 fps 를 보고 dpr 을 [1, 기기 dpr(최대 2)] 사이에서 조정한다.
// 느린 GPU 의 안전장치다. 최대 해상도에서 시작한다. 정한 dpr 은 onDpr 로 Universe 의
// 상태에 올린다 (Canvas 에 직접 setDpr 하면 다음 렌더에서 dpr prop 으로 되돌아간다).
function AdaptiveResolution({ onDpr }: { onDpr: (dpr: number) => void }) {
  return (
    <PerformanceMonitor
      factor={1}
      onChange={({ factor }) => onDpr(dprFor(factor, window.devicePixelRatio))}
    />
  );
}

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);
  const focused = useFocusStore((state) => state.focusedKey !== null);
  // 자동 해상도가 정한 dpr. Canvas 는 렌더될 때마다 dpr prop 으로 되돌리므로 (R3F 9.8) 값은 여기서 들고 있다. 바뀌는 것은 PerformanceMonitor 가 factor 를 바꿀 때뿐이라 초당 한 번도 안 된다.
  const [dpr, setDpr] = useState(() => dprFor(1, window.devicePixelRatio));
  // 성능 표시 (M9 스펙 6절). 켜고 끄는 것만 React 상태다.
  const [showPerf, setShowPerf] = useState(false);

  // Esc 로 초점을 푼다 (M5 스펙 9.1). P 로 성능 표시를 켜고 끈다 (M9 스펙 6절).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        useFocusStore.getState().clear();
      }
      if (isShortcut(event, 'p')) {
        setShowPerf((shown) => !shown);
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (!renderSupport.webgl) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas
        dpr={dpr}
        gl={GL_OPTIONS}
        camera={{
          fov: 50,
          position: [OVERVIEW_POSE.position.x, OVERVIEW_POSE.position.y, OVERVIEW_POSE.position.z],
        }}
        // 빈 곳을 클릭하면 초점을 푼다. R3F 는 2px 넘게 끌면 이것을 부르지 않으므로
        // 궤도 조작은 초점을 풀지 않는다.
        onPointerMissed={() => useFocusStore.getState().clear()}
      >
        <color attach="background" args={['#03040a']} />
        <ambientLight intensity={0.2} />
        {/* 중심 점광원은 가운데의 큰 천체 안에 묻힌다. 방향광을 쓴다. */}
        <directionalLight position={[20, 30, 25]} intensity={1.1} />
        <Stars radius={120} depth={60} count={4000} factor={4} fade />
        <SceneRoot />
        <AdaptiveResolution onDpr={setDpr} />
        {showPerf && <PerfMeter />}
        <OrbitControls
          makeDefault
          enableDamping
          // 초점 중에는 자동 회전을 끈다. 카메라가 천체를 따라가는 중이다.
          autoRotate={autoRotate && !focused}
          autoRotateSpeed={0.3}
          onStart={() => setAutoRotate(false)}
        />
      </Canvas>
      <FocusPanel />
      {!hasSnapshot && <p className="universe-message">waiting for the first snapshot…</p>}
    </div>
  );
}
