#include "core/DataAggregator.h"

#include <unordered_map>

namespace pulse {
namespace {

constexpr double kBytesPerMb = 1024.0 * 1024.0;

double toMb(uint64_t bytes) {
    return static_cast<double>(bytes) / kBytesPerMb;
}

}  // namespace

DataAggregator::DataAggregator(unsigned core_count, AggregatorConfig cfg)
    : cpu_delta_(core_count),
      filter_(cfg.filter),
      flow_estimator_(cfg.flow) {}

SystemSnapshot DataAggregator::aggregate(const RawSample& sample) {
    SystemSnapshot snapshot;
    snapshot.seq = ++seq_;
    snapshot.t = sample.timestamp_ms;

    // 1. pid 별 순간 CPU 사용률. 이 단계는 필터 전 전체를 대상으로 한다.
    std::unordered_map<uint32_t, std::optional<double>> cpu_by_pid;
    cpu_by_pid.reserve(sample.processes.size());
    for (const RawProcess& p : sample.processes) {
        // 한 표본에 같은 pid 가 두 번 들어오면 두 번째 update 는 방금 저장한
        // 표본과 비교해 nullopt 를 돌려주고, 유효한 값을 덮어쓴다. 첫 항목만 쓴다.
        if (cpu_by_pid.find(p.pid) != cpu_by_pid.end()) {
            continue;
        }
        cpu_by_pid[p.pid] =
            cpu_delta_.update(p.pid, p.cpu_cumulative_ms, sample.timestamp_ms);
    }

    // 2. 생명주기도 필터 전 전체를 대상으로 한다 (스펙 4.6).
    snapshot.lifecycle = lifecycle_.update(sample.processes);
    for (const uint32_t pid : snapshot.lifecycle.terminated) {
        cpu_delta_.forget(pid);
    }

    // 3. 전체 합계.
    snapshot.system.mem_used_mb = toMb(sample.memory.used_bytes);
    snapshot.system.mem_total_mb = toMb(sample.memory.total_bytes);
    snapshot.system.process_total = static_cast<uint32_t>(sample.processes.size());
    for (const RawProcess& p : sample.processes) {
        snapshot.system.thread_total += p.thread_count;
    }

    // 4. 코어 부하와 시스템 평균.
    double core_pct_sum = 0.0;
    snapshot.cores.reserve(sample.cores.size());
    for (const RawCore& c : sample.cores) {
        snapshot.cores.push_back(CoreLoad{c.id, c.pct});
        core_pct_sum += c.pct;
    }
    // system.cpu_pct 는 sample.cores(PDH)의 평균이다. PDH 는 리더 생성자에서
    // 이미 첫 수집을 해 두므로 첫 주기부터 유효한 값이 있다 — 델타 기반 값과
    // 달리 seq_ 에 의존하지 않는다. cores 가 비어 있을 때만 값을 비운다.
    if (!sample.cores.empty()) {
        snapshot.system.cpu_pct = core_pct_sum / static_cast<double>(sample.cores.size());
    }

    // 5. 그룹화 후 그룹별 CPU 를 구성원 합으로 채운다.
    GroupingResult grouped = group_builder_.build(sample.processes);
    snapshot.ambient = grouped.ambient;

    for (ProcessGroup& g : grouped.groups) {
        double sum = 0.0;
        bool any = false;

        const auto accumulate = [&](uint32_t pid) {
            const auto it = cpu_by_pid.find(pid);
            if (it != cpu_by_pid.end() && it->second.has_value()) {
                sum += *it->second;
                any = true;
            }
        };

        accumulate(g.root_pid);
        for (ChildProcess& child : g.children) {
            const auto it = cpu_by_pid.find(child.pid);
            if (it != cpu_by_pid.end()) {
                child.cpu_pct = it->second;
            }
            accumulate(child.pid);
        }

        // 그룹 CPU 는 값을 가진 구성원들의 합이다. 값을 가진 구성원이 하나도
        // 없을 때만 비운다. 자식이 막 생겨나 아직 두 번째 표본을 못 받은
        // 주기에는 부분합이 나오는데, 이는 의도된 하한값이다 — 매번 자식이
        // 하나 늘 때마다 그룹 전체를 비우면 화면이 계속 깜빡인다.
        if (any) {
            g.cpu_pct = sum;
        }
    }

    // 5b. lifecycle.spawned[] 의 group 필드를 채운다. 필터 전 grouped.groups
    // 기준으로 pid -> group key 조회 테이블을 만든다: 제외된 svchost 트리에
    // 속한 프로세스라도 그 그룹 key 자체는 유효한 정보이기 때문이다.
    // LifecycleTracker 는 그룹을 모르므로 그 값은 항상 빈 문자열이다.
    if (!snapshot.lifecycle.spawned.empty()) {
        std::unordered_map<uint32_t, std::string> group_key_by_pid;
        for (const ProcessGroup& g : grouped.groups) {
            group_key_by_pid.emplace(g.root_pid, g.key);
            for (const ChildProcess& child : g.children) {
                group_key_by_pid.emplace(child.pid, g.key);
            }
        }
        for (SpawnedProcess& spawned : snapshot.lifecycle.spawned) {
            const auto it = group_key_by_pid.find(spawned.pid);
            if (it != group_key_by_pid.end()) {
                spawned.group = it->second;
            }
        }
    }

    // 6. 상위 N개 선택. 합계와 생명주기는 이미 전체 기준으로 계산됐다.
    snapshot.groups = filter_.select(std::move(grouped.groups),
                                     snapshot.system.mem_total_mb);

    // 7. 화면에 남은 그룹에 대해서만 흐름을 추정한다.
    snapshot.flows = flow_estimator_.estimate(snapshot.groups, snapshot.cores);

    return snapshot;
}

}  // namespace pulse
