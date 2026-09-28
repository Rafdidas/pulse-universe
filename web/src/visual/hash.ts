// key を [0, 1) の数に変わる。同じ key は常に同じ数になるから
// 新しくロードしても初期配置・色調・位相が同じになる。salt を変わると、同じ key から
// 互いに独立した数を複数取ることができる。
//
// FNV-1a でまぜた後、murmur3 の仕上げ段階でビットをもう一度散らす。
// FNV-1a だけを使うと、最後の文字だけ違う key（`app.exe:100`、`app.exe:101`）が
// 類似した数でまとまる。
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
