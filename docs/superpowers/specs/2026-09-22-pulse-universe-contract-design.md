# Pulse Universe — 아키텍처 계약 설계

- 작성일: 2026-09-22
- 상태: 승인 대기
- 범위: 4개 레이어 사이의 인터페이스와 데이터 계약 확정. 개별 레이어의 내부 구현은 각자의 후속 스펙에서 다룬다.

## 1. 배경과 이 문서의 목적

Pulse Universe는 실행 중인 프로세스와 CPU 코어 상태를 C++로 수집해 Three.js 기반 3D 공간으로 시각화하는 인터랙티브 시스템 모니터다. 전체 기획은 4개의 독립 서브시스템으로 구성된다.

```
1. C++ Monitoring Engine   (Win32 API, 프로세스/CPU/스레드 수집)
2. Realtime Transport      (Boost.Beast WebSocket, 직렬화)
3. Frontend Data Layer     (React/TS, 스트림 수신, 보간, 상태)
4. Visualization Layer     (R3F, 셰이더, 파티클, GSAP, 포스트프로세싱)
```

이 문서는 넷 사이의 **계약**만 확정한다. 계약을 먼저 고정하는 이유는, 이 프로젝트의 가장 큰 기술적 불확실성이 "Windows에서 스레드-코어 매핑을 어디까지 실제로 얻을 수 있는가"이고, 그 답이 Thread Flow·Thread Migration 같은 핵심 비주얼의 실현 가능성을 직접 결정하기 때문이다. 계약 단계에서 이 제약을 드러내면, 얻을 수 없는 데이터에 의존하는 비주얼을 미리 대안으로 바꿀 수 있다.

## 2. 확정된 결정

| # | 결정 | 선택 | 근거 |
|---|---|---|---|
| D1 | 실행 권한 | 관리자 권한(elevated) 요구 | 프로세스 핸들 접근과 스레드별 CPU 시간 확보. ETW는 후속 확장 |
| D2 | 스레드-코어 매핑 | 1차는 추정, 계약에 `source` 필드로 실측 경로 예약 | ETW는 Phase 1 전체보다 큰 작업 |
| D3 | 스트리밍 범위 | 상위 N개 **그룹** (프로세스 아님) | 평평한 상위 N은 브라우저 렌더러로 화면이 점령됨 |
| D4 | 실행 형태 | 엔진 exe + 브라우저. 프론트엔드는 컨테이너를 모름 | 데스크톱 껍데기는 나중에 독립 추가 가능 |
| D5 | 자식 프로세스 표시 | 평소 접힘, Focus 시 위성으로 펼침 | 기획서 Focus Mode 전제. 기본 노출 개수는 파라미터 |
| D6 | 추가 수집 필드 | 실행 파일 경로·사용자 계정 포함. 디스크/네트워크 I/O 제외 | 앞의 둘은 즉시 가독성에 기여, I/O는 YAGNI |

### 2.1 되돌리기 쉬운 것과 어려운 것

- **쉬움**: 그룹 수(40), Focus 전 자식 노출 개수(0), 샘플링 주기(1000ms), 필터 지표 가중치. 전부 설정값 하나다.
- **어려움**: D1(권한), D6(수집 필드 집합). 엔진·스키마·프론트엔드를 동시에 고쳐야 한다.

## 3. 레이어 경계

각 레이어는 인접 레이어의 **구현**을 모른다. 이 프로젝트에서 가장 흔한 실패는 WebSocket JSON이 Win32 구조체를 그대로 닮고 R3F 컴포넌트가 그 JSON을 직접 읽는 형태다. 그렇게 되면 ETW를 붙일 때 셰이더까지 고쳐야 한다.

