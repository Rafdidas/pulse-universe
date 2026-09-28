import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  build: {
    // three + R3F + drei 가 한 청크에 1.26 MB(gzip 349 KB)가 된다. 이 앱은 엔진이
    // 로컬 디스크에서 서빙하므로 크기가 로딩을 막지 않는다. 코드 분할은 M9 에서
    // 측정한 뒤 판단한다.
    chunkSizeWarningLimit: 1600,
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./tests/setup.ts'],
    projects: [
      {
        extends: true,
        test: {
          name: 'node',
          environment: 'node',
          // 순수 모듈의 테스트. DOM 이 없는 환경에서 돌아야 순수하다는 것이 증명된다.
          include: ['tests/schema.test.ts', 'tests/visual/**/*.test.ts'],
        },
      },
      {
        extends: true,
        test: {
          name: 'jsdom',
          environment: 'jsdom',
          include: ['tests/**/*.test.{ts,tsx}'],
          exclude: ['tests/schema.test.ts', 'tests/visual/**'],
        },
      },
    ],
  },
});
