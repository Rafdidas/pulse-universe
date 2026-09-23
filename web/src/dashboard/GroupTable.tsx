import { Fragment, useState } from 'react';

import type { InterpolatedGroup } from '../state/interpolator';
import { formatMb, formatPct } from './format';

interface Props {
  groups: InterpolatedGroup[];
}

export function GroupTable({ groups }: Props) {
  const [expanded, setExpanded] = useState<string | null>(null);

  return (
    <table className="group-table">
      <thead>
        <tr>
          <th>GROUP</th>
          <th>PID</th>
          <th>PROCS</th>
          <th>CPU%</th>
          <th>MEM MB</th>
          <th>THREADS</th>
          <th>ACCOUNT</th>
        </tr>
      </thead>
      <tbody>
        {groups.map((group) => (
          // 짧은 <> 문법은 key 를 받지 못한다. 그룹마다 행이 둘 이상 나오므로
          // Fragment 를 명시해 key 를 준다.
          <Fragment key={group.key}>
            <tr onClick={() => setExpanded(expanded === group.key ? null : group.key)}>
              <td>{group.name}</td>
              <td>{group.root_pid}</td>
              <td>{group.proc_count}</td>
              <td>{formatPct(group.cpu_pct)}</td>
              <td>{formatMb(group.mem_mb)}</td>
              <td>{group.thread_count}</td>
              <td>{group.account}</td>
            </tr>
            {expanded === group.key &&
              group.children.map((child) => (
                <tr key={`${group.key}:${child.pid}`} className="child-row">
                  <td>{`↳ ${child.name}`}</td>
                  <td>{child.pid}</td>
                  <td>{child.role}</td>
                  <td>{formatPct(child.cpu_pct)}</td>
                  <td>{formatMb(child.mem_mb)}</td>
                  <td>{child.threads}</td>
                  <td />
                </tr>
              ))}
          </Fragment>
        ))}
      </tbody>
    </table>
  );
}
