import type { SystemTotals } from '../protocol/schema';
import type { OrbitPlan } from './solar';

// 이름표·별 정보 스펙 4절. 이름표는 가장 안쪽 궤도(= 가장 큰 천체들)에만 항상 보인다.
// 40 개 전부에 띄우면 글자로 덮인다.
export const MAX_LABELS = 8;

// 궤도 안의 자리는 key 순으로 고정되어 있으므로 대상이 깜빡이지 않는다.
export function labelKeys(plan: OrbitPlan, limit: number = MAX_LABELS): string[] {
  const first = plan.rings[0];
  if (first === undefined) {
    return [];
  }
  return first.keys.slice(0, Math.max(0, limit));
}

const MB_PER_GB = 1024;

function gb(mb: number): string {
  return (mb / MB_PER_GB).toFixed(1);
}

// 별 툴팁의 세부. 숫자는 대시보드와 같은 방식(CPU 는 소수 한 자리, 모름은 '-')이다.
export function systemDetail(system: SystemTotals): string {
  const cpu = system.cpu_pct === null ? '-' : system.cpu_pct.toFixed(1);
  return (
    `CPU ${cpu}% · Mem ${gb(system.mem_used_mb)} / ${gb(system.mem_total_mb)} GB` +
    ` · ${system.process_total.toLocaleString('en-US')} procs` +
    ` · ${system.thread_total.toLocaleString('en-US')} threads`
  );
}
