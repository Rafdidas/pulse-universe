import type { InterpolatedCore } from '../state/interpolator';
import { formatPct } from './format';

interface Props {
  cores: InterpolatedCore[];
}

export function CoreGrid({ cores }: Props) {
  return (
    <div className="core-grid">
      {cores.map((core) => (
        <div key={core.id} className="core-cell">
          <span className="core-id">{core.id}</span>
          <span className="core-pct">{formatPct(core.pct)}</span>
        </div>
      ))}
    </div>
  );
}
