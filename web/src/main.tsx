import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './dashboard/App';
import { useSystemStream } from './stream/useSystemStream';

// 스트림을 띄우는 일은 dashboard/ 밖에 둔다. 그래야 App 이 스토어만 읽는
// 순수한 화면으로 남고, 스트림 없이도 렌더된다.
function Root() {
  useSystemStream();
  return <App />;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Root />
  </StrictMode>,
);
