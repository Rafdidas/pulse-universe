import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useEffect, useState } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
import { OVERVIEW_POSE } from '../visual/camera';
import { FocusPanel } from './FocusPanel';
import { useFocusStore } from './focusStore';
import { SceneRoot } from './SceneRoot';

function probeWebgl(): boolean {
  try {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl2') ?? canvas.getContext('webgl');
    if (gl === null) {
      return false;
    }
    // 탐지용 컨텍스트를 그대로 두면 Universe 가 마운트될 때마다 하나씩
    // 새어 나간다. 판정이 끝나면 바로 반납한다.
    (gl as WebGLRenderingContext).getExtension('WEBGL_lose_context')?.loseContext();
    return true;
  } catch {
    return false;
  }
}

// 모듈 로드 시 한 번만 판정한다. Universe 가 여러 번 마운트되어도(뷰 토글)
// 컨텍스트를 반복해서 만들지 않는다.
const webglAvailable = probeWebgl();

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);
  const focused = useFocusStore((state) => state.focusedKey !== null);

  // Esc 로 초점을 푼다 (M5 스펙 9.1).
  useEffect(() => {
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        useFocusStore.getState().clear();
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  if (!webglAvailable) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas
        dpr={[1, 2]}
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
