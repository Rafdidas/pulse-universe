# ETW 실측 스레드-코어 매핑 설계

- 작성일: 2026-09-30
- 상태: 승인됨 (시제품 측정 반영)
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

### 2.1 시제품 결과

관리자 PowerShell(UAC)에서 확인했다.

- ETW 통합 테스트 2개 통과 (이 프로세스의 0.5 초 바쁜 루프가 0.4 초 넘게 잡힘, 파괴 뒤 같은 이름으로 다시 열림). 권한 없이 돌리면 두 테스트는 SKIP.
- `--json --mapping measured`: flows 13개가 모두 `measured`. `Code.exe` 는 코어 8·24·2·12, `ExoAM_x64.exe` 는 24·25·16·26.
- 브라우저(dev 서버)에서 엔진에 WebSocket 으로 붙어 확인: hello 의 `capabilities.thread_mapping` 이 `"measured"`, `host.elevated` 가 `true`, 스냅샷 flows 가 모두 `measured` (`Code.exe` 는 코어 10·8·4·12, `ExoAM_x64.exe` 는 16·20·24·17). 배지에 `elevated` 가 보였다. 창이 가려져 있어 장면 스크린샷은 찍지 못했다.
- 엔진 비용 (Release, `--serve`, 12 초 평균):

| 매핑 | CPU | 작업 집합 |
|---|---|---|
| estimated | 13.0 ~ 16.9 ms/s | 12 MB |
| measured, 버퍼 1 MB × 64~256 | 20.8 ~ 24.7 ms/s | 71 MB |
| measured, 버퍼 256 KB × 16~128 (확정) | 대기 16.9 / 4코어 부하 22.1 ms/s | 29 MB |

  커널은 코어마다 버퍼를 따로 잡는다. 작업 집합은 최소 버퍼 수보다 버퍼 크기 × 코어 수를 따른다 (최소 64 → 16 은 변화 없음, 1 MB → 256 KB 에서 71 → 29 MB). 256 KB 에서 대기·부하 모두 버퍼 유실 0.
- 종료: `--iterations` 로 스스로 끝나면 세션이 남지 않는다. `Stop-Process -Force` 로 죽이면 `PulseUniverse-Sched` 가 남고, 다음 실행이 시작 때 멈추고 새로 연다 (확인함).

### 2.2 검토에서 나온 우려의 검증

최종 리뷰는 두 가지를 우려했다. 둘 다 정답을 아는 부하로 직접 재어 본 뒤 결론을 냈다.

**(1) 코어 간 시계 어긋남.** 실시간 ETW 는 코어별 버퍼를 따로 넘기므로, 한가한 코어의 마지막 이벤트가 바쁜 코어보다 늦게 도착하면 `drain()` 이 그 코어의 마지막 스레드를 창 끝까지 돈 것으로 잡을 수 있다는 우려였다. 정답값은 엔진 밖에서 만든 부하 프로세스가 스스로 잰 바쁜 시간이다 (스피너 = 100%, 버스트 = 5 ms 바쁨 + 대기의 실측 합, 희소 스레드 = 250 ms 마다 1 ms). ETW 가 그 프로세스에 귀속한 코어 수를 정답값으로 나눈 비:

| 부하 | 수집기 그대로 (창을 지연하지 않음) | 창을 1.5 초 물려 닫는 변형 |
|---|---|---|
| 스피너 4 + 버스트 2 | 1.004 | 1.002 |
| 버스트 8 | 1.021 | 1.008 |
| 버스트 16 | 1.001 | 0.996 |
| 스피너 2 + 버스트 16 | 1.001 | 1.003 |
| 스피너 1 + 희소 24 | 1.009 | 1.014 |
| 스피너 3 + 희소 64 | 1.001 | 1.001 |
| 스피너 4 + 버스트 4 + 희소 32 | 1.001 | 1.001 |

오차는 어느 쪽에서도 2% 안이고, 지연을 두어도 나아지지 않는다. 그래서 창을 지연해 닫는 복잡한 구조(이벤트를 모아 두었다가 시각 순서로 정산)는 넣지 않는다. 우려가 옳다면 한가한 코어가 많은 희소 부하에서 비가 1 을 크게 넘어야 하는데 그렇지 않았다. 참고로 처음에는 프로세스 CPU 시간(`GetProcessTimes`)을 정답값으로 썼다가 짧은 버스트에서 13% 어긋나 보였다 — 그 값이 15.6 ms 틱 표집이라 부정확한 것이었다.

