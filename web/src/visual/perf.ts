// M9 스펙 5·6절. 자동 해상도와 성능 표시의 계산.

// 기기 dpr 이 더 커도 2 를 넘기지 않는다. 그 위는 픽셀 비용만 늘고 차이가 보이지 않는다.
export const MAX_DPR = 2;
export const DPR_STEP = 0.25;

// PerformanceMonitor 의 factor(0~1)를 dpr 로 바꾼다. 0 이면 1, 1 이면 기기 dpr(최대 2).
// 0.25 단위로 내려 자잘한 크기 변경(버퍼 재할당)을 줄인다.
export function dprFor(factor: number, deviceDpr: number): number {
  const maxDpr = Number.isFinite(deviceDpr) ? Math.min(MAX_DPR, Math.max(1, deviceDpr)) : 1;
  const f = Number.isFinite(factor) ? Math.min(1, Math.max(0, factor)) : 0;
  const raw = 1 + (maxDpr - 1) * f;
  return Math.max(1, Math.floor(raw / DPR_STEP + 1e-9) * DPR_STEP);
}

export interface FrameSummary {
  // 창 안의 평균 프레임 시간 (ms)
  avgMs: number;
  // 평균에서 구한 fps
  fps: number;
  // 창 안의 가장 긴 프레임 (ms)
  maxMs: number;
}

// 최근 프레임 시간을 모아 요약한다. 성능 표시가 초당 두 번 읽는다.
export class FrameStats {
  private totalSec = 0;
  private maxSec = 0;
  private count = 0;

  add(deltaSec: number): void {
    if (!Number.isFinite(deltaSec) || deltaSec < 0) {
      return;
    }
    this.totalSec += deltaSec;
    this.maxSec = Math.max(this.maxSec, deltaSec);
    this.count += 1;
  }

  // 모은 시간의 합(초). 표시 주기를 정하는 데 쓴다.
  get elapsedSec(): number {
    return this.totalSec;
  }

  // 요약을 돌려주고 창을 비운다. 모은 프레임이 없으면 null.
  take(): FrameSummary | null {
    if (this.count === 0 || this.totalSec <= 0) {
      this.reset();
      return null;
    }
    const avgSec = this.totalSec / this.count;
    const summary = { avgMs: avgSec * 1000, fps: 1 / avgSec, maxMs: this.maxSec * 1000 };
    this.reset();
    return summary;
  }

  reset(): void {
    this.totalSec = 0;
    this.maxSec = 0;
    this.count = 0;
  }
}

export interface RenderCounts {
  calls: number;
  triangles: number;
}

// 성능 표시의 두 줄. 프레임 요약이 없으면(아직 모으는 중) 첫 줄은 대시로 둔다.
export function formatPerf(summary: FrameSummary | null, dpr: number, counts: RenderCounts): string {
  const timing =
    summary === null
      ? '— ms · — fps'
      : `${summary.avgMs.toFixed(1)} ms · ${Math.round(summary.fps)} fps · max ${summary.maxMs.toFixed(1)} ms`;
  const triangles =
    counts.triangles >= 1000 ? `${Math.round(counts.triangles / 1000)}k` : `${counts.triangles}`;
  return `${timing}
dpr ${Number(dpr.toFixed(2))} · ${counts.calls} calls · ${triangles} tris`;
}
