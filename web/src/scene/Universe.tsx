import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useState } from 'react';

import { useSnapshotStore } from '../state/snapshotStore';
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

  if (!webglAvailable) {
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
