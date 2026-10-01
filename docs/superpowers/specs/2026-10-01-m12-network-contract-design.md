# M12 — 네트워크 정보를 스냅샷 계약에 싣기 설계

- 작성일: 2026-10-01
- 상태: 승인됨 (구현·확인됨 — 10절)
- 선행: M10 연결 수집, M11 트래픽 수집 (`main` 0735a1f), 계약 설계 `2026-09-22-pulse-universe-contract-design.md`
- 범위: 엔진이 M10·M11 의 결과를 WebSocket 스냅샷의 `network` 블록과 hello 의 `capabilities.network_traffic` 으로 보내고, 프런트엔드가 스키마로 검증해 스토어에 싣는다. 화면(M13~)·지연 시간·도메인 이름은 범위 밖이다.

## 1. 결정

| # | 결정 | 이유 |
|---|---|---|
| D120 | 추가만 하는 변경이라 `v` 는 1 그대로다. 프런트엔드 스키마는 `network` 를 필수로 둔다 | 엔진과 웹은 한 릴리스로 함께 나간다 (웹이 exe 에 내장). 기존 필드는 건드리지 않는다 |
| D121 | 연결은 프로세스 그룹 키(`name:root_pid`)로 이어진다. 엔진이 스냅샷의 그룹(루트 + 자식 PID)으로 연결의 PID 를 그룹에 매핑하고, 그룹에 속하지 않는 PID(상위 N 밖, 제외된 서비스)의 연결은 `connections`·`endpoints` 에서 뺀다 | 프런트엔드 천체가 그룹이다. 연결이 없는 고아 끝점 노드가 생기지 않게 한다. 전체 개수는 `summary` 에 남는다 |
| D122 | 끝점은 최대 64 개(연결 수 내림차순, 같으면 IP 오름차순), 연결은 그 끝점에 속한 것 중 속도(down+up)가 큰 순으로 최대 256 개 | 스냅샷 크기 상한 (약 +40 KB). 잘린 총수는 `summary.endpoints` 가 알려 준다 |
| D123 | 속도는 바이트/초의 정수이고 측정하지 못했으면 `null` 이다 (0 과 구분) | 계약서 6.1 절의 null 규칙. M11 의 nullopt 와 같다 |
| D124 | `network.traffic` 은 `"measured"`(트래픽 수집기가 있음) 또는 `"unavailable"`. hello 의 `capabilities.network_traffic` 도 같은 값으로 연결 시점의 능력을 나른다 | 스레드 매핑의 `capabilities.thread_mapping` 과 같은 방식. 수집기가 있어도 첫 스냅샷은 창이 없어 속도가 `null` 이다 |
| D125 | `--mapping` 이 `--serve` 에서 스레드 매핑 ETW 와 네트워크 트래픽 ETW 를 함께 정한다. `auto` 는 둘 다 시도하되 못 열어도 동작, `estimated` 는 둘 다 안 켬, `measured` 는 하나라도 못 열면 종료 코드 1 | M11 의 `--connections` 와 같은 규칙. 사용 설명이 하나로 끝난다 |
| D126 | 연결 목록 수집(스캐너)은 `--serve` 에서 항상 켠다 (관리자 권한 불필요). 권한이 없으면 속도만 `null` | M10 은 권한 없이 동작한다 |

## 2. 계약 (`network` 블록)

```json
"network": {
  "traffic": "measured",
  "summary": {
    "connections": 79, "established": 72, "endpoints": 50, "udp_sockets": 46,
    "down_bps": 120000, "up_bps": 8000
  },
  "endpoints": [
    { "ip": "142.250.76.110", "private": false, "ports": [443], "connections": 3,
      "groups": ["chrome.exe:18421", "msedge.exe:900"], "down_bps": 12400000, "up_bps": 800000 }
  ],
  "connections": [
    { "group": "chrome.exe:18421", "pid": 18421, "proto": "tcp", "local_port": 52141,
      "remote": "142.250.76.110", "remote_port": 443, "state": "established",
      "down_bps": 12000, "up_bps": 400 }
  ]
}
```

