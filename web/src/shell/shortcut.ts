// 한 글자 단축키 판정. Shell(D: 뷰 전환)과 Universe(P: 성능 표시)가 함께 쓴다.

// 입력 중인 글자를 단축키로 가로채지 않는다.
function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))
  );
}

// 한글 IME 가 켜져 있으면 event.key 는 'ㅇ' 이나 조합 중 'Process' 로
// 읽혀 물리 키를 알 수 없다. 그래서 물리 키(event.code)로 맞춘다.
// 반대로 AZERTY·Dvorak 에서는 글자가 다른 물리 키에 있으므로 글자
// (event.key)도 받는다. 조합 중(isComposing)이거나 키를 누르고 있어
// 자동 반복(repeat)되는 입력, 수정 키와 함께 누른 입력은 무시한다.
export function isShortcut(event: KeyboardEvent, letter: string): boolean {
  if (event.ctrlKey || event.metaKey || event.altKey || isTyping(event.target)) {
    return false;
  }
  if (event.isComposing || event.repeat) {
    return false;
  }
  const lower = letter.toLowerCase();
  return (
    event.code === `Key${lower.toUpperCase()}` ||
    event.key === lower ||
    event.key === lower.toUpperCase()
  );
}
