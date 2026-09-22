#pragma once

#include <cstdint>
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

struct RawSample {
    std::vector<RawProcess> processes;
    std::vector<RawCore> cores;
    RawMemory memory;
    uint64_t timestamp_ms = 0;
};

}  // namespace pulse
