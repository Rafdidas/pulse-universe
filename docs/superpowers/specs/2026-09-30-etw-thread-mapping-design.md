# ETW 실측 스레드-코어 매핑 설계

- 작성일: 2026-09-30
- 상태: 승인 대기
- 선행: 계약서 `2026-09-22-pulse-universe-contract-design.md` (D1, D2, 3.1, 4.3, 4.4, 6.2절), M7 스펙(flow 선명도 D39), M1~M9 구현 (`main`)
- 범위: 관리자 권한으로 실행될 때 ETW 문맥 전환(CSwitch) 이벤트로 그룹별·코어별 실행 시간을 재어 `flows[]` 를 `source: "measured"` 로 채운다. 권한이 없으면 지금의 추정을 그대로 쓴다. 프론트엔드는 바꾸지 않는다.

## 1. 이 문서의 위치

계약서 D2 는 스레드-코어 매핑을 1차에 추정으로 두고 `source` 필드로 실측 경로를 예약했다. 8절은 ETW 실측을 M9 이후의 독립 확장으로 남겼다. 6.2절의 정정(M1 구현 후)은 교체에 `RawSample` 의 새 필드, `FlowEstimator` 를 대신할 생산자, `DataAggregator` 의 분기가 필요하다고 적었다. 이 문서가 그 셋을 정한다.

## 2. 측정

관리자 PowerShell 에서 `logman` 으로 커널 추적을 5초씩 떴다 (버퍼 1024 KB × 64~256). 이벤트 내용은 `OpenTraceW`/`ProcessTrace` 로 ETL 파일을 읽는 C++ 도구로 직접 파싱했다 (`tracerpt` CSV 는 CSwitch 페이로드를 풀지 않는다).

환경: Windows 11, 28 논리 코어, 사용자 계정은 관리자 그룹(분할 토큰, 평소 not elevated).

| 추적 | 이벤트 | 초당 | 유실 |
|---|---|---|---|
| CSwitch, 대기 | 661,499 / 5.84 s | 약 113,000 | 0 |
| CSwitch, 부하 | 800,272 / 5.44 s | 약 147,000 | 0 |
| Profile 샘플, 대기 | 42,556 / 5 s | 약 8,500 | 0 |

- Profile 샘플은 쉬는 코어에서 거의 나오지 않는다(28코어 × 1 kHz = 28,000 의 1/3). 통계치다.
- CSwitch 80만 건(5.4초 분량)을 파싱하고 (pid, 코어)별로 쌓는 데 155 ms 가 걸렸다 (파일 읽기 포함). 실시간으로 받으면 코어 하나의 약 3% 다.
- 실측 매핑 (부하 추적, 코어 하나 기준 점유):

```
java.exe            114.5%   c24 21%  c25 20%  c22 13%  c20 13%  c21 11%
ExoAM_x64.exe       107.5%   c24 12%  c21 12%  c20 12%  c16 11%  c25  9%
msedgewebview2.exe   63.5%   c10  9%  c14  7%  c12  6%  c8   6%  c6   4%
Code.exe             53.3%   c20  7%  c21  5%  c24  5%  c6   4%  c25  4%
whale.exe            27.1%   c8   8%  c10  5%  c2   3%  c12  2%  c6   2%
```

프로세스마다 다른 코어 무리에서 돈다. 지금의 추정기는 모든 바쁜 그룹을 같은 뜨거운 코어로 보낸다 (M7 스펙 2절). 실측은 화면을 실제로 바꾼다.

## 3. 확정된 결정

| # | 결정 | 이유 |
|---|---|---|
| D54 | CSwitch 이벤트로 (스레드, 코어)별 실행 시간을 정확히 센다. Profile 샘플링은 쓰지 않는다 | 2절. 비용이 감당할 만하고 `measured` 라는 이름에 맞는 값이다 |
| D55 | 전역 "NT Kernel Logger" 대신 이름 붙은 전용 시스템 로거 세션 `PulseUniverse-Sched` 를 쓴다 | 다른 추적 도구(xperf, Process Monitor 등)와 부딪히지 않는다 |
| D56 | `--mapping auto\|estimated\|measured`, 기본 `auto`. `auto` 는 실패하면 추정으로, `measured` 는 실패하면 종료 | 관리자 권한 없이 띄워도 지금처럼 동작한다 |
| D57 | `weight` = 그룹 구성원이 코어 c 에서 돈 시간 ÷ 창 길이 (0~1). 5% 미만 버림, 그룹당 상위 4개 | "이 그룹이 코어 c 를 몇 % 썼나". 추정과 같은 걸러내기 |
| D58 | 수집기는 플랫폼 계층(`platform/windows`)에, 누적과 흐름 변환은 순수 코어 계층(`core/`)에 둔다 | 코어 로직을 권한 없이 단위 테스트한다 |

### 3.1 되돌리기 쉬운 것과 어려운 것

걸러내기 값, 세션 버퍼 크기, 플러시 주기는 상수다. 되돌리기 어려운 것은 `RawSample` 에 측정값 자리를 여는 것(4절)이다 — 이후 다른 측정원(예: Linux perf)도 같은 모양을 채워야 한다.

