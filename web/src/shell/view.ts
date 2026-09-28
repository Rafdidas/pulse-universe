// 스펙 8절. 어느 화면을 보여줄지는 URL 해시가 정한다 — 새로고침과 북마크에
// 유지되고, 엔진의 SPA 폴백과 충돌하지 않는다.
export type View = 'universe' | 'dashboard';

export const DASHBOARD_HASH = '#dashboard';

export function viewFromHash(hash: string): View {
  return hash === DASHBOARD_HASH ? 'dashboard' : 'universe';
}

// 반대쪽 화면의 해시. 우주로 돌아갈 때는 해시를 비운다.
export function toggledHash(view: View): string {
  return view === 'dashboard' ? '' : DASHBOARD_HASH;
}