- `summary.connections`·`established`·`udp_sockets`·`down_bps`·`up_bps` 는 **모든** 프로세스 기준이다 (그룹에 속하지 않는 것 포함). `summary.endpoints` 도 모든 프로세스 기준 총수다. 속도 합은 측정했을 때만 숫자, 아니면 `null`.
- `endpoints[]`: `ip`(IPv4 점 표기 또는 IPv6 RFC 5952), `private`(사설망·링크 로컬), `ports`(오름차순 중복 없음), `connections`(포함된 연결 수), `groups`(이 끝점에 연결된 그룹 키, 오름차순 중복 없음), `down_bps`·`up_bps`(포함된 연결의 합, 측정 못 하면 `null`). 포함된(그룹에 속한) 연결만으로 다시 계산한다.
- `connections[]`: `group`, `pid`, `proto`(`"tcp"`/`"udp"`), `local_port`, `remote`(IP), `remote_port`, `state`(소문자: `established`, `syn_sent`, `syn_received`, `fin_wait1`, `fin_wait2`, `close_wait`, `closing`, `last_ack`; UDP 는 `"none"`), `down_bps`, `up_bps`. 정렬: `group`, `remote`, `remote_port`, `local_port`, `proto` 오름차순 (결정적).
- 로컬 IP 는 보내지 않는다 (화면에 필요 없다).
- `network.traffic == "unavailable"` 이면 모든 `*_bps` 는 `null`.
- 스캐너가 없는 엔진(테스트)은 `traffic: "unavailable"`, 빈 `endpoints`·`connections`, 0 인 `summary` (속도 `null`) 를 보낸다.

hello: `"capabilities": { "thread_mapping": "...", "network_traffic": "measured" | "unavailable" }`.

## 3. 엔진

- `core/Snapshot.h`: `NetworkEndpointOut`, `NetworkConnectionOut`, `NetworkSummaryOut`, `NetworkSnapshot` 구조체와 `SystemSnapshot::network` (이름: `NetworkSnapshot network;`). 기본값은 "unavailable·빈 목록".
- `core/NetworkSnapshot.{h,cpp}` (순수): `NetworkSnapshot buildNetworkSnapshot(const NetworkView&, const std::vector<ProcessGroup>& groups, bool traffic_capable, NetworkLimits limits = {})`. 알고리즘: pid → 그룹 키 맵(루트·자식) → 포함된 연결만 `NetworkConnectionOut` 으로 → 끝점별 재집계 → 끝점 정렬·상한 → 연결 상한(속도순으로 자른 뒤 결정적 순서로 다시 정렬) → 요약.
- `app/EngineLoop`: 선택 의존성 `NetworkSources { IConnectionScanner* scanner = nullptr; INetworkTrafficSource* traffic = nullptr; }` 를 생성자 인자로 받는다 (기본 없음). 한 반복의 순서: `traffic->drain()`(창을 닫음) → `reader.read()` → 집계 → `scanner->scan()` → `aggregateNetwork` → `buildNetworkSnapshot` → `snapshot.network`. 스캐너 오류 문자열은 stderr 에 한 줄(전과 같이 중복 억제는 하지 않는다 — 오류가 있을 때만 나온다).
- `app/ServeApp`: `ServeConfig::network` (`NetworkSources`) 를 `EngineLoop` 와 hello(`network_traffic`)에 쓴다.
- `network/Serializer`: `HelloInfo::network_traffic`, 스냅샷의 `network` 직렬화. 속도는 `llround` 한 정수 또는 `null`. 필드 이름·순서는 위 JSON.
- `main`: `--serve` 에서 스캐너를 만들고, `--mapping` 규칙(D125)으로 `EtwNetworkCollector` 를 시작한다 (실패 시 `auto` 는 stderr 에 이유 한 줄 `network traffic: not measured - <이유>`, `measured` 는 종료 코드 1). 시작 성공 시 `onNetworkConsoleControl` 핸들러를 등록하고 0.5 초 기다린 뒤 서버를 시작한다. 서버가 끝나면 핸들러를 뺀다.

## 4. 프런트엔드 (`web/src/protocol/schema.ts`)

- `NetworkEndpointSchema`, `NetworkConnectionSchema`, `NetworkSummarySchema`, `NetworkSchema` (`traffic: z.enum(['measured','unavailable'])`), `SnapshotSchema.network`, `HelloSchema.capabilities.network_traffic`. 타입 `Network`, `NetworkEndpoint`, `NetworkConnection`.
- 픽스처 `tests/fixtures/snapshot.json` 에 `network` 블록(끝점 몇 개·연결 몇 개, 측정됨)을 더한다. `tests/fixtures/network.ts` 에 빈 네트워크 값(`emptyNetwork`)을 두고 스냅샷 리터럴을 만드는 기존 테스트가 쓰게 한다.
- 스토어·보간기는 스냅샷 객체를 그대로 다루므로 코드 변경이 없어야 한다 (타입이 따라온다). 화면은 아직 쓰지 않는다.

## 5. 계약서 갱신