## 4. 데이터 모델 (`platform/RawTypes.h`)

```cpp
// 한 창 동안 한 프로세스가 한 코어에서 돈 시간.
struct RawRunTime {
    uint32_t pid = 0;
    uint32_t core = 0;
    double seconds = 0.0;
};

struct RawThreadMapping {
    double window_seconds = 0.0;      // 이 창의 길이 (이벤트 시각 기준)
    std::vector<RawRunTime> run_times;
};

struct RawSample {
    ...
    // 실측 수집기가 살아 있으면 채운다. 없으면 추정한다 (계약서 6.2절).
    std::optional<RawThreadMapping> thread_mapping;
};

struct HostInfo {
    ...
    // hello 의 capabilities.thread_mapping. 연결 시점의 수집기 상태.
    std::string thread_mapping = "estimated";
};
```

## 5. 실행 시간 누적 (`core/RunTimeTable`, 순수)

플랫폼 계층의 수집기가 이벤트를 풀어 이 클래스의 메서드를 부른다. 스레드 안전성은 수집기가 책임진다(뮤텍스). 시각 단위는 틱(정수)이고 틱/초는 생성자에서 받는다.

- `threadStarted(pid, tid)` — Thread Start / DCStart(런다운). tid → pid.
- `threadEnded(tid)` — Thread End. 표에서 지운다 (그 스레드가 아직 코어에 올라가 있으면 다음 전환에서 정산된 뒤 사라진다).
- `contextSwitch(core, newTid, ts)` — 코어 `core` 의 현재 조각(이전 스레드, 시작 시각)을 `ts` 까지 정산하고 새 조각을 연다. 이전 스레드의 pid 를 모르면 `unknownTicks` 로만 센다. pid 0(Idle)은 쌓지 않는다.
- `drain() -> RawThreadMapping` — 창을 닫는다. 기준 시각 `now` 는 지금까지 본 이벤트 시각의 최댓값이다. 코어마다 진행 중인 조각을 `now` 까지 정산하고 조각 시작을 `now` 로 옮긴다 (문맥 전환 없이 코어를 독점한 스레드도 센다). 창 길이 = `now − 직전 drain 의 now`. 첫 drain 은 첫 이벤트 시각부터다. 이벤트가 하나도 없었으면 창 길이 0, 빈 목록.
- 이벤트가 `now` 보다 이른 시각으로 늦게 도착하면(버퍼 병합 차이) 정산 길이는 0 으로 자른다.

## 6. 흐름 변환 (`core/MeasuredFlows`, 순수)

`measuredFlows(groups, mapping, cfg) -> vector<Flow>`

- 그룹 구성원 pid (`root_pid` 와 `children[].pid`) 의 실행 시간을 코어별로 더한다.
- `weight = 합 / window_seconds`, 1 에서 자른다. `window_seconds <= 0` 이면 빈 목록.
- `FlowConfig` 의 `min_weight` 미만은 버리고 그룹당 `max_flows_per_group` 개를 weight 내림차순으로 남긴다 (FlowEstimator 와 같은 규칙).
- `source = "measured"`.
- 화면에 남은 그룹(`snapshot.groups`)에 대해서만 만든다.

`DataAggregator` 7단계: `sample.thread_mapping` 이 있으면 `measuredFlows`, 없으면 `flow_estimator_.estimate`.

## 7. ETW 수집기 (`platform/windows/EtwSchedulerCollector`)

- 세션: `StartTraceW` 로 `PulseUniverse-Sched` 를 연다. `LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE`, 버퍼 1024 KB, 최소 64 · 최대 256 개, `FlushTimer = 1` 초, `ClockType = 1` (QPC). 시작 전에 같은 이름의 세션을 `ControlTraceW(STOP)` 로 멈춘다 (이전 실행이 죽으며 남긴 것).
- 사용 이벤트: `TraceSetInformation(TraceSystemTraceEnableFlagsInfo)` 로 `EVENT_TRACE_FLAG_PROCESS | THREAD | CSWITCH` 를 켠다.
- 소비: 별도 스레드에서 `OpenTraceW`(실시간) + `ProcessTrace`. 콜백은 Thread 공급자(`{3d6fa8d1-…}`) opcode 1·3(Start·DCStart) → `threadStarted`, 2(End) → `threadEnded`, 36(CSwitch) → `contextSwitch(BufferContext.ProcessorIndex, NewThreadId, TimeStamp)`.
- `ProcessTrace` 는 세션 시작 뒤의 스레드만 Start 로 알린다. 이미 있던 스레드는 세션을 켤 때 커널이 보내는 DCStart 런다운으로 채운다 (2절 측정에서 DCStart 8,400여 건 확인).
- 종료: 소멸자에서 `ControlTraceW(STOP)` → 소비 스레드 join → `CloseTrace`.
- `WindowsSystemReader::read()` 가 수집기가 있으면 `drain()` 결과를 `RawSample::thread_mapping` 에 싣는다.
- 틱/초: 로그파일 헤더의 `PerfFreq`.

