// null 은 모름, 0 은 측정된 0 이다. 대시보드의 존재 이유가 숫자 검증이므로
// 둘이 같아 보이면 안 된다.
export function formatPct(value: number | null): string {
  if (value === null) {
    return '-';
  }
  return value.toFixed(1);
}

export function formatMb(value: number): string {
  return Math.round(value).toLocaleString('en-US');
}