`2026-09-22-pulse-universe-contract-design.md` 의 4.3 절 hello 에 `network_traffic`, 4.4 절 snapshot 에 `network` 블록을 더하고, 4.5 절 필드 계약에 위 규칙(그룹 키로 연결, 상한, null)을 적는다. 상태 줄 옆에 "M12 에서 추가 (v1 유지)" 를 단다.

## 6. 테스트

- `buildNetworkSnapshot`: 그룹 매핑(루트·자식), 그룹에 없는 PID 제외, 끝점 재집계(포함된 연결만), 끝점 상한·정렬·`summary.endpoints` 총수, 연결 상한(속도순으로 남김)과 결정적 순서, 측정 안 됨 → 모든 속도 null, UDP 연결의 proto·state, 빈 입력, 입력 순서를 섞어도 같은 결과.
- `serializeSnapshot`/`serializeHello`: `network` 필드 이름·타입, 속도 정수/null, 빈 블록, hello 의 `network_traffic`. 기존 직렬화 시험은 그대로 통과.
- `EngineLoop`: 가짜 스캐너·트래픽 원으로 스냅샷의 `network` 가 채워진다 (그룹에 속한 PID 의 연결이 보이고 속도가 계산됨), 의존성이 없으면 "unavailable·빈 목록", 순서(drain → read)와 스캐너 오류가 루프를 죽이지 않음.
- `ServeApp`: 기존 통합 시험에서 hello 에 `network_traffic` 이 `unavailable`(수집기 없음)로 오고 스냅샷에 `network` 가 있다.
- 프런트엔드: 스키마가 새 필드를 받아들이고(`schema.test.ts`: 픽스처 파싱), 잘못된 `traffic` 값·누락된 `network` 를 거부, hello 의 새 필드, 기존 테스트 전부 통과(타입체크 포함).
- 실제 실행: 일반 권한으로 `--serve` 를 띄워 WebSocket 으로 받은 스냅샷에 `network.endpoints` 가 있고 속도가 `null` 이며 hello 가 `unavailable` 임을 본다. 관리자 권한으로 같은 것을 보면 속도가 숫자임을 사람이 한 번 본다 (UAC).

## 7. 실패 동작

| 상황 | 동작 |
|---|---|
| 스캐너 일부 표 읽기 실패 | stderr 에 이유, 읽은 만큼으로 `network` 를 만든다 |
| 트래픽 수집기가 도중에 멈춤 | `drain()` 이 nullopt → 속도 `null`, `traffic` 은 연결 시점 능력 그대로 |
| 관리자가 아님 (`auto`) | `traffic: "unavailable"`, 속도 `null`, 나머지 정상 |
| `--mapping measured` 인데 한쪽 ETW 실패 | 종료 코드 1 |
| 그룹이 하나도 없음 | 빈 `endpoints`·`connections`, `summary` 는 전체 기준 |

## 8. 완료 조건

- 엔진과 웹 테스트가 통과하고(경고 0, 타입체크·린트 통과) 정적 링크 릴리스 빌드가 통과한다.
- 일반 권한 `--serve` 의 실제 스냅샷에 `network` 가 있고 hello 에 `network_traffic: "unavailable"` 이 있다.
- 계약서가 새 필드를 설명한다.
- 기존 모드와 기존 필드는 바뀌지 않는다.

## 9. 범위 밖

화면(M13~M16), 최적화·GPU 입자(M17), 지연 시간, 도메인·국가, 연결 생성·종료 이벤트 추적, 로컬 IP 전송.

## 10. 구현 결과 (2026-10-01)

시제품이 곧 구현이라 계획서를 따로 쓰지 않고 `feature/m12-contract` 에서 바로 만들었다.

| 항목 | 결과 |
|---|---|
| 엔진 테스트 | 296 케이스 중 289 통과, 7 건너뜀(ETW 관리자 전용), 경고 0. 새 시험: 스냅샷 빌더 9, 직렬화 3, 엔진 루프 3, 서버 통합 1 |
| 웹 테스트 | 31 파일 320 개 통과, 타입체크 통과, 린트 경고는 `main.tsx` 의 기존 하나뿐 |
| 실제 `--serve` (일반 권한) | WebSocket 으로 받은 hello 의 `network_traffic` 이 `unavailable`, 스냅샷의 `network` 가 49 개 끝점·77 개 연결(전체 54 끝점·87 연결 중 그룹에 속한 것)을 담고 속도는 null, 스냅샷 한 장 약 49 KB |
| 계약서 | 4.3 절 hello, 4.4 절 snapshot 예, 4.5 절 필드 표에 `network` 를 추가 (v 1 유지) |
| 확인 대기 | 관리자 권한 `--serve` 에서 속도가 숫자로 오는지(UAC), 실제 NIC 트래픽 |
