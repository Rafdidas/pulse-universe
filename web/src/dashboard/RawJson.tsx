import { useState } from 'react';

import type { Snapshot } from '../protocol/schema';

interface Props {
  snapshot: Snapshot | null;
}

export function RawJson({ snapshot }: Props) {
  const [open, setOpen] = useState(false);

  return (
    <section className="raw-json">
      <button type="button" onClick={() => setOpen(!open)}>
        {open ? 'hide raw json' : 'show raw json'}
      </button>
      {open && <pre>{snapshot === null ? 'no snapshot yet' : JSON.stringify(snapshot, null, 2)}</pre>}
    </section>
  );
}
