// 연출 진행도에 쓰는 이징. 입력은 0~1 로 잘라서 쓴다.
export function clamp01(x: number): number {
  return Math.min(1, Math.max(0, x));
}

// 빠르게 시작해 천천히 멈춘다. 형성처럼 "자리를 잡는" 움직임.
export function easeOutCubic(t: number): number {
  const u = 1 - clamp01(t);
  return 1 - u * u * u;
}

// 천천히 시작해 빠르게 끝난다. 수렴·붕괴처럼 "빨려 드는" 움직임.
export function easeInCubic(t: number): number {
  const c = clamp01(t);
  return c * c * c;
}
