#include "platform/windows/WindowsSystemReader.h"

#include <windows.h>
// windows.h 가 먼저 와야 한다.
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <cctype>
#include <chrono>
#include <string>
#include <vector>

namespace pulse {
namespace {

// FILETIME 은 100ns 단위다.
uint64_t fileTimeTo100ns(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

// Windows epoch(1601-01-01) 과 Unix epoch(1970-01-01) 의 차이, 100ns 단위.
constexpr uint64_t kUnixEpochOffset100ns = 116444736000000000ull;

uint64_t fileTimeToUnixMs(const FILETIME& ft) {
    const uint64_t raw = fileTimeTo100ns(ft);
    if (raw <= kUnixEpochOffset100ns) {
        return 0;
    }
    return (raw - kUnixEpochOffset100ns) / 10000ull;
}

uint64_t nowUnixMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string toUtf8(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

bool isAllDigits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

unsigned logicalCoreCount() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwNumberOfProcessors == 0 ? 1u : info.dwNumberOfProcessors;
}

// 프로세스 핸들을 열어 얻을 수 있는 것만 채운다.
// 권한이 부족하거나 보호된 프로세스면 조용히 건너뛴다.
void enrichFromHandle(RawProcess& process) {
    const HANDLE handle = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, process.pid);
    if (handle == nullptr) {
        return;
    }

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (::GetProcessTimes(handle, &creation, &exit, &kernel, &user) != 0) {
        process.start_time_ms = fileTimeToUnixMs(creation);
        const uint64_t busy_100ns = fileTimeTo100ns(kernel) + fileTimeTo100ns(user);
        process.cpu_cumulative_ms = busy_100ns / 10000ull;
    }

    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(handle, &counters, sizeof(counters)) != 0) {
        process.mem_bytes = counters.WorkingSetSize;
    }

    wchar_t path[MAX_PATH] = {};
    DWORD path_len = MAX_PATH;
    if (::QueryFullProcessImageNameW(handle, 0, path, &path_len) != 0) {
        process.image_path = toUtf8(path);
    }

    ::CloseHandle(handle);
}

// 세션 0 은 서비스와 시스템 프로세스 전용이다. 사용자가 띄운 것은 세션 1 이상이다.
Account accountForPid(uint32_t pid) {
    DWORD session_id = 0;
    if (::ProcessIdToSessionId(pid, &session_id) == 0) {
        return Account::System;
    }
    return session_id == 0 ? Account::System : Account::User;
}

}  // namespace

WindowsSystemReader::WindowsSystemReader() : core_count_(logicalCoreCount()) {
    PDH_HQUERY query = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return;
    }
    query_ = query;

    PDH_HCOUNTER counter = nullptr;
    // English 카운터 이름을 쓰면 OS 표시 언어와 무관하게 동작한다.
    if (::PdhAddEnglishCounterW(query, L"\\Processor(*)\\% Processor Time", 0,
                                &counter) != ERROR_SUCCESS) {
        ::PdhCloseQuery(query);
        query_ = nullptr;
        return;
    }
    counter_ = counter;

    // PDH 는 두 번째 수집부터 값을 낸다. 첫 수집을 여기서 해 둔다.
    ::PdhCollectQueryData(query);
}

WindowsSystemReader::~WindowsSystemReader() {
    if (query_ != nullptr) {
        ::PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
    }
}

unsigned WindowsSystemReader::coreCount() const {
    return core_count_;
}

RawSample WindowsSystemReader::read() {
    RawSample sample;
    sample.timestamp_ms = nowUnixMs();

    // --- 프로세스 열거 ---
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry) != 0) {
            do {
                // pid 0 은 System Idle Process 다. 실제 프로세스가 아니므로 건너뛴다.
                if (entry.th32ProcessID == 0) {
                    continue;
                }
                RawProcess process;
                process.pid = entry.th32ProcessID;
                process.ppid = entry.th32ParentProcessID;
                process.name = toUtf8(entry.szExeFile);
                process.thread_count = entry.cntThreads;
                process.account = accountForPid(process.pid);
                enrichFromHandle(process);
                sample.processes.push_back(std::move(process));
            } while (::Process32NextW(snapshot, &entry) != 0);
        }
        ::CloseHandle(snapshot);
    }

    // --- 코어별 부하 ---
    sample.cores.reserve(core_count_);
    bool cores_filled = false;
    if (query_ != nullptr && counter_ != nullptr) {
        const auto query = static_cast<PDH_HQUERY>(query_);
        const auto counter = static_cast<PDH_HCOUNTER>(counter_);
        if (::PdhCollectQueryData(query) == ERROR_SUCCESS) {
            DWORD buffer_size = 0;
            DWORD item_count = 0;
            PDH_STATUS status = ::PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
            if (status == PDH_MORE_DATA && buffer_size > 0) {
                std::vector<unsigned char> buffer(buffer_size);
                auto* items =
                    reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
                if (::PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size,
                                                   &item_count, items) == ERROR_SUCCESS) {
                    for (DWORD i = 0; i < item_count; ++i) {
                        const std::string name = toUtf8(items[i].szName);
                        // "_Total" 은 합계 항목이라 코어가 아니다.
                        // 논리 프로세서가 64개를 넘는 장비에서는 "0,3" 같은
                        // 프로세서 그룹 표기가 섞여 들어오므로 숫자만인 것만 받는다.
                        if (!isAllDigits(name)) {
                            continue;
                        }
                        RawCore core;
                        core.id = static_cast<uint32_t>(std::stoul(name));
                        double pct = items[i].FmtValue.doubleValue;
                        if (pct < 0.0) pct = 0.0;
                        if (pct > 100.0) pct = 100.0;
                        core.pct = pct;
                        sample.cores.push_back(core);
                    }
                    cores_filled = !sample.cores.empty();
                }
            }
        }
    }
    if (!cores_filled) {
        // PDH 가 아직 값을 내지 않았다. 0 으로 채워 코어 수 불변식을 지킨다.
        sample.cores.clear();
        for (unsigned i = 0; i < core_count_; ++i) {
            sample.cores.push_back(RawCore{i, 0.0});
        }
    }

    // --- 메모리 ---
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory) != 0) {
        sample.memory.total_bytes = memory.ullTotalPhys;
        sample.memory.used_bytes = memory.ullTotalPhys - memory.ullAvailPhys;
    }

    return sample;
}

}  // namespace pulse
