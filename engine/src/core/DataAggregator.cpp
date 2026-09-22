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
    if (!sample.cores.empty() && seq_ > 1) {
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

        if (any) {
            g.cpu_pct = sum;
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
