import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { Shell } from './shell/Shell';
import { useSystemStream } from './stream/useSystemStream';

// 스트림을 띄우는 일은 화면 컴포넌트 밖에 둔다. 그래야 Shell 과 그 아래가
// 스토어만 읽는 순수한 화면으로 남고, 스트림 없이도 렌더된다.
function Root() {
  useSystemStream();
  return <Shell />;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Root />
  </StrictMode>,
);
