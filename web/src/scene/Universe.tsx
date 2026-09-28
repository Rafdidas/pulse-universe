import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useState } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
import { SceneRoot } from './SceneRoot';

function webglAvailable(): boolean {
  try {
    const canvas = document.createElement('canvas');
    return (canvas.getContext('webgl2') ?? canvas.getContext('webgl')) !== null;
  } catch {
    return false;
  }
}

// 스펙 7절. 장면의 틀: 배경, 별, 카메라, 조명, 조작. 천체는 SceneRoot 가 그린다.
export function Universe() {
  const hasSnapshot = useSnapshotStore((state) => state.current !== null);
  const [webgl] = useState(webglAvailable);
  // 사용자가 한 번 조작하면 자동 회전을 멈춘다. 보던 각도를 빼앗지 않는다.
  const [autoRotate, setAutoRotate] = useState(true);

  if (!webgl) {
    return (
      <div className="universe-message">
        WebGL unavailable — open <a href="#dashboard">#dashboard</a>
      </div>
    );
  }

  return (
    <div className="universe">
      <Canvas dpr={[1, 2]} camera={{ fov: 50, position: [0, 10, 58] }}>
        <color attach="background" args={['#03040a']} />
        <ambientLight intensity={0.2} />
        {/* 중심 점광원은 가운데의 큰 천체 안에 묻힌다. 방향광을 쓴다. */}
        <directionalLight position={[20, 30, 25]} intensity={1.1} />
        <Stars radius={120} depth={60} count={4000} factor={4} fade />
        <SceneRoot />
        <OrbitControls
          enableDamping
          autoRotate={autoRotate}
          autoRotateSpeed={0.3}
          onStart={() => setAutoRotate(false)}
        />
      </Canvas>
      {!hasSnapshot && <p className="universe-message">waiting for the first snapshot…</p>}
    </div>
  );
}