```
Layer 1  C++ Monitoring Engine
  platform/windows/   Win32 / NtQuery 원시 접근
       ↓ 플랫폼 중립 구조체
  process/ cpu/       델타 계산, 사용률 산출
       ↓
  core/DataAggregator SystemSnapshot 생성, 필터 정책 적용
       ↓ SystemSnapshot (순수 C++ 값 타입, JSON 모름)

Layer 2  Transport
  network/Serializer        SystemSnapshot → JSON
  network/WebSocketServer   Boost.Beast, 127.0.0.1 전용
  network/StaticFileServer  배포 빌드에서 프론트엔드 서빙
       ↓ JSON over WebSocket  ← 계약 지점

Layer 3  Frontend Data Layer
  hooks/useSystemStream  연결, 재연결, 파싱, 검증
  state/snapshotStore    최신 + 이전 스냅샷
  state/interpolator     1Hz → 60fps 보간 (Three.js 모름)
       ↓ 보간된 뷰 모델 (매 프레임)

Layer 4  Visualization
  three/ ProcessNode, CpuCore, ThreadFlow, ParticleSystem
  motion/ GSAP 전환
  shader/ GLSL          (WebSocket 모름)
```

### 3.1 경계 규칙

1. **L1 → L2**: 엔진은 순수 C++ 값 타입 `SystemSnapshot`만 내놓는다. 직렬화는 Transport의 책임이다. 이 덕분에 M1에서 네트워크 없이 같은 스냅샷을 CLI로 출력해 데이터 정확성을 먼저 검증할 수 있다.
2. **L2 → L3**: 계약은 버전 필드를 가진 JSON 스키마 하나다. 스레드-코어 매핑은 `source: "estimated" | "measured"` 를 달고 나간다. 프론트엔드는 이 값에 따라 시각적 확신도(Flow 선명도)만 달리하고 나머지 로직은 동일하게 유지한다.
3. **L3 → L4**: R3F 컴포넌트는 WebSocket 메시지가 아니라 매 프레임 보간된 값을 받는다. 시각화는 데이터가 1초마다 오는지 100ms마다 오는지 알 필요가 없다. 샘플링 주기를 바꿔도 시각화는 그대로다.
4. **컨테이너 무지**: 프론트엔드는 자신이 브라우저인지 데스크톱 껍데기 안인지 모른다. 이를 지키는 장치가 4.1의 엔드포인트 탐색 규칙이다.

## 4. 전송 계약

### 4.1 엔드포인트 탐색

```
VITE_PULSE_WS_URL 가 정의되어 있으면  → 그 값을 사용   (개발: Vite 5173 + 엔진 9000)
없으면                               → window.location 에서 유도  (배포: 엔진이 단일 origin)
```

프론트엔드 코드에 주소를 하드코딩하지 않는다. 배포 빌드는 same-origin이 되어 CORS 문제가 사라진다. 개발 중에만 엔진이 `Access-Control-Allow-Origin: http://localhost:5173` 을 허용한다.

### 4.2 보안 경계

엔진은 관리자 권한으로 동작하며 시스템 정보를 노출한다. 따라서:

- 리스닝 소켓은 `127.0.0.1` 에만 바인딩한다. `0.0.0.0` 바인딩을 금지한다.
- WebSocket 핸드셰이크에서 `Origin` 헤더를 허용 목록과 대조하고, 불일치 시 거절한다.
- 엔진은 어떤 경우에도 클라이언트로부터 명령을 받지 않는다. 계약은 서버 → 클라이언트 단방향이다. 이 제약이 관리자 권한 프로세스의 공격 표면을 최소로 유지한다.

### 4.3 메시지: hello

연결 직후 1회 전송한다.

```json
{
  "type": "hello",
  "v": 1,
  "interval_ms": 1000,
  "core_count": 16,
  "capabilities": { "thread_mapping": "estimated" },
  "host": { "os": "Windows 11", "elevated": true }
}
```

`capabilities.thread_mapping` 이 나중에 `"measured"` 로 바뀌는 것이 ETW 확장의 유일한 계약 변경 지점이다.

### 4.4 메시지: snapshot

`interval_ms` 마다 전송한다.

