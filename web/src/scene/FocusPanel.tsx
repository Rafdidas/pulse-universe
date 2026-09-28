import { formatMb, formatPct } from '../dashboard/format';
import { useInterpolated } from '../state/useInterpolated';
import { useFocusStore } from './focusStore';

// M5 스펙 9.5. Canvas 밖 DOM 패널. 초점이 없으면 아무것도 마운트하지 않는다 —
// 10 Hz 타이머는 초점이 있는 동안에만 돈다.
export function FocusPanel() {
  const focusedKey = useFocusStore((state) => state.focusedKey);
  if (focusedKey === null) {
    return null;
  }
  return <FocusPanelBody groupKey={focusedKey} />;
}

function FocusPanelBody({ groupKey }: { groupKey: string }) {
  const frame = useInterpolated();
  const clear = useFocusStore((state) => state.clear);
  const group = frame?.groups.find((g) => g.key === groupKey);
  if (group === undefined) {
    return null;
  }
  const children = [...group.children].sort((a, b) => b.mem_mb - a.mem_mb);

  return (
    <aside className="focus-panel" aria-label="focused group">
      <header>
        <strong>{group.name}</strong>
        <button type="button" onClick={clear} aria-label="close focus">
          ×
        </button>
      </header>
      <dl>
        <dt>pid</dt>
        <dd>{group.root_pid}</dd>
        <dt>account</dt>
        <dd>{group.account}</dd>
        <dt>memory</dt>
        <dd>{formatMb(group.mem_mb)} MB</dd>
        <dt>cpu</dt>
        <dd>{formatPct(group.cpu_pct)}%</dd>
        <dt>processes</dt>
        <dd>{group.proc_count}</dd>
        <dt>threads</dt>
        <dd>{group.thread_count}</dd>
      </dl>
      <p className="focus-panel-path">{group.image_path || '-'}</p>
      {children.length === 0 ? (
        <p className="focus-panel-empty">no child processes</p>
      ) : (
        <table>
          <thead>
            <tr>
              <th>child</th>
              <th>pid</th>
              <th>MB</th>
              <th>CPU %</th>
            </tr>
          </thead>
          <tbody>
            {children.map((child) => (
              <tr key={child.pid}>
                <td>{child.name}</td>
                <td>{child.pid}</td>
                <td>{formatMb(child.mem_mb)}</td>
                <td>{formatPct(child.cpu_pct)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </aside>
  );
}
