# M11 — 네트워크 트래픽(바이트·속도) 수집 설계

- 작성일: 2026-10-01
- 상태: 승인됨 (IPv4·IPv6 모두 관리자 시험으로 확인됨 — 10절)
- 선행: M10 연결 수집 (`main` e99e0c7), ETW 스레드 매핑 (`EtwSchedulerCollector`)
- 범위: ETW `Microsoft-Windows-Kernel-Network` 로 연결(플로우)별 송수신 바이트를 수집해 업로드·다운로드 속도를 만들고, `--connections` 에서 확인한다. 관리자 권한이 없으면 속도를 "측정 불가"로 둔다. 스냅샷 계약·WebSocket(M12)과 지연 시간(latency)은 범위 밖이다.

## 1. 결정

| # | 결정 | 이유 |
|---|---|---|
| D110 | 전용 ETW 세션 `PulseUniverse-Net` (일반 실시간 세션, 시스템 로거 아님)에 공급자 `Microsoft-Windows-Kernel-Network` ({7DD42A49-5329-4832-8DFD-43D979153A88}) 를 키워드 IPv4(0x10)·IPv6(0x20) 로 켠다. 소비는 별도 스레드의 `ProcessTrace` | 스레드 매핑 세션은 커널 시스템 로거라 일반 매니페스트 공급자를 섞기 어렵다. 세션을 나누면 한쪽 실패가 다른 쪽에 번지지 않는다 |
| D111 | 이벤트는 TCP 송신·수신, UDP 송신·수신 (IPv4·IPv6) 네 종류만 쓴다. 페이로드에서 PID, 크기, 주소·포트 네 값을 읽어 **플로우 키** (프로토콜, PID, 정규화한 두 끝점)에 바이트를 누적한다 | 연결 목록(M10)과 같은 키로 맞출 수 있고, UDP 는 이 이벤트로만 원격 끝점을 알 수 있다 (D101) |
| D112 | 플로우 키의 두 끝점은 정렬해서 저장한다 (방향 무관). 연결 목록과 맞출 때는 어느 쪽이 로컬인지 따지지 않고 정렬한 쌍으로 비교한다 | 수신 이벤트에서 `saddr`/`daddr` 가 어느 쪽인지 문서마다 달라 시험으로 확인한다 (6절). 정렬 키는 그 불확실성을 흡수한다 |
| D113 | 수집기는 창 단위 `drain()` 으로 바이트를 내준다 (스레드 매핑과 같은 방식). 속도 = 창 동안 바이트 / 창 길이. 창 길이는 `drain()` 호출 사이의 단조 시계 간격이고, `drain()` 은 창을 닫기 전에 ETW 버퍼를 강제로 흘려보낸다 (ETW 는 버퍼가 차거나 1 초 타이머에만 이벤트를 전달해 그대로는 창마다 0~2 개 묶음이 들쭉날쭉하기 때문이다). 평활은 하지 않는다 (프런트엔드 몫) | 같은 패턴을 재사용한다. 하나의 값에 의미를 정해 둔다: 마지막 창의 평균 B/s |
| D114 | 실측 가능 여부는 `--mapping` 과 같은 규칙을 쓴다: `auto`(기본)는 관리자면 측정, 아니면 측정 없이 동작 / `estimated` 는 측정하지 않음 / `measured` 는 측정을 못 하면 종료 코드 1. `--connections` 가 이 옵션을 처음으로 사용한다 | 사용자가 정한 "ETW 실측, 관리자만". 스레드 매핑과 같은 사용 방식이라 설명이 하나로 끝난다 |
| D115 | 관리자가 아니거나 세션이 실패하면 표의 속도 칸은 `-` 로 두고 요약 줄에 `traffic: not measured (<이유>)` 를 한 줄 낸다. 연결 목록·끝점 묶음은 그대로 나온다 | 권한이 없어도 M10 의 모든 정보는 보인다 |

## 2. 타입 (`platform/RawNetwork.h` 에 추가)