```json
{
  "type": "snapshot",
  "v": 1,
  "seq": 1423,
  "t": 1758531600123,
  "system": {
    "cpu_pct": 34.2,
    "mem_used_mb": 18432,
    "mem_total_mb": 32768,
    "process_total": 382,
    "thread_total": 4187
  },
  "cores": [
    { "id": 0, "pct": 82.4 },
    { "id": 1, "pct": 12.1 }
  ],
  "groups": [
    {
      "key": "whale.exe:22008",
      "name": "whale.exe",
      "root_pid": 22008,
      "cpu_pct": 12.4,
      "mem_mb": 3626.0,
      "proc_count": 26,
      "thread_count": 412,
      "started_at": 1758520000000,
      "account": "user",
      "image_path": "C:/Program Files/Naver/Whale/Application/whale.exe",
      "children": [
        {
          "pid": 8400,
          "name": "whale.exe",
          "role": "renderer",
          "cpu_pct": 4.1,
          "mem_mb": 570.5,
          "threads": 18
        }
      ]
    }
  ],
  "flows": [
    { "group": "whale.exe:22008", "core": 3, "weight": 0.42, "source": "estimated" }
  ],
  "lifecycle": {
    "spawned": [
      { "pid": 20114, "ppid": 22008, "name": "whale.exe", "group": "whale.exe:22008" }
    ],
    "terminated": [18002]
  },
  "ambient": { "service_proc_count": 84, "service_mem_mb": 1400 }
}
```

### 4.5 필드 계약

| 필드 | 타입 | 의미 | 대응 비주얼 |
|---|---|---|---|
| `seq` | int | 메시지 순번. 건너뛰면 보간 리셋 | 재연결 시 노드 미끄러짐 방지 |
| `t` | int | Unix epoch ms | 타임라인 확장의 기반 |
| `cores[].pct` | float 0~100 | 코어별 사용률 | Core Orb 크기·셰이더 왜곡·파티클 밀도 |
| `groups[].key` | string | 그룹 안정 식별자 `name:root_pid` | React key, 노드 동일성 |
| `groups[].mem_mb` | float | 트리 전체 합산 메모리 | Node 크기 |
| `groups[].cpu_pct` | float 또는 null | 트리 전체 합산 CPU | Pulse 속도, Glow 세기 |
| `groups[].account` | `"user"` 또는 `"system"` | 실행 계정 구분 | 색상 계열 구분 |
| `groups[].image_path` | string | 실행 파일 경로 | 아이콘 추출 |
| `groups[].children[]` | array | 자식 프로세스. 평소 미표시 | Focus 시 위성 배치 |
| `groups[].children[].role` | string | 자식 역할(`renderer`, `gpu-process` 등). 판별 불가 시 `"child"` | 위성 라벨·색 구분 |
| `flows[].group` | string | 출발 그룹의 `key` | Flow 시작점 |
| `flows[].core` | int | 도착 코어 `id` | Flow 도착점 |
| `flows[].weight` | float 0~1 | 그룹 → 코어 흐름 세기 | Flow 굵기·입자 밀도 |
| `flows[].source` | `"estimated"` 또는 `"measured"` | 매핑 신뢰도 | Flow 선명도 |
| `lifecycle.spawned` | array | 실제 생성된 프로세스 | 파티클 수렴 생성 |
| `lifecycle.spawned[].group` | string | 소속 그룹의 `key`. 그룹에 속하지 않으면 빈 문자열 | 어느 노드에서 생성 연출을 재생할지 |
| `lifecycle.terminated` | array of pid | 실제 종료된 프로세스 | Collapse / 붕괴 |
| `ambient` | object | svchost 계열 집계 | 배경 미세 입자 |

### 4.6 lifecycle 이 별도로 존재하는 이유

프론트엔드가 연속한 스냅샷을 비교해 "사라진 항목 = 종료"로 판단하면 **틀린다**. 상위 N개 그룹만 전송하므로, 조용해져서 목록에서 빠진 것과 실제 종료된 것이 구분되지 않는다. 전자에 붕괴 애니메이션을 재생하면 살아 있는 프로그램이 터지는 장면이 나온다.

따라서 엔진은 전체 프로세스를 계속 추적하면서 **실제 생성/종료만** `lifecycle` 에 담는다. 목록에서 빠진 항목은 조용히 페이드아웃하고, `terminated` 에 오른 항목만 붕괴시킨다.

