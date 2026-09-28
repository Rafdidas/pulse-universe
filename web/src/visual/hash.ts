// key 를 [0, 1) 의 수로 바꾼다. 같은 key 는 언제나 같은 수가 되므로
// 새로고침해도 초기 배치·색조·위상이 같다. salt 를 바꾸면 같은 key 에서
// 서로 독립적인 수를 여러 개 뽑을 수 있다.
//
// FNV-1a 로 섞은 뒤 murmur3 의 마무리 단계로 비트를 한 번 더 흩는다.
// FNV-1a 만으로는 끝 글자만 다른 key(`app.exe:100`, `app.exe:101`)가
// 비슷한 수로 뭉친다.
export function hash01(key: string, salt = 0): number {
  let h = (0x811c9dc5 ^ salt) >>> 0;
  for (let i = 0; i < key.length; i += 1) {
    h ^= key.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  h ^= h >>> 16;
  h = Math.imul(h, 0x85ebca6b);
  h ^= h >>> 13;
  h = Math.imul(h, 0xc2b2ae35);
  h ^= h >>> 16;
  return (h >>> 0) / 0x100000000;
}
