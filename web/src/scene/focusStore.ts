import { create } from 'zustand';

// M5 스펙 9.1. 어느 그룹에 초점이 있는지. 장면과 Canvas 밖 패널이 함께 읽는다.
// 프레임 값(전환 진행도, 카메라 자세)은 여기 두지 않는다 — 그것은 장면의
// 가변 객체(FocusFrame)에 있다.
interface FocusState {
  focusedKey: string | null;
  focus: (key: string) => void;
  // 같은 천체를 다시 누르면 초점을 푼다.
  toggle: (key: string) => void;
  clear: () => void;
}

export const useFocusStore = create<FocusState>((set, get) => ({
  focusedKey: null,
  focus: (key) => set({ focusedKey: key }),
  toggle: (key) => set({ focusedKey: get().focusedKey === key ? null : key }),
  clear: () => set({ focusedKey: null }),
}));