```cpp
// 방향 없는 플로우 키. 두 끝점은 (ip, port) 사전순으로 정렬해 둔다.
struct FlowKey {
    NetProtocol protocol = NetProtocol::Tcp;
    uint32_t pid = 0;
    IpBytes ip_a; uint16_t port_a = 0;   // 16 바이트 주소 (IPv4 는 ::ffff:a.b.c.d)
    IpBytes ip_b; uint16_t port_b = 0;
    bool operator<(const FlowKey&) const;   // 완전 순서
    bool operator==(const FlowKey&) const;
};

struct RawFlowTraffic {
    FlowKey key;
    uint64_t bytes_sent = 0;       // 이 프로세스가 보낸 바이트 (창 동안)
    uint64_t bytes_received = 0;   // 이 프로세스가 받은 바이트 (창 동안)
};

struct RawNetworkTraffic {
    double window_seconds = 0.0;   // 이벤트 시각 기준 창 길이
    std::vector<RawFlowTraffic> flows;
};

class INetworkTrafficSource {
public:
    virtual ~INetworkTrafficSource() = default;
    // 마지막 drain 이후의 창을 닫아 돌려준다. 아직 창이 없으면 nullopt.
    virtual std::optional<RawNetworkTraffic> drain() = 0;
};
```

`core/FlowKey.{h,cpp}` 의 `makeFlowKey(protocol, pid, ip1, port1, ip2, port2)` 가 정렬을 맡는다 (OS 헤더를 쓰지 않는 순수 함수).

## 3. 수집기 (`platform/windows/EtwNetworkCollector.{h,cpp}`)

- `static std::unique_ptr<EtwNetworkCollector> start(std::string& error)`: 소유권 뮤텍스 `Global\PulseUniverse-Net-Owner` (이미 있으면 "another pulse-engine is already measuring network traffic" 로 실패), 남은 세션 정리(`stopSessionByName`), `StartTrace`(일반 실시간 세션, 이름 `PulseUniverse-Net`), `EnableTraceEx2`(공급자, 키워드 0x30, 레벨 4), `OpenTrace`(실시간), 소비 스레드. 권한 부족은 `ETW network events need administrator rights`, 남은 세션 때문에 못 멈추면 스레드 매핑과 같은 안내.
- 이벤트 콜백(`ProcessTrace` 스레드): 공급자 GUID 와 이벤트 ID 로 네 종류를 가려, 페이로드를 TDH 없이 고정 오프셋으로 읽는다 (5절의 배치). 알 수 없는 ID·짧은 페이로드는 버린다. 잠긴 구간은 맵 갱신 한 번이다.
- `drain()`: 먼저 `ControlTrace(EVENT_TRACE_CONTROL_FLUSH)` 로 버퍼를 흘려보내고 소비 스레드가 처리할 시간(250 ms)을 둔 뒤, 마지막 호출 이후의 누적을 `RawNetworkTraffic` 으로 돌려주고 비운다. 창 길이는 첫·마지막 이벤트 시각이 아니라 **drain 호출 사이의 단조 시계 간격**이다 (이벤트가 없는 창도 길이를 가진다 — 속도 0). 첫 호출은 창이 없으므로 nullopt. `--connections` 는 세션을 연 뒤 0.5 초(공급자가 켜지는 시간) 기다리고 첫 drain 을 부른다.
- 유실 경고: 스레드 매핑과 같이 `EventsLost`·`RealTimeBuffersLost` 가 늘면 10 초에 한 번 stderr 경고.
- 종료: 소멸자가 세션을 멈추고, `main` 의 콘솔 핸들러가 이름으로 멈춘다 (`stopSessionByName`).
- 버퍼: 64 KB × 16~64 개(최대 4 MB), 플러시 타이머 1 초. 시험에서 16 KB × 32 개는 1 KB 조각 5000 번 전송의 약 9 % 를 잃었다. 이벤트가 많아도 콜백은 맵 갱신뿐이다.

## 4. 집계기 확장 (`core/NetworkAggregator`)

