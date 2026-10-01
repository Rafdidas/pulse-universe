// 화면 다듬기 스펙 3절 (D91). three 를 모르는 순수 모듈이다. 이름표를 별에서 천체로 향하는 화면
// 방향의 바깥쪽에 두고, 이름표끼리 겹치면 작은 천체의 것을 숨긴다. 단위는 모두 화면 px 이다.

// 천체 가장자리와 이름표 사이의 틈.
export const LABEL_GAP = 4;
// 겹침 판정에서 두 상자 사이에 두는 여유 (쌍마다 한 번 더한다).
export const LABEL_PADDING = 2;

export interface LabelInput {
  key: string;
  // 천체의 화면 중심과 화면 반지름.
  sx: number;
  sy: number;
  radiusPx: number;
  // 이름표 상자의 크기.
  width: number;
  height: number;
  // 지금 보이고 있는가. 보이는 이름표가 새 이름표보다 우선해서 깜빡임을 막는다.
  wasVisible: boolean;
}

export interface StarPoint {
  sx: number;
  sy: number;
}

export interface LabelPlacement {
  key: string;
  visible: boolean;
  // 이름표 상자의 화면 중심.
  cx: number;
  cy: number;
}

function overlaps(a: LabelPlacement, aSize: LabelInput, b: LabelPlacement, bSize: LabelInput): boolean {
  const reachX = (aSize.width + bSize.width) / 2 + LABEL_PADDING;
  const reachY = (aSize.height + bSize.height) / 2 + LABEL_PADDING;
  return Math.abs(a.cx - b.cx) < reachX && Math.abs(a.cy - b.cy) < reachY;
}

// 입력과 같은 순서로 배치를 돌려준다. 같은 입력이면 같은 출력이다.
export function placeLabels(inputs: readonly LabelInput[], star: StarPoint): LabelPlacement[] {
  const placements = inputs.map((input): LabelPlacement => {
    let dx = input.sx - star.sx;
    let dy = input.sy - star.sy;
    const length = Math.hypot(dx, dy);
    if (length < 1e-6) {
      dx = 0;
      dy = -1;
    } else {
      dx /= length;
      dy /= length;
    }
    // 상자의 중심에서 가장자리까지의 길이를 이 방향으로 재서 상자가 천체에 닿지 않게 민다.
    const half = Math.abs(dx) * (input.width / 2) + Math.abs(dy) * (input.height / 2);
    const distance = input.radiusPx + LABEL_GAP + half;
    return { key: input.key, visible: true, cx: input.sx + dx * distance, cy: input.sy + dy * distance };
  });

  // 우선순위: 지금 보이는 것, 큰 천체, key 순.
  const order = inputs.map((_, index) => index).sort((a, b) => {
    const left = inputs[a];
    const right = inputs[b];
    if (left.wasVisible !== right.wasVisible) {
      return left.wasVisible ? -1 : 1;
    }
    if (left.radiusPx !== right.radiusPx) {
      return right.radiusPx - left.radiusPx;
    }
    return left.key < right.key ? -1 : left.key > right.key ? 1 : 0;
  });

  const accepted: number[] = [];
  for (const index of order) {
    const clash = accepted.some((other) =>
      overlaps(placements[index], inputs[index], placements[other], inputs[other]),
    );
    if (clash) {
      placements[index].visible = false;
    } else {
      accepted.push(index);
    }
  }
  return placements;
}
