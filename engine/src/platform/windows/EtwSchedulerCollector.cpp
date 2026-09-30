#include "platform/windows/EtwSchedulerCollector.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <evntcons.h>
#include <evntrace.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace pulse {
namespace {

// 커널 Thread 이벤트 공급자 (MOF 클래스 Thread_V2 ~ V5 와 CSwitch 가 이 GUID 로 온다).
constexpr GUID kThreadProvider = {
    0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};

// 세션 식별용. 시스템 로거 모드 세션은 SystemTraceControlGuid 가 아니라 자기 GUID 를 쓴다.
constexpr GUID kSessionGuid = {
    0x6a3f1c52, 0x8e0b, 0x4d47, {0x9a, 0x61, 0x2f, 0x5b, 0x0c, 0x7e, 0x94, 0xd3}};

constexpr UCHAR kOpcodeThreadStart = 1;
constexpr UCHAR kOpcodeThreadEnd = 2;
constexpr UCHAR kOpcodeThreadDcStart = 3;
constexpr UCHAR kOpcodeContextSwitch = 36;

// 측정(ETW 스펙 2절): 부하 중 초당 약 15만 건(약 7 MB). 커널은 코어마다 버퍼를 따로
// 잡으므로 상주 메모리는 대략 코어 수 × 버퍼 크기 × 2(세션과 소비자)다. 1 MB 버퍼에서는
// 28 코어 기계의 작업 집합이 +59 MB 였다. 코어 하나가 초당 쓰는 양은 수백 KB 라 256 KB 로
// 충분하다.
constexpr ULONG kBufferKb = 256;
constexpr ULONG kMinBuffers = 16;
constexpr ULONG kMaxBuffers = 128;
// 이벤트가 적을 때도 1초 안에 전달되게 한다.
constexpr ULONG kFlushSeconds = 1;

// ProcessTrace 는 PROCESS_TRACE_MODE_RAW_TIMESTAMP 없이 열면 시각을 FILETIME(100ns)
// 으로 바꿔 준다.
constexpr double kTicksPerSecond = 1e7;

// 유실 경고를 이보다 자주 찍지 않는다.
constexpr int64_t kLostWarningIntervalMs = 10000;

// EVENT_TRACE_PROPERTIES 뒤에 세션 이름을 붙인 버퍼. StartTrace·ControlTrace 가
// 이 모양을 요구한다.
std::vector<unsigned char> makeProperties() {
    const size_t name_bytes =
        (std::wcslen(EtwSchedulerCollector::kSessionName) + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(EVENT_TRACE_PROPERTIES) + name_bytes, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    props->Wnode.BufferSize = static_cast<ULONG>(buffer.size());
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;  // QPC
    props->Wnode.Guid = kSessionGuid;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    props->EnableFlags =
        EVENT_TRACE_FLAG_PROCESS | EVENT_TRACE_FLAG_THREAD | EVENT_TRACE_FLAG_CSWITCH;
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint32_t readU32(const unsigned char* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// ProcessTrace 스레드에서 불린다. Thread 공급자의 Start·DCStart·End·CSwitch 만 본다.
// 페이로드: Thread_V* 는 ProcessId, TThreadId 로 시작하고, CSwitch 는 NewThreadId 로 시작한다.
void WINAPI onEventRecord(PEVENT_RECORD record) {
    auto* collector = static_cast<EtwSchedulerCollector*>(record->UserContext);
    const EVENT_HEADER& header = record->EventHeader;
    if (collector == nullptr || !IsEqualGUID(header.ProviderId, kThreadProvider)) {
        return;
    }
    const auto* data = static_cast<const unsigned char*>(record->UserData);
    const USHORT length = record->UserDataLength;

    switch (header.EventDescriptor.Opcode) {
        case kOpcodeThreadStart:
        case kOpcodeThreadDcStart:
            if (length >= 8) {
                collector->onThreadStarted(readU32(data, 0), readU32(data, 4));
            }
            break;
        case kOpcodeThreadEnd:
            if (length >= 8) {
                collector->onThreadEnded(readU32(data, 4));
            }
            break;
        case kOpcodeContextSwitch:
            if (length >= 4) {
                collector->onContextSwitch(GetEventProcessorIndex(record), readU32(data, 0),
                                           header.TimeStamp.QuadPart);
            }
            break;
        default:
            break;
    }
}

}  // namespace

EtwSchedulerCollector::EtwSchedulerCollector()
    : properties_(makeProperties()), table_(kTicksPerSecond) {}

std::unique_ptr<EtwSchedulerCollector> EtwSchedulerCollector::start(std::string& error) {
    std::unique_ptr<EtwSchedulerCollector> collector(new EtwSchedulerCollector());

    // 엔진이 강제 종료되면 세션이 커널에 남는다. 같은 이름으로 다시 열기 전에 멈춘다.
    std::vector<unsigned char> stale = makeProperties();
    ::ControlTraceW(0, kSessionName, asProperties(stale), EVENT_TRACE_CONTROL_STOP);

    TRACEHANDLE session = 0;
    const ULONG started =
        ::StartTraceW(&session, kSessionName, asProperties(collector->properties_));
    if (started != ERROR_SUCCESS) {
        error = started == ERROR_ACCESS_DENIED
                    ? "ETW kernel events need administrator rights"
                    : "StartTrace failed with error " + std::to_string(started);
        return nullptr;
    }
    collector->session_ = session;

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
    EtwSchedulerCollector* self = collector.get();
    collector->thread_ = std::thread([self] {
        TRACEHANDLE handle = self->consumer_;
        // 세션이 멈추거나 CloseTrace 가 불릴 때까지 돌아오지 않는다.
        ::ProcessTrace(&handle, 1, nullptr, nullptr);
        self->running_ = false;
    });
    return collector;
}

EtwSchedulerCollector::~EtwSchedulerCollector() {
    if (session_ != 0) {
        ::ControlTraceW(session_, nullptr, asProperties(properties_), EVENT_TRACE_CONTROL_STOP);
    }
    if (consumer_ != 0) {
        ::CloseTrace(consumer_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void EtwSchedulerCollector::onThreadStarted(uint32_t pid, uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.threadStarted(pid, tid);
}

void EtwSchedulerCollector::onThreadEnded(uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.threadEnded(tid);
}

void EtwSchedulerCollector::onContextSwitch(uint32_t core, uint32_t new_tid, int64_t ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    table_.contextSwitch(core, new_tid, ts);
}

std::optional<RawThreadMapping> EtwSchedulerCollector::drain() {
    if (!running_) {
        return std::nullopt;
    }
    warnIfEventsLost();
    std::lock_guard<std::mutex> lock(mutex_);
    return table_.drain();
}

void EtwSchedulerCollector::warnIfEventsLost() {
    std::vector<unsigned char> query = makeProperties();
    if (::ControlTraceW(session_, nullptr, asProperties(query), EVENT_TRACE_CONTROL_QUERY) !=
        ERROR_SUCCESS) {
        return;
    }
    const unsigned long lost = asProperties(query)->EventsLost;
    if (lost <= events_lost_) {
        return;
    }
    const int64_t now = steadyMs();
    if (last_warning_ms_ != 0 && now - last_warning_ms_ < kLostWarningIntervalMs) {
        return;
    }
    std::fprintf(stderr, "thread mapping: ETW dropped %lu events so far\n", lost);
    events_lost_ = lost;
    last_warning_ms_ = now;
}

}  // namespace pulse
