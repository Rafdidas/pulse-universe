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
    // FlowEstimator 는 "estimated", 실측 매핑(measuredFlows)은 "measured" 를 단다.
    // network/Serializer.h 의 HelloInfo::thread_mapping 이 프로토콜 계층에서
    // 연결 시점의 능력을 나른다 — 수집기가 살아 있는 동안 두 값은 같다 (ETW 스펙 8절).
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

// M12 스펙 2절. 스냅샷의 network 블록. 속도는 바이트/초이고, 측정하지 못했으면 값이 없다 (직렬화하면 null).
struct NetworkEndpointOut {
    std::string ip;
    bool is_private = false;
    std::vector<uint16_t> ports;       // 오름차순, 중복 없음
    uint32_t connections = 0;          // 포함된 연결 수
    std::vector<std::string> groups;   // 연결된 그룹 key, 오름차순, 중복 없음
    std::optional<double> down_bps;
    std::optional<double> up_bps;
};

struct NetworkConnectionOut {
    std::string group;  // ProcessGroup::key
    uint32_t pid = 0;
    std::string proto;  // "tcp" 또는 "udp"
    uint16_t local_port = 0;
    std::string remote;  // IP
    uint16_t remote_port = 0;
    std::string state;  // "established", "syn_sent" ... UDP 는 "none"
    std::optional<double> down_bps;
    std::optional<double> up_bps;
};

struct NetworkSummaryOut {
    // 모든 프로세스 기준이다 (그룹에 속하지 않는 것 포함).
    uint32_t connections = 0;
    uint32_t established = 0;
    uint32_t endpoints = 0;  // 상한으로 잘리기 전의 총수
    uint32_t udp_sockets = 0;
    std::optional<double> down_bps;
    std::optional<double> up_bps;
};

struct NetworkSnapshot {
    // "measured": 트래픽 수집기가 있다. "unavailable": 없다 (속도는 모두 null).
    std::string traffic = "unavailable";
    NetworkSummaryOut summary;
    std::vector<NetworkEndpointOut> endpoints;
    std::vector<NetworkConnectionOut> connections;
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
    NetworkSnapshot network;
};

}  // namespace pulse
