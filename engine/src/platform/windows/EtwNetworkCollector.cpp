#include "platform/windows/EtwNetworkCollector.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <evntcons.h>
#include <evntrace.h>

#include <chrono>
#include <cstdio>
#include <cwchar>

#include "core/FlowKey.h"
#include "core/NetworkEvents.h"

namespace pulse {
namespace {

// Microsoft-Windows-Kernel-Network 공급자.
constexpr GUID kNetworkProvider = {
    0x7dd42a49, 0x5329, 0x4832, {0x8d, 0xfd, 0x43, 0xd9, 0x79, 0x15, 0x3a, 0x88}};

// 세션 식별용.
constexpr GUID kSessionGuid = {
    0x2c8d4e61, 0x93a5, 0x4f2b, {0xb7, 0x10, 0x5e, 0x6a, 0x91, 0x0d, 0x3c, 0xf8}};

// KERNEL_NETWORK_KEYWORD_IPV4 | KERNEL_NETWORK_KEYWORD_IPV6.
constexpr ULONGLONG kKeywords = 0x10 | 0x20;

// 이벤트 한 건은 작지만 큰 전송은 짧은 시간에 수만 건을 낸다. 시험에서 16 KB x 32 개 버퍼는 1 KB 조각 5000 번
// 전송의 약 9 % 를 잃었다. 64 KB x 최대 64 개(4 MB)는 그 급증을 받는다. 상주 메모리는 필요한 만큼만 늘어난다.
constexpr ULONG kBufferKb = 64;
constexpr ULONG kMinBuffers = 16;
constexpr ULONG kMaxBuffers = 64;
// 이벤트가 적을 때도 1초 안에 전달되게 한다.
constexpr ULONG kFlushSeconds = 1;
// drain() 이 버퍼를 강제로 흘려보낸 뒤 소비 스레드가 그 이벤트를 처리하도록 기다리는 시간.
constexpr int kFlushSettleMs = 250;

// 세션 소유권을 나타내는 이름 붙은 뮤텍스. 살아 있는 다른 엔진의 세션을 멈추지 않게 한다.
constexpr const wchar_t* kOwnerMutexName = L"Global\\PulseUniverse-Net-Owner";

// 유실 경고를 이보다 자주 찍지 않는다.
constexpr int64_t kLostWarningIntervalMs = 10000;

std::vector<unsigned char> makeProperties() {
    const size_t name_bytes = (std::wcslen(EtwNetworkCollector::kSessionName) + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(EVENT_TRACE_PROPERTIES) + name_bytes, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    props->Wnode.BufferSize = static_cast<ULONG>(buffer.size());
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;  // QPC
    props->Wnode.Guid = kSessionGuid;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->BufferSize = kBufferKb;
    props->MinimumBuffers = kMinBuffers;
    props->MaximumBuffers = kMaxBuffers;
    props->FlushTimer = kFlushSeconds;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return buffer;
}

EVENT_TRACE_PROPERTIES* asProperties(std::vector<unsigned char>& buffer) {
    return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
}

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ProcessTrace 스레드에서 불린다. 이 공급자의 이벤트만 넘긴다.
void WINAPI onEventRecord(PEVENT_RECORD record) {
    auto* collector = static_cast<EtwNetworkCollector*>(record->UserContext);
    if (collector == nullptr || !IsEqualGUID(record->EventHeader.ProviderId, kNetworkProvider)) {
        return;
    }
    collector->onEvent(record->EventHeader.EventDescriptor.Id, static_cast<const unsigned char*>(record->UserData),
                       record->UserDataLength);
}

}  // namespace

EtwNetworkCollector::EtwNetworkCollector() : properties_(makeProperties()) {}

unsigned long EtwNetworkCollector::stopSessionByName() {
    std::vector<unsigned char> props = makeProperties();
    return ::ControlTraceW(0, kSessionName, asProperties(props), EVENT_TRACE_CONTROL_STOP);
}

std::unique_ptr<EtwNetworkCollector> EtwNetworkCollector::start(std::string& error) {
    std::unique_ptr<EtwNetworkCollector> collector(new EtwNetworkCollector());

    // 살아 있는 다른 엔진이 이 세션을 쓰고 있으면 건드리지 않는다. 뮤텍스는 그 프로세스가 죽으면
    // 커널이 없애므로, 이미 있는데 소유자가 없는 경우는 없다.
    HANDLE owner = ::CreateMutexW(nullptr, FALSE, kOwnerMutexName);
    if (owner == nullptr) {
        error = "CreateMutex failed with error " + std::to_string(::GetLastError());
        return nullptr;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(owner);
        error = "another pulse-engine is already measuring network traffic";
        return nullptr;
    }
    collector->owner_mutex_ = owner;

    // 소유자가 없는 세션은 엔진이 강제 종료되며 커널에 남은 것이다. 같은 이름으로 다시 열기 전에 멈춘다.
    const ULONG stopped = stopSessionByName();

    TRACEHANDLE session = 0;
    const ULONG started = ::StartTraceW(&session, kSessionName, asProperties(collector->properties_));
    if (started != ERROR_SUCCESS) {
        if (started == ERROR_ACCESS_DENIED) {
            error = "ETW network events need administrator rights";
        } else if (started == ERROR_ALREADY_EXISTS && stopped == ERROR_ACCESS_DENIED) {
            error = "a trace session left by an earlier run is still active; "
                    "run once as administrator to clear it";
        } else {
            error = "StartTrace failed with error " + std::to_string(started);
        }
        return nullptr;
    }
    collector->session_ = session;

    const ULONG enabled = ::EnableTraceEx2(session, &kNetworkProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                           TRACE_LEVEL_INFORMATION, kKeywords, 0, 0, nullptr);
    if (enabled != ERROR_SUCCESS) {
        error = "EnableTraceEx2 failed with error " + std::to_string(enabled);
        return nullptr;  // 소멸자가 세션을 멈춘다
    }

    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = const_cast<LPWSTR>(kSessionName);
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = onEventRecord;
    logfile.Context = collector.get();
    const TRACEHANDLE consumer = ::OpenTraceW(&logfile);
    if (consumer == INVALID_PROCESSTRACE_HANDLE) {
        error = "OpenTrace failed with error " + std::to_string(::GetLastError());
        return nullptr;  // 소멸자가 세션을 멈춘다
    }
    collector->consumer_ = consumer;

    collector->running_ = true;
    EtwNetworkCollector* self = collector.get();
    collector->thread_ = std::thread([self] {
        TRACEHANDLE handle = self->consumer_;
        // 세션이 멈추거나 CloseTrace 가 불릴 때까지 돌아오지 않는다.
        const ULONG result = ::ProcessTrace(&handle, 1, nullptr, nullptr);
        self->running_ = false;
        if (!self->stopping_) {
            std::fprintf(stderr, "network traffic: ETW stopped unexpectedly (error %lu), rates are not measured\n",
                         static_cast<unsigned long>(result));
        }
    });
    return collector;
}

EtwNetworkCollector::~EtwNetworkCollector() {
    stopping_ = true;
    if (session_ != 0) {
        ::ControlTraceW(session_, nullptr, asProperties(properties_), EVENT_TRACE_CONTROL_STOP);
    }
    if (consumer_ != 0) {
        ::CloseTrace(consumer_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (owner_mutex_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(owner_mutex_));
    }
}

void EtwNetworkCollector::onEvent(uint16_t event_id, const unsigned char* data, size_t length) {
    const std::optional<NetworkEvent> event = parseNetworkEvent(event_id, data, length);
    if (!event.has_value()) {
        return;
    }
    const FlowKey key = makeFlowKey(event->protocol, event->pid, event->saddr, event->sport, event->daddr, event->dport);
    std::lock_guard<std::mutex> lock(mutex_);
    RawFlowTraffic& flow = flows_[key];
    flow.key = key;
    if (event->sent) {
        flow.bytes_sent += event->size;
    } else {
        flow.bytes_received += event->size;
    }
}

std::optional<RawNetworkTraffic> EtwNetworkCollector::drain() {
    if (!running_) {
        return std::nullopt;
    }
    warnIfEventsLost();

    // 이벤트는 버퍼가 차거나 1 초 타이머가 돌 때만 전달된다. 그대로 두면 창마다 0 개 또는 2 개의 플러시 묶음이
    // 들어 속도가 들쭉날쭉하다 (리뷰). 창을 닫기 전에 강제로 흘려보내 지금까지의 이벤트가 이 창에 들어오게 한다.
    {
        std::vector<unsigned char> props = makeProperties();
        const ULONG flushed = ::ControlTraceW(session_, nullptr, asProperties(props), EVENT_TRACE_CONTROL_FLUSH);
        if (flushed != ERROR_SUCCESS && !flush_warned_) {
            flush_warned_ = true;
            std::fprintf(stderr, "network traffic: ETW flush failed with error %lu, rates may lag by up to a second\n",
                         static_cast<unsigned long>(flushed));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kFlushSettleMs));
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    const std::optional<std::chrono::steady_clock::time_point> previous = window_start_;
    window_start_ = now;
    std::map<FlowKey, RawFlowTraffic> flows;
    flows.swap(flows_);
    if (!previous.has_value()) {
        return std::nullopt;  // 첫 호출: 창의 시작 시점일 뿐이다.
    }

    RawNetworkTraffic traffic;
    traffic.window_seconds = std::chrono::duration<double>(now - *previous).count();
    traffic.flows.reserve(flows.size());
    for (auto& entry : flows) {
        traffic.flows.push_back(std::move(entry.second));
    }
    return traffic;
}

void EtwNetworkCollector::warnIfEventsLost() {
    std::vector<unsigned char> query = makeProperties();
    if (::ControlTraceW(session_, nullptr, asProperties(query), EVENT_TRACE_CONTROL_QUERY) != ERROR_SUCCESS) {
        return;
    }
    // EventsLost 는 커널이 버퍼를 못 채워 잃은 이벤트, RealTimeBuffersLost 는 소비자가 늦어 잃은 버퍼다.
    const unsigned long events = asProperties(query)->EventsLost;
    const unsigned long buffers = asProperties(query)->RealTimeBuffersLost;
    if (events <= events_lost_ && buffers <= buffers_lost_) {
        return;
    }
    const int64_t now = steadyMs();
    if (last_warning_ms_ != 0 && now - last_warning_ms_ < kLostWarningIntervalMs) {
        return;
    }
    std::fprintf(stderr, "network traffic: ETW dropped %lu events and %lu buffers so far\n", events, buffers);
    events_lost_ = events;
    buffers_lost_ = buffers;
    last_warning_ms_ = now;
}

}  // namespace pulse