- `aggregateNetwork(connections, names, traffic)` — 세 번째 인자 `const std::optional<RawNetworkTraffic>&` (없으면 M10 과 같다).
- 연결(`ConnectionView`)에 `std::optional<double> down_bps, up_bps` 를 더한다. 값은 TCP 연결의 (pid, 정렬한 로컬·원격 끝점) 키로 찾은 플로우의 `bytes_received / window`, `bytes_sent / window`. 창이 있는데 플로우가 없으면 0, 측정을 못 하면 nullopt.
- UDP: 플로우가 있는 UDP 소켓은 `ConnectionView`(프로토콜 UDP, 상태 `-`) 로 연결에 더한다 (원격 끝점이 처음으로 보인다). 같은 PID 의 UDP 소켓 개수(`udp_sockets`)는 그대로 센다. 플로우 없는 UDP 소켓은 지금처럼 개수만 센다. 루프백(요약의 `loopback` 에 센다)·미지정 제외는 TCP 와 같은 규칙. 플로우의 두 끝점 중 로컬은 그 PID 의 UDP 소켓에 (바인드 주소, 포트)가 맞는 쪽이다 (wildcard 소켓은 모든 주소에 맞는다). 양쪽이 다 맞으면(NTP 123 ↔ 123, mDNS 5353 ↔ 5353) 이 PC 의 로컬 주소(연결 목록의 로컬 주소들)인 쪽이 로컬이고, 그래도 가릴 수 없으면 추측하지 않고 그 플로우를 건너뛴다.
- 끝점(`RemoteEndpoint`)과 프로세스(`ProcessNetwork`)에 합산 `down_bps`, `up_bps`. 연결 목록에 없는 TCP 플로우(세션 시작 전에 열린 연결의 늦은 이벤트, 이미 닫힌 연결)는 무시한다.
- `NetworkSummary` 에 `bool traffic_measured` 와 `double down_bps, up_bps` (측정했을 때의 전체 합)를 더한다.
- 합산 규칙: 속도 합은 연결 하나씩 더한다. 같은 플로우를 두 번 세지 않는다 (키가 유일).

## 5. 이벤트 배치 (시험으로 확인할 가정)

공급자 매니페스트의 이벤트 ID: TCP 송신 IPv4 `10`, 수신 IPv4 `11`, 송신 IPv6 `26`, 수신 IPv6 `27`; UDP 송신 IPv4 `42`, 수신 IPv4 `43`, 송신 IPv6 `58`, 수신 IPv6 `59`. 페이로드 (리틀 엔디언):

| 이벤트 | 배치 |
|---|---|
| TCP IPv4 (10, 11) | `PID u32`, `size u32`, `daddr u32`, `saddr u32`, `dport u16`, `sport u16`, … |
| TCP IPv6 (26, 27) | `PID u32`, `size u32`, `daddr 16B`, `saddr 16B`, `dport u16`, `sport u16`, … |
| UDP IPv4 (42, 43) | `PID u32`, `size u32`, `daddr u32`, `saddr u32`, `dport u16`, `sport u16`, … |
| UDP IPv6 (58, 59) | `PID u32`, `size u32`, `daddr 16B`, `saddr 16B`, `dport u16`, `sport u16`, … |

- IPv4 주소는 네트워크 바이트 순서의 `u32`, 포트는 네트워크 바이트 순서의 `u16` 로 본다.
- 이 배치와 ID 는 문서와 기억에 근거한 가정이다. **관리자 권한 시험**(6절)에서 알려진 크기의 루프백 전송으로 확인하고, 다르면 이 절과 코드를 고친다.

## 6. 테스트

- **순수 로직 (일반 권한)**: `makeFlowKey` 정렬 (끝점 순서를 바꿔도 같은 키, 같은 IP 다른 포트, IPv6, 프로토콜·PID 구분), 이벤트 파서 (`parseNetworkEvent(eventId, payload) -> optional<ParsedNetworkEvent>` 를 순수 함수로 분리: 네 종류의 정상 페이로드, 짧은 페이로드 거부, 모르는 ID 무시, 포트 바이트 순서, IPv4·IPv6 주소 문자열), 집계기 확장 (속도 계산, 플로우 없는 연결은 0, 측정 불가는 nullopt, UDP 플로우가 연결과 끝점에 나타남, 루프백 플로우 제외, 연결 목록에 없는 플로우 무시, 프로세스·끝점 합산, 같은 입력은 같은 출력).
- **표 출력**: 속도 칸 형식 (`1.2 MB/s`, `0 B/s`, `-`), 요약 줄의 `traffic:` 부분, 측정 불가 시 한 줄.
- **옵션**: `--connections --mapping estimated|measured|auto` 파싱(이미 됨), 사용법 문구.
- **관리자 권한 시험 (일반 권한에서는 건너뜀)**: 수집기를 시작하고, 이 프로세스 안에서 127.0.0.1 TCP 로 정확히 `N = 5 MB` 를 보내고 받은 뒤 `drain()` 한 합계에서 이 PID 의 플로우를 찾아 `bytes_sent ≥ N` 과 `bytes_received ≥ N` 이 모두 성립함을 확인한다 (프로토콜·PID·포트 쌍이 일치). UDP 로 100 개의 1 KB 데이터그램을 루프백으로 보낸 뒤 UDP 플로우의 합계도 확인한다. 이 시험이 5 절의 배치를 증명한다.
- 실제 PC 확인: 관리자 권한으로 `pulse-engine --connections --mapping measured --iterations 3` 를 돌려 브라우저·다운로드 중의 속도가 보이는지 사람이 본다.

