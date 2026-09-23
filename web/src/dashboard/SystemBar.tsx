import type { InterpolatedSnapshot } from '../state/interpolator';
import { formatMb, formatPct } from './format';

interface Props {
  frame: InterpolatedSnapshot;
}

export function SystemBar({ frame }: Props) {
  const system = frame.system;

  return (
    <div className="system-bar">
      <span>cpu {formatPct(system.cpu_pct)} %</span>
      <span>
        mem {formatMb(system.mem_used_mb)} / {formatMb(system.mem_total_mb)} MB
      </span>
      <span>processes {system.process_total}</span>
      <span>threads {system.thread_total}</span>
    </div>
  );
}
