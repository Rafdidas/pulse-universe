#pragma once

#include <memory>
#include <string>

#include "platform/ISystemReader.h"
#include "platform/windows/EtwSchedulerCollector.h"

namespace pulse {

// Toolhelp32 로 프로세스를 열거하고, PSAPI 로 메모리를, PDH 로 코어별 부하를 읽는다.
// PDH 카운터는 두 번째 수집부터 값이 나오므로 생성자에서 한 번 수집해 둔다.
// measure_threads 가 참이면 ETW 수집기를 띄워 표본마다 실측 스레드-코어 매핑을
// 싣는다 (ETW 스펙 7절). 띄우지 못하면 이유를 mappingError() 에 남기고 추정으로 간다.
class WindowsSystemReader final : public ISystemReader {
public:
    explicit WindowsSystemReader(bool measure_threads = false);
    ~WindowsSystemReader() override;

    WindowsSystemReader(const WindowsSystemReader&) = delete;
    WindowsSystemReader& operator=(const WindowsSystemReader&) = delete;

    RawSample read() override;
    unsigned coreCount() const override;
    HostInfo hostInfo() const override;

    // ETW 수집기가 돌고 있는가.
    bool measuringThreads() const;
    // 수집기를 띄우지 못한 이유. 띄웠거나 시도하지 않았으면 빈 문자열.
    const std::string& mappingError() const;

private:
    void* query_ = nullptr;    // PDH_HQUERY
    void* counter_ = nullptr;  // PDH_HCOUNTER
    unsigned core_count_ = 0;
    std::unique_ptr<EtwSchedulerCollector> collector_;
    std::string mapping_error_;
};

}  // namespace pulse