## 7. 실패 동작

| 상황 | 동작 |
|---|---|
| 관리자가 아님 (`auto`) | 속도 `-`, 요약 줄에 `traffic: not measured (ETW network events need administrator rights)`. 나머지는 M10 과 같다 |
| `--mapping measured` 인데 측정 불가 | stderr 에 이유, 종료 코드 1 |
| 다른 엔진이 세션을 쓰는 중 | `auto` 는 측정 없이, `measured` 는 종료 코드 1 |
| 세션이 도중에 멈춤 | stderr 한 줄, 이후 `drain()` 은 nullopt (속도 `-`) |
| 이벤트 유실 | 10 초에 한 번 경고, 값은 하한이 된다 |
| 연결 수·이벤트가 매우 많음 | 맵 갱신만 하므로 콜백이 가볍다. 플로우 맵은 매 `drain()` 마다 비운다 |

## 8. 완료 조건

- 관리자 권한 시험이 5 MB 루프백 전송과 UDP 100 개로 통과한다 (사람이 UAC 로 승인해 실행).
- 관리자 권한으로 `--connections --mapping measured` 가 실제 속도를 보여 주고, 일반 권한에서는 같은 명령이 `auto` 에서 `-` 와 안내 한 줄을 낸다.
- 일반 권한 테스트 전부 통과, 경고 0, 정적 링크 릴리스 빌드 통과.
- 기존 모드·ETW 스레드 매핑 동작은 바뀌지 않는다.

## 9. 범위 밖

스냅샷 계약·`capabilities.network_traffic`·WebSocket (M12), 지연 시간(RTT), 연결 생성·종료 이벤트, TCP ESTATS, 도메인·국가, 화면.

## 10. 구현·시험 결과 (2026-10-01)

시제품이 곧 구현이라 계획서를 따로 쓰지 않고 `feature/m11-traffic` 에서 바로 만들었다.

| 항목 | 결과 |
|---|---|
| 일반 권한 | 엔진 테스트 280 케이스 중 273 통과, 7 건너뜀(ETW 관리자 전용), 경고 0. `--connections` 는 `traffic: not measured (ETW network events need administrator rights)` 를 낸다 |
| 관리자 시험 (IPv4) | UAC 로 실행: 루프백 TCP 5 MB 를 1 KB·64 KB 조각으로 보내 송수신 모두 5 MB 이상, UDP 100 개(1 KB), 세션 정리 모두 통과. 이벤트 ID 10/11/42/43 과 PID·크기·주소·포트 오프셋(5절)이 맞음을 증명한다 |
| 시험이 알려 준 것 | 16 KB × 32 버퍼는 1 KB 조각 5000 번 전송의 약 9 % 를 잃었다 → 64 KB × 64 로 늘림. 공급자를 켠 직후 잠시는 이벤트가 오지 않았다 → 0.5 초 대기. 첫 시험의 TCP 64 KB 조각은 첫 실행에서만 부족했고(7 개 분량) 기다림을 두고 다시 통과했다 |
| 리뷰 반영 | 창을 닫기 전 ETW 버퍼 강제 플러시(속도의 들쭉날쭉 제거), UDP 로컬 쪽을 (바인드 주소, 포트)와 로컬 주소 집합으로 판별(NTP·mDNS 같은 같은 포트 통신), UDP 루프백 집계, 죽은 코드 제거, 핸들러 해제 |
| 관리자 시험 (IPv6) | UAC 로 실행: `::1` 루프백 TCP 1 MB 와 UDP 50 개의 송수신이 모두 세어졌다. IPv6 이벤트(26/27/58/59)의 배치(5절)가 맞다 |
| 전달 지연 | 진단 시험: UDP 100 개를 보내고 50 ms 간격으로 drain 하면 0 → 47 KB(0.32 초) → 100 KB(0.49 초)로 모인다. 강제 플러시를 해도 이벤트는 소비자에게 0.3~0.5 초 늦게 닿는다. 그래서 값은 일정한 지연을 두고 같은 창으로 보고되고(정상 상태의 속도는 정확하다), 관리자 시험은 기대한 바이트가 모일 때까지(최대 5 초) 여러 번 drain 한다 |
| 확인 대기 | 실제 NIC 트래픽(`--connections --mapping measured`)은 사람이 한 번 본다 (루프백만 시험했다) |
