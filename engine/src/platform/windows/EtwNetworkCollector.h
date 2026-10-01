#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/RawNetwork.h"

namespace pulse {

// M11 스펙 3절. 전용 실시간 ETW 세션에서 Microsoft-Windows-Kernel-Network 의 데이터 전송 이벤트를 받아
// 플로우별 송수신 바이트를 쌓는다. 관리자 권한이 필요하다. 이벤트는 별도 스레드의 ProcessTrace 가 받고,
// 샘플링 스레드는 drain() 으로 창을 가져간다.
class EtwNetworkCollector final : public INetworkTrafficSource {
public:
    // 세션 이름. 엔진이 죽으며 남긴 세션은 다음 start() 가 먼저 멈춘다.
    static constexpr const wchar_t* kSessionName = L"PulseUniverse-Net";

    // 세션을 열고 소비 스레드를 띄운다. 실패하면 error 에 이유를 담고 nullptr.
    // 다른 pulse-engine 이 이미 같은 세션을 쓰고 있으면 그 세션을 멈추지 않고 실패한다.
    static std::unique_ptr<EtwNetworkCollector> start(std::string& error);

    // 이름으로 세션을 멈춘다. 콘솔 종료 신호로 죽을 때 소멸자가 돌지 못하므로 main 의 핸들러가 부른다.
    // 세션이 없어도 안전하다.
    static unsigned long stopSessionByName();

    ~EtwNetworkCollector() override;

    EtwNetworkCollector(const EtwNetworkCollector&) = delete;
    EtwNetworkCollector& operator=(const EtwNetworkCollector&) = delete;

    // 마지막 drain 이후의 누적을 돌려주고 비운다. 첫 호출(비교할 이전 시점이 없음)이거나 소비 스레드가
    // 멈췄으면 nullopt. 창 길이는 호출 사이의 단조 시계 간격이다 — 이벤트가 없는 창도 길이를 가진다.
    std::optional<RawNetworkTraffic> drain() override;

    // 이벤트 콜백. ProcessTrace 스레드에서 불린다. 외부에서 부르지 않는다.
    void onEvent(uint16_t event_id, const unsigned char* data, size_t length);

private:
    EtwNetworkCollector();

    // 세션의 유실 이벤트 수가 늘었으면 stderr 에 경고한다 (10초에 한 번).
    void warnIfEventsLost();

    void* owner_mutex_ = nullptr;  // 세션 소유권. 프로세스가 죽으면 커널이 닫아 준다.
    uint64_t session_ = 0;         // TRACEHANDLE (StartTrace)
    uint64_t consumer_ = 0;        // TRACEHANDLE (OpenTrace)
    std::vector<unsigned char> properties_;  // EVENT_TRACE_PROPERTIES + 이름
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::map<FlowKey, RawFlowTraffic> flows_;
    std::optional<std::chrono::steady_clock::time_point> window_start_;
    unsigned long events_lost_ = 0;
    unsigned long buffers_lost_ = 0;
    int64_t last_warning_ms_ = 0;
};

}  // namespace pulse
