import type { Flow } from '../protocol/schema';

interface Props {
  flows: Flow[];
}

export function FlowList({ flows }: Props) {
  if (flows.length === 0) {
    return <p className="empty">no flows</p>;
  }

  return (
    <ul className="flow-list">
      {flows.map((flow) => (
        <li key={`${flow.group}:${flow.core}`}>
          {flow.group} → core {flow.core} ({flow.weight.toFixed(2)}, {flow.source})
        </li>
      ))}
    </ul>
  );
}
