import type { FocusFrame } from './sceneContext';

// 장면의 포인터 상호작용 상수. R3F 는 드래그 끝에 버튼을 뗀 것에도 onClick 을
// 부르므로, 움직인 거리(event.delta)가 이보다 크면 클릭으로 보지 않는다 (px).
// ProcessNode·SatelliteNode·CoreOrb 가 함께 쓴다.
export const CLICK_SLOP = 2;

// 초점이 잡히면 초점과 무관한 천체·코어의 발광·불투명도가 이만큼까지 줄어든다
// (M5 스펙 9.3). 1 − DIM_DEPTH × weight.
export const DIM_DEPTH = 0.75;

// 초점과 무관한 천체·선일수록 1 보다 작다. ProcessNode 와 FlowStreams 가 함께 쓴다 (M8 D46).
export function dimFor(focus: Pick<FocusFrame, 'key' | 'previousKey' | 't' | 'weight'>, key: string): number {
  // 초점이 A 에서 B 로 옮겨 가는 동안은 weight 가 1 로 유지되므로, 카메라 전환
  // 진행도 t 로 A 는 어두워지고 B 는 밝아지게 섞는다.
  if (focus.key !== null && focus.previousKey !== null && focus.previousKey !== focus.key) {
    if (key === focus.key) {
      return 1 - DIM_DEPTH * focus.weight * (1 - focus.t);
    }
    if (key === focus.previousKey) {
      return 1 - DIM_DEPTH * focus.weight * focus.t;
    }
    return 1 - DIM_DEPTH * focus.weight;
  }
  const center = focus.key ?? focus.previousKey;
  if (center === null || center === key) {
    return 1;
  }
  return 1 - DIM_DEPTH * focus.weight;
}
