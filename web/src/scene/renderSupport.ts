// 이 브라우저가 무엇을 그릴 수 있는지 모듈 로드 시 한 번만 판정한다. Universe 가 여러 번
// 마운트되어도(뷰 토글) 탐지용 컨텍스트를 반복해서 만들지 않는다.

interface RenderSupport {
  webgl: boolean;
  // 반정밀(half float) 렌더 타깃에 그릴 수 있는가 (M9 스펙 7절). 없으면 후처리는 8비트
  // 버퍼로 그린다.
  halfFloat: boolean;
}

function probe(): RenderSupport {
  try {
    const canvas = document.createElement('canvas');
    const gl2 = canvas.getContext('webgl2');
    // three 0.186 은 WebGL2 가 필요하다. WebGL1 만 되는 브라우저는 지원하지 않는 것으로 본다.
    if (gl2 === null) {
      return { webgl: false, halfFloat: false };
    }
    const halfFloat =
      gl2.getExtension('EXT_color_buffer_float') !== null ||
      gl2.getExtension('EXT_color_buffer_half_float') !== null;
    // 탐지용 컨텍스트를 그대로 두면 Universe 가 마운트될 때마다 하나씩
    // 새어 나간다. 판정이 끝나면 바로 반납한다.
    gl2.getExtension('WEBGL_lose_context')?.loseContext();
    return { webgl: true, halfFloat };
  } catch {
    return { webgl: false, halfFloat: false };
  }
}

export const renderSupport: RenderSupport = probe();
