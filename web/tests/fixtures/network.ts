import type { Network } from '../../src/protocol/schema';

// 스냅샷 리터럴을 만드는 시험이 쓰는 빈 네트워크 블록 (수집기 없음).
export const emptyNetwork: Network = {
  traffic: 'unavailable',
  summary: { connections: 0, established: 0, endpoints: 0, udp_sockets: 0, down_bps: null, up_bps: null },
  endpoints: [],
  connections: [],
};
