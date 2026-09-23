#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pulse {

enum class Account { User, System };

struct SpawnedProcess {
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string name;
    // 이 pid 가 속한 그룹의 key. GroupBuilder 결과에서 못 찾으면 빈 문자열로
    // 남는다 (제외된 svchost 트리 등). LifecycleTracker 는 그룹을 모르므로
    // 채우지 않는다 — DataAggregator 가 그룹핑 이후에 채운다.
    std::string group;
};

struct LifecycleDelta {
    std::vector<SpawnedProcess> spawned;
    std::vector<uint32_t> terminated;
};

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

struct CoreLoad {
    uint32_t id = 0;
    double pct = 0.0;
};

struct Flow {
    std::string group;  // ProcessGroup::key
    uint32_t core = 0;
    double weight = 0.0;
    // M1 은 추정만 한다. ETW 수집기가 들어오면 "measured" 가 된다.
    // network/Serializer.h 의 HelloInfo::thread_mapping 이 프로토콜 계층에서
    // 같은 값을 나른다 — 이걸 바꾸면 그쪽도 같이 바꿔야 한다.
    std::string source = "estimated";
};

struct Ambient {
    uint32_t service_proc_count = 0;
    double service_mem_mb = 0.0;
};

struct SystemTotals {
    std::optional<double> cpu_pct;
    double mem_used_mb = 0.0;
    double mem_total_mb = 0.0;
    uint32_t process_total = 0;
    uint32_t thread_total = 0;
};

struct SystemSnapshot {
    uint64_t seq = 0;
    uint64_t t = 0;
    SystemTotals system;
    std::vector<CoreLoad> cores;
    std::vector<ProcessGroup> groups;
    std::vector<Flow> flows;
    LifecycleDelta lifecycle;
    Ambient ambient;
};

}  // namespace pulse
