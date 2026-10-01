# M10 — 네트워크 연결 수집 설계

- 작성일: 2026-10-01
- 상태: 승인됨 (구현·확인됨 — 10절)
- 선행: Network Universe 확장 기획 M10~M17 (M10 이 첫 단계), 엔진·프런트엔드 `v0.3.0` (`main` fa6a7f1)
- 범위: 이 PC 의 TCP·UDP 연결을 수집해 프로세스에 매핑하고 원격 끝점으로 묶어, C++ CLI(`--connections`)에서 확인한다. 바이트·속도(M11), 스냅샷 계약과 WebSocket(M12), 화면(M13~)은 범위 밖이다.

## 1. 결정

| # | 결정 | 이유 |
|---|---|---|
| D100 | 연결 목록은 IP Helper `GetExtendedTcpTable`(`TCP_TABLE_OWNER_PID_ALL`)과 `GetExtendedUdpTable`(`UDP_TABLE_OWNER_PID`)을 IPv4·IPv6 로 호출해 얻는다. 관리자 권한은 필요 없다 | OS 가 소유 PID 까지 준다. 사용자가 고른 "관리자는 트래픽 수치(M11)에만" 과 맞는다 |
| D101 | UDP 는 이 단계에서 로컬 소켓(PID·로컬 IP·포트)만 모은다. 원격 끝점은 M11 의 ETW(Kernel-Network, 관리자 전용)에서 얻는다 | IP Helper 의 UDP 표에는 원격 주소가 없다 (UDP 는 연결이 아니라 소켓이다) |
| D102 | 원격 끝점은 원격 **IP** 로 묶는다. 한 IP 의 여러 포트는 한 노드의 포트 목록이 된다 | 기획의 "Remote Endpoint = 하나의 Network Node" 와 맞고, 노드 수가 폭증하지 않는다 |
| D103 | 집계에서 제외한다: 원격 주소가 미지정(`0.0.0.0`, `::`)인 항목(LISTEN·미연결), 루프백(`127.0.0.0/8`, `::1`, `::ffff:127.x`), 활성이 아닌 TCP 상태(`CLOSED`, `TIME_WAIT`, `DELETE_TCB`). 제외한 개수는 요약에 센다 | 외부 공간(External Space)에는 나가는 연결만 둔다. 이 엔진과 브라우저의 루프백 연결이 노드로 뜨지 않게 한다. TIME_WAIT 는 소유 PID 가 0 이라 프로세스에 매핑되지 않는다 |
| D104 | 도메인 이름·국가·ASN 은 수집하지 않는다. 끝점은 IP·포트·사설망 여부만 가진다 | 사용자 결정. 역방향 DNS 는 PC 가 DNS 질의를 보내는 외부 통신이라 "아무것도 밖으로 보내지 않는다" 와 충돌한다 |
| D105 | CLI 는 새 모드 `--connections` 로 확인한다. 서버·계약은 건드리지 않는다 | 기획 26절: "C++ CLI 에서 우선 확인". 계약 확장은 M12 |

## 2. 타입 (`platform/RawNetwork.h`, 새 파일)

```cpp
enum class NetProtocol { Tcp, Udp };

enum class TcpState {
    None,  // UDP
    Closed, Listen, SynSent, SynReceived, Established,
    FinWait1, FinWait2, CloseWait, Closing, LastAck, TimeWait, DeleteTcb,
};

struct RawConnection {
    uint32_t pid = 0;
    NetProtocol protocol = NetProtocol::Tcp;
    std::string local_ip;      // 사람이 읽는 문자열: "192.168.0.10", "fe80::1"
    uint16_t local_port = 0;
    std::string remote_ip;     // UDP 는 빈 문자열
    uint16_t remote_port = 0;  // UDP 는 0
    TcpState state = TcpState::None;
};

struct ConnectionScan {
    std::vector<RawConnection> connections;
    // 일부 표를 읽지 못했을 때의 이유 (예: "GetExtendedTcpTable(AF_INET6) failed with error 5").
    // 읽은 만큼은 connections 에 있다. 비어 있으면 모두 읽은 것이다.
    std::string error;
};

class IConnectionScanner {
public:
    virtual ~IConnectionScanner() = default;
    virtual ConnectionScan scan() = 0;
};
```

- 포트는 네트워크 바이트 순서로 오므로 `ntohs` 로 바꾼다. IP 문자열은 `InetNtopW` 로 만들고 UTF-8 로 바꾼다 (IPv6 의 scope id 는 붙이지 않는다).
- TCP 상태는 `MIB_TCP_STATE` 값을 `TcpState` 로 옮긴다. 모르는 값은 `Closed` 로 취급해 집계에서 빠지게 한다.

## 3. 스캐너 (`platform/windows/WindowsConnectionScanner.{h,cpp}`)