**(2) Ctrl+C.** `--serve` 는 정상 종료하고 세션이 남지 않는다 (asio 가 신호를 받는다). 신호 처리가 없는 `--dump`·`--json` 은 죽을 때 소멸자가 돌지 못해 세션이 남았다 → 8절의 콘솔 핸들러로 고쳤고, 고친 뒤 `--dump`·`--serve` 모두 Ctrl+C 뒤에 세션이 남지 않음을 확인했다. 두 엔진을 동시에 띄우면 둘째는 뮤텍스 때문에 `estimated` 로 물러나고 첫째의 세션은 그대로 살아 있음을 확인했다.


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
- `contextSwitch(core, newTid, ts)` — 코어 `core` 의 현재 조각(이전 스레드, 조각을 열 때 알던 pid, 시작 시각)을 `ts` 까지 정산하고 새 조각을 연다. 정산 때 pid 는 스레드 표에서 다시 찾고, 그 사이 스레드가 끝나 표에 없으면 조각을 열 때 알던 pid 를 쓴다. 둘 다 몰라 pid 가 0 이면(Idle 도 0) 쌓지 않는다.
- `drain() -> RawThreadMapping` — 창을 닫는다. 기준 시각 `now` 는 지금까지 본 이벤트 시각의 최댓값이다. 코어마다 진행 중인 조각을 `now` 까지 정산하고 조각 시작을 `now` 로 옮긴다 (문맥 전환 없이 코어를 독점한 스레드도 센다). 창 길이 = `now − 직전 drain 의 now`. 첫 drain 은 첫 이벤트 시각부터다. 이벤트가 하나도 없었으면 창 길이 0, 빈 목록.
- 이벤트가 `now` 보다 이른 시각으로 늦게 도착하면(버퍼 병합 차이) 정산 길이는 0 으로 자른다.

## 6. 흐름 변환 (`core/MeasuredFlows`, 순수)

`measuredFlows(groups, mapping, cfg) -> vector<Flow>`

- 그룹 구성원 pid (`root_pid` 와 `children[].pid`) 의 실행 시간을 코어별로 더한다.
- `weight = 합 / window_seconds`, 1 에서 자른다. `window_seconds <= 0` 이면 빈 목록.
- `FlowConfig` 의 `min_weight` 미만은 버리고 그룹당 `max_flows_per_group` 개를 weight 내림차순으로 남긴다 (FlowEstimator 와 같은 규칙).
- `source = "measured"`.
- 화면에 남은 그룹(`snapshot.groups`)에 대해서만 만든다.
- `DataAggregator` 는 `cores[]` 에 없는 코어로 가는 흐름을 버린다. ETW 의 코어 번호는 프로세서 그룹을 가로질러 이어지지만 코어 부하(PDH)는 그룹 0 만 보므로, 64 개를 넘는 기계에서는 그릴 곳이 없다.

`DataAggregator` 7단계: `sample.thread_mapping` 이 있으면 `measuredFlows`, 없으면 `flow_estimator_.estimate`.

## 7. ETW 수집기 (`platform/windows/EtwSchedulerCollector`)

- 세션: `StartTraceW` 로 `PulseUniverse-Sched` 를 연다. `LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE`, 버퍼 256 KB, 최소 16 · 최대 128 개 (2.1절), `FlushTimer = 1` 초, `Wnode.ClientContext = 1` (QPC). 세션 GUID 는 고정된 자체 GUID. 시작 전에 같은 이름의 세션을 `ControlTraceW(STOP)` 로 멈춘다 (이전 실행이 죽으며 남긴 것).
- 사용 이벤트: 세션 속성의 `EnableFlags = EVENT_TRACE_FLAG_PROCESS | THREAD | CSWITCH`.
- 소비: 별도 스레드에서 `OpenTraceW`(실시간, EVENT_RECORD) + `ProcessTrace`. 콜백은 Thread 공급자(`{3d6fa8d1-…}`) opcode 1·3(Start·DCStart) → `threadStarted`, 2(End) → `threadEnded`, 36(CSwitch) → `contextSwitch(GetEventProcessorIndex, NewThreadId, TimeStamp)`. 페이로드는 앞의 uint32 들만 읽는다 (Thread: ProcessId, TThreadId / CSwitch: NewThreadId).
- `ProcessTrace` 는 세션 시작 뒤의 스레드만 Start 로 알린다. 이미 있던 스레드는 세션을 켤 때 커널이 보내는 DCStart 런다운으로 채운다 (2절 측정에서 DCStart 8,400여 건 확인).
- 종료: 소멸자에서 `ControlTraceW(STOP)` → `CloseTrace` → 소비 스레드 join. `CloseTrace` 를 join 앞에 둔 것은 STOP 이 실패해도 `ProcessTrace` 가 풀리게 하려는 것이다.
- 소유권: 세션을 열기 전에 이름 붙은 뮤텍스 `Global\PulseUniverse-Sched-Owner` 를 만든다. 이미 있으면 살아 있는 다른 엔진이 쓰는 세션이므로 멈추지 않고 실패한다 (`auto` 는 그 이유로 추정으로 간다). 뮤텍스는 프로세스가 죽으면 커널이 닫으므로, 뮤텍스가 없는데 세션이 남아 있다면 죽은 엔진의 것이라 시작 때 멈춰도 안전하다.
- 콘솔 종료 신호: `main` 이 콘솔 핸들러를 달아, Ctrl+C·창 닫기로 죽을 때 이름으로 세션을 멈춘다 (`--dump`·`--json` 은 신호 처리가 없어 소멸자가 돌지 못한다). `--serve` 는 asio 가 Ctrl+C 를 먼저 받아 정상 종료하므로 소멸자가 멈춘다.
- `WindowsSystemReader::read()` 가 수집기가 있으면 `drain()` 결과를 `RawSample::thread_mapping` 에 싣는다.
- 틱/초: `PROCESS_TRACE_MODE_RAW_TIMESTAMP` 없이 열면 `ProcessTrace` 가 시각을 FILETIME(100 ns)으로 바꿔 준다. 1e7.

