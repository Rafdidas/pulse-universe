// useFrame 우선순위. 작은 값이 먼저 돈다. 양수는 R3F 의 자동 렌더를 끄므로
// 전부 음수이고, 노드·위성 메시와 drei Html 은 기본값 0 에서 돈다.
//
//   scene       보간 → 존재 추적 → lifecycle → 레이아웃 → 버스트 생성
//   camera      Focus 전환 진행도로 카메라 자세 (노드 위치가 정해진 뒤)
//   satellites  위성 위치 (Focus 진행도가 정해진 뒤)
//   tooltip     툴팁 anchor (위성 위치가 정해진 뒤, Html 이 투영하기 전)
//   particles   입자 버퍼 (모든 기준점이 정해진 뒤)
//   postfx      심도 초점·세기 (카메라 target 과 Focus 강도가 정해진 뒤). 렌더는
//               EffectComposer 가 양수 우선순위에서 맡는다.
export const FRAME_PRIORITY = {
  scene: -1,
  camera: -0.8,
  satellites: -0.6,
  tooltip: -0.4,
  particles: -0.2,
  postfx: -0.1,
} as const;