## 5. 그룹화 정책

### 5.1 그룹 정의

프로세스 트리의 루트로 묶는다. 부모와 자식의 이미지 이름이 같으면 부모 쪽으로 계속 올라가고, 달라지는 지점에서 멈춘다. 이 규칙은 특정 브라우저 이름을 하드코딩하지 않으며 Chromium 계열, Electron 앱, 언어 런타임에 동일하게 적용된다.

실측 예 (본 개발 PC, 382 프로세스 / 103 그룹):

```
Code.exe         29 procs   3,649 MB
whale.exe        26 procs   3,626 MB
Figma.exe        14 procs   2,351 MB
claude.exe       13 procs   2,031 MB
Exosphere.exe    15 procs   1,634 MB
msedgewebview2   25 procs   1,346 MB
ChatGPT.exe      10 procs     941 MB
node.exe         11 procs     931 MB
Notion.exe        7 procs     733 MB
explorer.exe      1 proc      343 MB
dwm.exe           1 proc      162 MB
SearchHost.exe    1 proc      122 MB
```

상위 40개 그룹을 전송하면 질량 편차가 약 30배인 노드 집합이 되고, 대부분 사용자가 이름으로 식별 가능하다.

### 5.2 svchost 예외

`svchost.exe` 는 본 PC에서 84개가 동작하며 서로 무관한 Windows 서비스들이다. 하나로 묶으면 정체불명의 거대 노드가 되고, 풀어놓으면 화면을 점령한다. 따라서 그룹 목록에서 제외하고 `ambient` 집계로만 전송해 배경 입자로 표현한다.

### 5.3 선택 지표

그룹 점수는 합산 CPU와 합산 메모리의 가중합으로 계산하며, 가중치는 설정값이다. 사용자 계정(`account == "user"`) 그룹에 가산점을 둬 사용자가 직접 띄운 프로그램이 배경 서비스보다 우선 노출되게 한다.

## 6. 엔진 측정 방식

### 6.1 CPU 사용률은 델타로 구한다

Windows는 순간 CPU 사용률을 제공하지 않고 누적 CPU 시간만 제공한다. 따라서 직전 표본을 보관했다가 뺀다.

```
직전 누적   12,450 ms
현재 누적   12,574 ms
델타           124 ms
÷ (1000 ms × 16 cores) = 0.78 %
```

**귀결: 엔진 기동 직후 첫 주기에는 프로세스·그룹의 CPU 값이 존재하지 않는다.** 이 구간의 `groups[].cpu_pct` 는 `null` 로 전송하며, 프론트엔드는 해당 노드를 무채색으로 표시했다가 첫 실측이 도착하면 활성화한다. 결함이 아니라 의도된 인트로로 취급한다.

**단, `system.cpu_pct` 는 이 규칙에서 제외된다.** 이 값은 델타에서 파생되지 않고 코어별 부하(PDH)의 평균이다. 수집기가 생성 시점에 한 번 수집해 두므로 첫 스냅샷에서도 `null` 이 아니며, `null` 인 경우는 코어 목록 자체가 비어 있을 때뿐이다.

다만 **첫 스냅샷의 값은 대개 0.0 이다.** PDH 는 두 수집 사이의 경과 시간에 대해 비율을 계산하는데, 생성자와 첫 `read()` 사이는 수 밀리초에 불과해 측정 창이 사실상 비어 있다. 이는 결함이 아니라 PDH 의 정의상 결과다. 프론트엔드 입장에서 중요한 차이는 이것이다 — 그룹 CPU 는 `null`(모름)이고, 시스템 CPU 는 `0.0`(측정했고 값이 0)이다. 두 번째 스냅샷부터는 정상적인 값이 나온다.

### 6.1.1 메모리 지표

`groups[].mem_mb` 는 구성원 프로세스의 `WorkingSetSize` 합이다.

