import { Component, type ErrorInfo, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
}

interface State {
  hasError: boolean;
}

// R3F 는 렌더러 생성 실패나 장면 렌더 중 던진 에러를 바깥으로 다시 던진다.
// 경계가 없으면 리액트가 루트 전체를 언마운트해 토글 버튼과 D 단축키까지
// 함께 사라지고, 장면이 이상할 때 피신할 대시보드(스펙 10절, D22)에 닿을
// 수 없게 된다. <Universe/> 만 감싸고 토글 버튼은 밖에 둔다.
export class SceneErrorBoundary extends Component<Props, State> {
  state: State = { hasError: false };

  static getDerivedStateFromError(): State {
    return { hasError: true };
  }

  componentDidCatch(error: unknown, info: ErrorInfo) {
    console.error('SceneErrorBoundary caught an error from Universe:', error, info);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="universe-message">
          3D scene failed — open <a href="#dashboard">#dashboard</a>
        </div>
      );
    }
    return this.props.children;
  }
}
