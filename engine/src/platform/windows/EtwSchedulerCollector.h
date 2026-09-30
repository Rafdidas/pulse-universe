#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/RunTimeTable.h"
#include "platform/RawTypes.h"

namespace pulse {

// ETW 스펙 7절. 전용 시스템 로거 세션으로 스레드 생성·종료와 문맥 전환(CSwitch)
// 이벤트를 받아 RunTimeTable 에 쌓는다. 관리자 권한이 필요하다.
// 이벤트는 별도 스레드의 ProcessTrace 가 받고, 샘플링 스레드는 drain() 으로 창을 가져간다.
class EtwSchedulerCollector {
public:
    // 세션 이름. 엔진이 죽으며 남긴 세션은 다음 start() 가 먼저 멈춘다.
    static constexpr const wchar_t* kSessionName = L"PulseUniverse-Sched";

    // 세션을 열고 소비 스레드를 띄운다. 실패하면 error 에 이유를 담고 nullptr.
    static std::unique_ptr<EtwSchedulerCollector> start(std::string& error);

    ~EtwSchedulerCollector();

    EtwSchedulerCollector(const EtwSchedulerCollector&) = delete;
    EtwSchedulerCollector& operator=(const EtwSchedulerCollector&) = delete;

    // 소비 스레드가 살아 있으면 창을 닫아 돌려준다. 멈췄으면 nullopt — 호출자는
    // 추정으로 돌아간다.
    std::optional<RawThreadMapping> drain();

    // 이벤트 콜백. ProcessTrace 스레드에서 불린다. 외부에서 부르지 않는다.
    void onThreadStarted(uint32_t pid, uint32_t tid);
    void onThreadEnded(uint32_t tid);
    void onContextSwitch(uint32_t core, uint32_t new_tid, int64_t ts);

private:
    EtwSchedulerCollector();

    // 세션의 유실 이벤트 수가 늘었으면 stderr 에 경고한다 (10초에 한 번).
    void warnIfEventsLost();

    uint64_t session_ = 0;   // TRACEHANDLE (StartTrace)
    uint64_t consumer_ = 0;  // TRACEHANDLE (OpenTrace)
    std::vector<unsigned char> properties_;  // EVENT_TRACE_PROPERTIES + 이름
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    RunTimeTable table_;
    unsigned long events_lost_ = 0;
    int64_t last_warning_ms_ = 0;
};

}  // namespace pulse
