#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platform/RawTypes.h"

namespace pulse {

struct ChildProcess {
    uint32_t pid = 0;
    std::string name;
    // M1 에서는 항상 "child". 실제 role(renderer, gpu-process ...) 추출은
    // 프로세스 커맨드라인이 필요하므로 M5 로 미룬다.
    std::string role = "child";
    std::optional<double> cpu_pct;
    double mem_mb = 0.0;
    uint32_t threads = 0;
};

struct ProcessGroup {
    std::string key;  // "<name>:<root_pid>"
    std::string name;
    uint32_t root_pid = 0;
    std::optional<double> cpu_pct;
    double mem_mb = 0.0;
    uint32_t proc_count = 0;
    uint32_t thread_count = 0;
    uint64_t started_at = 0;
    Account account = Account::System;
    std::string image_path;
    std::vector<ChildProcess> children;
};

struct Ambient {
    uint32_t service_proc_count = 0;
    double service_mem_mb = 0.0;
};

}  // namespace pulse