## 8. 옵션과 권한 (D56)

- `--mapping auto|estimated|measured` (`cli/Options`). 기본 `auto`.
- `auto`: 관리자 권한이 아니거나 세션 시작이 실패하면 추정으로 동작하고 stderr 에 이유 한 줄. `estimated`: 수집기를 만들지 않는다. `measured`: 실패하면 이유를 출력하고 종료 코드 1.
- `HostInfo::thread_mapping` 은 수집기가 시작됐으면 `"measured"`. hello 의 `capabilities.thread_mapping` 이 이 값이다. hello 는 엔진이 시작할 때 한 번 만들어지므로 "엔진 시작 시점의 상태" 다.
- 수집기가 도중에 죽으면(`ProcessTrace` 가 오류로 돌아옴) stderr 에 한 줄을 남기고 이후 표본은 `thread_mapping` 을 비워 추정으로 돌아간다. 창이 아직 길이를 갖지 못한 표본(기동 직후 첫 표본, 이벤트가 아직 안 온 표본)도 `thread_mapping` 이 비어 추정이 된다 — 빈 창을 실측으로 내보내면 그 스냅샷의 `flows` 가 비기 때문이다. `flows[].source` 는 매 스냅샷의 실제 출처다. 계약서의 "두 값 일치" 규칙은 수집기가 살아 있는 동안 유지되며, 이 예외를 계약서 4.3절 정정으로 적는다.
- 모든 모드에서 시작할 때 stderr 에 한 줄: `thread mapping: measured (ETW)`, 또는 `auto` 가 물러설 때 `thread mapping: estimated - <이유>`. `--json` 의 stdout 은 그대로다. `--dump` 표는 바꾸지 않는다.

## 9. 모듈 구조

```
engine/src/platform/RawTypes.h                         RawRunTime, RawThreadMapping, RawSample::thread_mapping, HostInfo::thread_mapping
engine/src/core/RunTimeTable.{h,cpp}                   누적 (순수)
engine/src/core/MeasuredFlows.{h,cpp}                  흐름 변환 (순수)
engine/src/core/DataAggregator.cpp                     분기
engine/src/platform/windows/EtwSchedulerCollector.{h,cpp}  세션·소비 스레드
engine/src/platform/windows/WindowsSystemReader.{h,cpp}    수집기 소유, drain
engine/src/cli/Options.{h,cpp}                         --mapping
engine/src/main.cpp, app/ServeApp.cpp                  옵션 전달, 시작 한 줄, hello 능력
engine/src/core/Snapshot.h, network/Serializer.h        두 값의 뜻을 적은 주석
docs/.../2026-09-22-pulse-universe-contract-design.md  4.3·6.2절 정정
```

`core/` 는 여전히 Win32 를 모른다. 프론트엔드는 바꾸지 않는다 — `measured` 선은 이미 선명도 1.0 으로 그린다 (M7 D39).

## 10. 실패 동작

- 권한 없음 / 세션 시작 실패 / 같은 이름 세션을 멈추지 못함 → 8절 규칙.
- 이벤트 유실: 세션의 `EventsLost`(커널이 버퍼를 못 채워 잃음)와 `RealTimeBuffersLost`(소비자가 늦어 잃은 버퍼) 를 drain 때 읽어 어느 쪽이든 늘었으면 stderr 에 경고 (반복 출력은 10초에 한 번).
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
- 엔진 CPU 사용량 증가를 재어 기록한다 (시제품: Release 에서 +4~8 ms/s, 작업 집합 +17 MB).
- 엔진 종료 뒤 `logman query -ets` 에 `PulseUniverse-Sched` 가 없다.

## 13. 범위 밖

프로세스 이름·명령줄 등 ETW 의 다른 정보, 스레드별 표시, 코어 주파수·C-state, Linux 측정원, 권한 상승 요청 UI.

## 14. 구현 순서

1. 데이터 모델 + `RunTimeTable` + 테스트.
2. `measuredFlows` + `DataAggregator` 분기 + 테스트.
3. `--mapping` 옵션 + 테스트.
4. `EtwSchedulerCollector`, `WindowsSystemReader` 연결, main 의 시작 한 줄, hello 능력, 통합 테스트, 계약서 정정.
