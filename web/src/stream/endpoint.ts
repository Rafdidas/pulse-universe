// 계약서 4.1 절. 개발 중에는 Vite 와 엔진이 다른 포트에 있어 환경변수가 필요하고,
// 배포에서는 엔진이 프론트엔드를 서빙하므로 같은 origin 에서 유도한다.
// 이 덕분에 주소가 코드에 박히지 않고, 배포 빌드에서 CORS 문제가 사라진다.
export function resolveEndpoint(
  env: { VITE_PULSE_WS_URL?: string },
  location: { protocol: string; host: string },
): string {
  const configured = env.VITE_PULSE_WS_URL;
  if (configured !== undefined && configured.length > 0) {
    return configured;
  }

  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${scheme}//${location.host}`;
}