- `scan()` 은 네 표(TCP4, TCP6, UDP4, UDP6)를 읽는다. 각 표는 `GetExtended*Table` 을 크기를 먼저 묻고(`ERROR_INSUFFICIENT_BUFFER`) 버퍼를 만들어 다시 부르는데, 사이에 표가 커지면 다시 시도한다 (최대 5 회).
- 한 표가 끝내 실패하면 그 표만 건너뛰고 `error` 에 이유를 이어 붙인다. 네 표 모두 실패해도 예외를 던지지 않는다.
- 같은 호출에서 TCP 의 `LISTEN` 항목과 UDP 항목은 `RawConnection` 으로 그대로 돌려준다 (필터링은 집계기가 한다).
- 테스트에서는 `IConnectionScanner` 의 가짜를 쓴다.

## 4. 집계기 (`core/NetworkAggregator.{h,cpp}`, 순수)

```cpp
struct ConnectionView {
    uint32_t pid = 0;
    std::string process;   // 이름. 모르면 "pid <n>"
    std::string local_ip;
    uint16_t local_port = 0;
    std::string remote_ip;
    uint16_t remote_port = 0;
    TcpState state = TcpState::Established;
};

struct ProcessNetwork {
    uint32_t pid = 0;
    std::string process;
    std::vector<ConnectionView> connections;   // 정렬: remote_ip, remote_port, local_port
    uint32_t udp_sockets = 0;                  // 이 프로세스의 UDP 로컬 소켓 수
};

struct RemoteEndpoint {
    std::string ip;
    bool is_private = false;                   // 사설망·링크 로컬 (10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10)
    std::vector<uint16_t> ports;               // 오름차순, 중복 없음
    uint32_t connection_count = 0;
    std::vector<std::string> processes;        // 이름, 오름차순, 중복 없음
};

struct NetworkSummary {
    uint32_t connections = 0;       // 집계에 들어간 TCP 연결 수
    uint32_t established = 0;       // 그중 ESTABLISHED
    uint32_t endpoints = 0;
    uint32_t listening = 0;         // 제외: LISTEN·미지정 원격
    uint32_t loopback = 0;          // 제외: 루프백
    uint32_t inactive = 0;          // 제외: CLOSED·TIME_WAIT·DELETE_TCB
    uint32_t udp_sockets = 0;
};

struct NetworkView {
    std::vector<ProcessNetwork> processes;     // 정렬: 연결 수 내림차순, 이름, pid
    std::vector<RemoteEndpoint> endpoints;     // 정렬: connection_count 내림차순, ip
    NetworkSummary summary;
};

// names: pid -> 프로세스 이름. 없는 pid 는 "pid <n>" 으로 표시한다.
NetworkView aggregateNetwork(const std::vector<RawConnection>& connections,
                             const std::unordered_map<uint32_t, std::string>& names);
```

- 판정 순서(한 항목당 한 번만 센다): UDP → `udp_sockets` (소유 프로세스에 +1, 요약에도 +1); TCP 중 상태가 `LISTEN` 이거나 원격 IP 가 미지정 → `listening`; 원격이 루프백 → `loopback`; 상태가 `CLOSED`/`TIME_WAIT`/`DELETE_TCB` → `inactive`; 나머지 → 연결.
- 루프백·미지정·사설망 판정은 문자열이 아니라 주소를 파싱해서 한다 (`inet_pton` 계열). 파싱에 실패한 원격 IP 는 외부 주소로 취급한다 (제외하지 않는다).
- 같은 입력은 같은 출력이다 (모든 목록이 정렬되어 있다).
- 프로세스 이름은 `names` 에서 온다. PID 가 같은 프로세스가 둘 이상 있을 수 없다.

## 5. CLI (`--connections`)

- `Options`: `Mode::Connections`, 인자 `--connections`. `--interval-ms`, `--iterations` 를 받는다 (`--dump` 와 같은 의미, 기본 반복 없음 = 무한). `--serve`·`--json`·`--dump` 와 같이 쓸 수 없다 (한 모드만). `--max-groups` 는 무시한다 (받아들이되 쓰지 않는다 — 다른 모드와 인자 호환을 위해).
- `main`: `runConnections` — `WindowsConnectionScanner` 와 기존 `ISystemReader`(`WindowsSystemReader`, 이름은 `RawSample::processes`)로 매 간격 `aggregateNetwork` 를 만들어 `formatNetworkTable` 로 출력한다. 스캐너가 `error` 를 돌려주면 stderr 에 한 줄 남기고 읽은 만큼 출력한다. 종료 코드: 끝까지 돌면 0.
- `cli/NetworkTableFormatter.{h,cpp}`: `std::string formatNetworkTable(const NetworkView&)`. 형식:

```
Network  connections 24 (established 21) | endpoints 9 | udp sockets 31 | skipped: listening 58, loopback 12, inactive 40

chrome.exe (pid 18421)  12 connections, 4 udp sockets
  TCP  192.168.0.10:52141 -> 142.250.76.110:443    ESTABLISHED
  ...

Endpoints
  142.250.76.110      ports 443            12 connections  chrome.exe, msedge.exe
  192.168.0.1  (lan)  ports 53, 443         3 connections  svchost.exe
```

  프로세스는 연결이 있는 것만 보여 준다 (UDP 소켓만 있는 프로세스는 요약의 `udp sockets` 에만 센다). 한 프로세스가 연결을 많이 가지면 앞의 `MAX_CONNECTIONS_PER_PROCESS = 8` 개만 보여 주고 `... and N more` 로 줄인다. 끝점은 앞의 24 개만 보여 주고 `... and N more endpoints` 로 줄인다.

