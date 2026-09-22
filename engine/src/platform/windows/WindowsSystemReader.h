#pragma once

#include "platform/ISystemReader.h"

namespace pulse {

// Toolhelp32 로 프로세스를 열거하고, PSAPI 로 메모리를, PDH 로 코어별 부하를 읽는다.
// PDH 카운터는 두 번째 수집부터 값이 나오므로 생성자에서 한 번 수집해 둔다.
class WindowsSystemReader final : public ISystemReader {
public:
    WindowsSystemReader();
    ~WindowsSystemReader() override;

    WindowsSystemReader(const WindowsSystemReader&) = delete;
    WindowsSystemReader& operator=(const WindowsSystemReader&) = delete;

    RawSample read() override;
    unsigned coreCount() const override;

private:
    void* query_ = nullptr;    // PDH_HQUERY
    void* counter_ = nullptr;  // PDH_HCOUNTER
    unsigned core_count_ = 0;
};

}  // namespace pulse