작업 관리자의 "메모리" 열은 private working set(상주 + 비공유)이지만 `GetProcessMemoryInfo` 로는 얻을 수 없다. 얻을 수 있는 두 값은 모두 과대 계상한다. `WorkingSetSize` 는 공유 페이지를 그룹 구성원 수만큼 중복 계산하고, `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` 는 상주하지 않는 커밋까지 포함한다. 본 개발 PC 실측에서 후자가 더 크게 벗어났다.

```
                 WorkingSetSize    PrivateUsage
whale.exe  x31      4344 MB           5135 MB
Code.exe   x29      3379 MB           3800 MB
Figma.exe  x14      2526 MB           4709 MB
```

따라서 `WorkingSetSize` 를 쓴다. 실제 private working set 은 `NtQueryInformationProcess` 가 필요하며 후속 과제로 남긴다.

### 6.2 Thread Flow 추정

1차 구현에서 `flows` 는 실측이 아니다. 그룹의 CPU 점유율을 현재 코어별 부하에 비례 배분해 `weight` 를 만든다. 기여도가 낮은 흐름은 잘라내어 화면을 어지럽히지 않는다.

이 값은 `source: "estimated"` 로 표시되고 시각적으로 낮은 불투명도로 그려진다. ETW 확장이 들어오면 동일한 필드가 `"measured"` 로 바뀌고 선명해진다. **추정을 실측처럼 보여주지 않는 것**이 이 설계의 원칙이다.

**ETW 교체 비용에 대한 정정 (M1 구현 후).** 이 문서는 당초 ETW 수집기를 끼워 넣어도 위 레이어가 바뀌지 않는다고 서술했다. M1 구현 결과 그 주장은 과장이었다. 와이어 필드(`flows[].source`)는 예약돼 있지만 **데이터 모델에는 측정값이 들어갈 자리가 없다.** `RawSample` 은 프로세스 목록과 코어 목록을 서로 무관한 두 배열로 담고 있고, ETW 수집기의 산출물은 바로 그 둘을 잇는 스레드-코어 매핑이다. 실제 교체에는 최소한 `RawSample` 의 새 필드, `FlowEstimator` 를 대체할 생산자, 그리고 `DataAggregator` 의 분기가 필요하다.

시각화 레이어(Layer 4)는 여전히 영향을 받지 않는다 — 그쪽은 `Flow` 의 `weight` 와 `source` 만 읽는다. 바뀌는 범위는 Layer 1 내부와 `RawSample` 경계까지다. 측정값의 형태가 확정되기 전에 미리 자리를 비워두는 것은 이득이 없으므로, M1 시점에는 이 제약을 기록만 하고 구조를 바꾸지 않는다.

### 6.3 검증 방법

엔진은 `--dump` 모드를 제공한다. 네트워크 없이 스냅샷을 콘솔 표로 출력해 작업 관리자와 직접 대조한다. 숫자가 검증되지 않은 채 시각화 단계로 넘어가면, 이후 이상 현상이 셰이더 결함인지 데이터 결함인지 구분할 수 없게 된다.

## 7. 프론트엔드 데이터 레이어

### 7.1 보간

데이터는 1 Hz로 도착하고 화면은 60 fps로 그려진다. 보간은 데이터 레이어의 책임이며 시각화는 관여하지 않는다.

- 연속값(`cpu_pct`, `mem_mb`, `cores[].pct`)은 직전 스냅샷에서 현재 스냅샷으로 주기에 걸쳐 보간한다.
- `seq` 가 불연속이면 보간을 리셋하고 새 값으로 즉시 점프한다.
- 노드의 물리적 위치는 보간 대상이 아니다. Force 시뮬레이션이 매 프레임 자체적으로 갱신한다.

### 7.2 연결 관리

`useSystemStream` 이 연결·재연결·지수 백오프·스키마 검증을 담당한다. `v` 가 예상과 다르면 데이터를 버리고 사용자에게 버전 불일치를 알린다. 잘못된 모양의 메시지를 시각화 레이어로 통과시키지 않는다.

## 8. 구현 순서