## 6. 엔진 구조

- `engine/CMakeLists.txt`: `pulse_core` 에 `src/core/NetworkAggregator.cpp`, `src/cli/NetworkTableFormatter.cpp`, `src/platform/windows/WindowsConnectionScanner.cpp` 를 더한다. 링크 라이브러리에 `iphlpapi`, 주소 변환에 `ws2_32` (이미 있다).
- 정적 링크 검사(`package.ps1`)는 `iphlpapi` 가 Windows 기본 DLL 이라 그대로 통과해야 한다.

## 7. 테스트

- `aggregateNetwork` (가짜 입력): 프로세스 이름 매핑·없는 pid 의 `pid <n>`, 각 제외 규칙(LISTEN, 미지정 `0.0.0.0`/`::`, 루프백 `127.0.0.1`/`::1`/`::ffff:127.0.0.1`, `TIME_WAIT`·`CLOSED`·`DELETE_TCB`)과 요약 카운트, 한 항목이 한 규칙으로만 세어지는지, 끝점 묶음(여러 포트·여러 프로세스·연결 수), 사설망 판정(경계 `172.15.x` 는 공인, `172.16.x`~`172.31.x` 는 사설), IPv6, UDP 소켓 카운트, 정렬 결정성(입력 순서를 섞어도 같은 출력), 파싱할 수 없는 원격 IP 는 외부로 취급.
- `formatNetworkTable`: 요약 줄, 프로세스 블록, 연결 8 개 초과 시 `... and N more`, 끝점 24 개 초과, 빈 입력(연결 없음) 형식.
- `Options`: `--connections` 파싱, `--interval-ms`·`--iterations` 허용, 다른 모드와 함께 쓰면 오류, 사용법에 `--connections` 가 있다.
- `WindowsConnectionScanner` (실제 OS): 테스트 프로세스가 `127.0.0.1` 에 TCP 리스너를 열고(임시 포트) 클라이언트를 연결한 뒤 `scan()` 을 부르면, (a) 리스너 포트에 대한 `LISTEN` 항목, (b) 클라이언트→리스너와 리스너→클라이언트 두 개의 `ESTABLISHED` 항목이 소유 PID = `GetCurrentProcessId()` 로 들어 있어야 한다. UDP 소켓을 하나 열고 로컬 포트로 찾아 UDP 항목도 확인한다. `scan().error` 는 비어 있어야 한다.
- 실제 외부 연결을 만드는 시험은 하지 않는다 (`--connections` 를 사람이 돌려 보는 것으로 확인).

## 8. 완료 조건

- `pulse-engine --connections --iterations 1` 이 이 PC 의 연결을 프로세스별로 표로 보여 주고 끝점 요약을 낸다. 일반 권한에서 동작한다.
- 위 테스트가 통과하고 엔진 빌드에 경고가 없다.
- 정적 링크 릴리스 빌드가 그대로 통과한다 (`iphlpapi` 는 기본 DLL).
- `--serve`·`--dump`·`--json` 의 동작과 출력은 바뀌지 않는다.

## 9. 범위 밖

바이트·속도·지연(M11), UDP 원격 끝점(M11), 스냅샷 계약·WebSocket·프로세스 그룹과의 연결(M12), 도메인·국가, 연결 생성·종료 이벤트 추적(M12 이후), 화면.

## 10. 구현 결과 (2026-10-01)

시제품이 곧 구현이라 계획서를 따로 쓰지 않고 `feature/m10-network` 에서 바로 만들었다.

| 항목 | 결과 |
|---|---|
| 엔진 테스트 | 249 케이스 중 246 통과, 3 건너뜀(ETW). 컴파일 경고 0. 새 시험: IP 파싱·분류, 집계기 12, 표 출력 9, 옵션 3, 실제 OS 스캐너 2 |
| 실제 OS 스캐너 | 테스트 프로세스가 연 127.0.0.1 TCP 리스너·클라이언트·UDP 소켓을 소유 PID 와 함께 찾는다 (LISTEN, 양쪽 ESTABLISHED, UDP 원격 없음) |
| `--connections --iterations 1` | 일반 권한(비관리자)에서 이 PC 의 연결 85~99 개, 끝점 58~70 개를 프로세스별로 출력 (브라우저·메신저·ChatGPT 등). 요약 줄: 제외된 LISTEN 60, 루프백 29~68, 비활성 26~31 |
| 정적 릴리스 빌드 | `package.ps1` 의 정적 링크 검사 통과 (`iphlpapi` 는 Windows 기본 DLL), 내장 웹 검사 통과, 릴리스 exe 도 `--connections` 동작 |
| 기존 모드 | `--serve`·`--dump`·`--json` 출력과 동작 변경 없음 (`--connections` 는 ETW 를 켜지 않는다) |

스펙 5절의 끝점 줄은 IP 열을 26 칸으로 정렬한다.
