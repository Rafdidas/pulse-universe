// 장면의 포인터 상호작용 상수. R3F 는 드래그 끝에 버튼을 뗀 것에도 onClick 을
// 부르므로, 움직인 거리(event.delta)가 이보다 크면 클릭으로 보지 않는다 (px).
// ProcessNode·SatelliteNode·CoreOrb 가 함께 쓴다.
export const CLICK_SLOP = 2;

// 초점이 잡히면 초점과 무관한 천체·코어의 발광·불투명도가 이만큼까지 줄어든다
// (M5 스펙 9.3). 1 − DIM_DEPTH × weight.
export const DIM_DEPTH = 0.75;