각 단계는 **화면에 보이는 결과물**을 기준으로 나눴다. 아무것도 보이지 않는 구간을 길게 두지 않는 것이 목적이다.

| # | 단계 | 완료 시 확인 가능한 것 |
|---|---|---|
| M1 | C++ 엔진 수집 + `--dump` | 콘솔 그룹 표. 작업 관리자와 숫자 대조 |
| M2 | WebSocket 전송 | 브라우저 콘솔에 1초마다 JSON |
| M3 | React 데이터 레이어 + 숫자 대시보드 | 표·막대 기반 일반 모니터. 데이터 정확성 최종 확인 |
| M4 | R3F 장면 + Process Node | 첫 우주. 크기·맥박·발광을 가진 40개 천체 |
| M5 | 생성/종료 + Focus + 카메라 | 파티클 수렴 생성, 붕괴, 클릭 시 자식 전개 |
| M6 | CPU Core Orb + 셰이더 + 파티클 | 부하에 따라 일그러지고 빛나는 코어 |
| M7 | Thread Flow | 프로세스와 코어를 잇는 빛의 흐름 |
| M8 | 포스트프로세싱 + 비주얼 마감 | Bloom, 심도, 최종 룩 |
| M9 | 최적화 | 인스턴싱, 오브젝트 풀, 성능 측정 |

M3은 생략하지 않는다. 의도적으로 평범한 대시보드를 만들어 숫자를 검증한 뒤 3D로 넘어간다. 이 단계를 건너뛰면 M6~M7에서 셰이더를 오래 의심하게 된다.

ETW 기반 실측 스레드-코어 매핑은 M9 이후의 독립 확장으로 둔다. 계약에 `source` 와 `capabilities.thread_mapping` 이 이미 있으므로 나머지 레이어 변경 없이 교체된다.

## 9. 기술 스택

```
C++       C++20 / MSVC / CMake / vcpkg (Boost.Asio, Boost.Beast)
Frontend  Vite + React + TypeScript
State     Zustand
3D        three.js + @react-three/fiber + drei + @react-three/postprocessing
Motion    GSAP
```

GSAP은 상태 전환(생성, 소멸, Focus, 카메라, 패널)에만 사용한다. 반복 모션(부유, 맥박, 파티클, Flow, Glow)은 Three.js 렌더 루프와 셰이더에서 처리한다. 노드마다 GSAP 타임라인을 두는 구조는 채택하지 않는다.

## 10. 테스트 전략

| 대상 | 방식 |
|---|---|
| CPU 델타 계산, 그룹화 규칙, 필터 정렬 | C++ 단위 테스트. OS 호출을 인터페이스 뒤로 분리해 가짜 표본을 주입 |
| 플랫폼 리더 | 통합 테스트. 실제 Win32 호출이 형식에 맞는 값을 돌려주는지 확인 |
| 직렬화 | 스냅샷 → JSON 후 스키마 대조 |
| 전체 데이터 정확성 | `--dump` 출력과 작업 관리자 수동 대조 (M1, M3) |
| 프론트엔드 데이터 레이어 | 기록된 JSON 재생으로 보간·재연결·버전 불일치 검증 |
| 시각화 | 자동 테스트 대상 아님. 기록된 스트림 재생으로 육안 확인 |

## 11. 범위 밖

- Linux / macOS 지원. 플랫폼 리더는 인터페이스 뒤에 두되 구현은 Windows만 한다.
- 데스크톱 껍데기(Tauri/Electron) 번들링, 코드 서명, 설치 프로그램.
- 기획서 34장의 Memory / Network / GPU Universe, 타임라인 재생.
- 원격 호스트 감시. 계약은 로컬 전용이며 단방향이다.

## 12. 후속 스펙

이 문서는 계약만 다룬다. 다음 스펙이 각 레이어의 내부를 다룬다.

1. C++ 엔진 내부 설계 (M1~M2)
2. 프론트엔드 데이터 레이어와 검증용 대시보드 (M3)
3. Process Universe 시각화 (M4~M5)
4. CPU Core Universe 와 Thread Flow (M6~M8)
