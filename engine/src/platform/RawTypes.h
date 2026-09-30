#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Snapshot.h"

namespace pulse {

struct RawProcess {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string name;
    uint64_t cpu_cumulative_ms = 0;
    uint64_t mem_bytes = 0;
    uint32_t thread_count = 0;
    uint64_t start_time_ms = 0;
    Account account = Account::System;
    std::string image_path;
};

struct RawCore {
    uint32_t id = 0;
    double pct = 0.0;
};

struct RawMemory {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

struct HostInfo {
    std::string os;
    bool elevated = false;
    // hello 의 capabilities.thread_mapping. 엔진이 시작할 때의 실측 수집기 상태다
    // (ETW 스펙 8절). 수집기가 없으면 "estimated".
    std::string thread_mapping = "estimated";
};

// 한 창 동안 한 프로세스가 한 코어에서 돈 시간.
struct RawRunTime {
    uint32_t pid = 0;
    uint32_t core = 0;
    double seconds = 0.0;
};

// 실측 스레드-코어 매핑 한 창 (ETW 스펙 4절). 창 길이는 이벤트 시각 기준이다.
struct RawThreadMapping {
    double window_seconds = 0.0;
    std::vector<RawRunTime> run_times;
};

struct RawSample {
    std::vector<RawProcess> processes;
    std::vector<RawCore> cores;
    RawMemory memory;
    uint64_t timestamp_ms = 0;
    // 실측 수집기가 살아 있으면 채운다. 없으면 흐름을 추정한다 (계약서 6.2절).
    std::optional<RawThreadMapping> thread_mapping;
};

}  // namespace pulse