## 8. 옵션과 권한 (D56)

- `--mapping auto|estimated|measured` (`cli/Options`). 기본 `auto`.
- `auto`: 관리자 권한이 아니거나 세션 시작이 실패하면 추정으로 동작하고 stderr 에 이유 한 줄. `estimated`: 수집기를 만들지 않는다. `measured`: 실패하면 이유를 출력하고 종료 코드 1.
- `HostInfo::thread_mapping` 은 수집기가 시작됐으면 `"measured"`. hello 의 `capabilities.thread_mapping` 이 이 값이다.
- 수집기가 도중에 죽으면(`ProcessTrace` 가 오류로 돌아옴) 이후 표본은 `thread_mapping` 을 비워 추정으로 돌아간다. `flows[].source` 는 매 스냅샷의 실제 출처다. 계약서의 "두 값 일치" 규칙은 수집기가 살아 있는 동안 유지되며, 이 예외를 계약서 4.3절 정정으로 적는다.
- `--dump` 표 머리에 `mapping measured|estimated` 를 표시한다.

## 9. 모듈 구조

```
engine/src/platform/RawTypes.h                         RawRunTime, RawThreadMapping, RawSample::thread_mapping, HostInfo::thread_mapping
engine/src/core/RunTimeTable.{h,cpp}                   누적 (순수)
engine/src/core/MeasuredFlows.{h,cpp}                  흐름 변환 (순수)
engine/src/core/DataAggregator.cpp                     분기
engine/src/platform/windows/EtwSchedulerCollector.{h,cpp}  세션·소비 스레드
engine/src/platform/windows/WindowsSystemReader.{h,cpp}    수집기 소유, drain
engine/src/cli/Options.{h,cpp}                         --mapping
engine/src/main.cpp, app/ServeApp.cpp                  옵션 전달, hello 능력
engine/src/cli/TableFormatter.cpp                      dump 머리
docs/.../2026-09-22-pulse-universe-contract-design.md  4.3·6.2절 정정
```

`core/` 는 여전히 Win32 를 모른다. 프론트엔드는 바꾸지 않는다 — `measured` 선은 이미 선명도 1.0 으로 그린다 (M7 D39).

## 10. 실패 동작

- 권한 없음 / 세션 시작 실패 / 같은 이름 세션을 멈추지 못함 → 8절 규칙.
- 이벤트 유실: 세션의 `EventsLost` 를 drain 때 읽어 늘어났으면 stderr 에 한 번 경고 (반복 출력은 10초에 한 번).
- 엔진이 강제 종료되어 세션이 남으면 다음 실행이 시작 때 정리한다 (7절).

## 11. 테스트 전략

| 대상 | 방식 |
|---|---|
| `RunTimeTable` | Catch2: 단일 코어 전환 정산, 여러 코어, 진행 중 조각의 drain 정산과 이월, 모르는 tid, Idle 제외, 늦게 온 이벤트 자르기, 빈 창, 스레드 종료 |
| `measuredFlows` | Catch2: 구성원 합산, 창 길이로 나눔·1 에서 자름, 걸러내기·상위 4개, 화면 밖 그룹 제외, 창 0 |
| `DataAggregator` 분기 | Catch2: `thread_mapping` 이 있으면 `source == "measured"`, 없으면 `"estimated"` |
| `Options` | Catch2: `--mapping` 세 값, 잘못된 값 |
| ETW 수집기 | 통합 테스트 1개: 관리자 권한일 때만 세션을 열고 1초 뒤 drain 에 실행 시간이 있는지. 권한이 없으면 `SKIP` |
| 전체 | 컨트롤러가 관리자 권한 엔진 + 브라우저로 확인 |

## 12. 완료 조건

- 엔진 빌드 경고 0, Catch2 전체 통과 (권한 없는 실행에서는 수집기 테스트가 SKIP).
- 권한 없이 `--serve`: 지금과 같다 (`estimated`, stderr 에 이유 한 줄).
- 관리자 권한 `--serve`: hello 가 `thread_mapping: "measured"`, 스냅샷의 flows 가 `source: "measured"`. 브라우저에서 선이 선명하고, 그룹마다 서로 다른 코어로 간다 (추정과 비교 스크린샷).
- 엔진 CPU 사용량 증가를 재어 기록한다 (기대: 코어 하나의 수 %).
- 엔진 종료 뒤 `logman query -ets` 에 `PulseUniverse-Sched` 가 없다.

## 13. 범위 밖

프로세스 이름·명령줄 등 ETW 의 다른 정보, 스레드별 표시, 코어 주파수·C-state, Linux 측정원, 권한 상승 요청 UI.

## 14. 구현 순서

1. 데이터 모델 + `RunTimeTable` + 테스트.
2. `measuredFlows` + `DataAggregator` 분기 + 테스트.
3. `--mapping` 옵션, `HostInfo`, hello, dump 머리 + 테스트.
4. `EtwSchedulerCollector`, `WindowsSystemReader` 연결, 통합 테스트, 계약서 정정.
