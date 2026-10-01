#include "app/EngineLoop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>
#include <unordered_map>
#include <utility>

#include "core/NetworkAggregator.h"
#include "core/NetworkSnapshot.h"

namespace pulse {
namespace {

// stop() 이 최대 이만큼만 기다리면 반응한다.
constexpr unsigned kSleepSliceMs = 20;

}  // namespace

EngineLoop::EngineLoop(ISystemReader& reader, EngineLoopConfig cfg, SnapshotHandler handler,
                       NetworkSources network)
    : reader_(reader),
      cfg_(std::move(cfg)),
      handler_(std::move(handler)),
      aggregator_(reader.coreCount(), cfg_.aggregator),
      network_(network) {}

void EngineLoop::run() {
    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::milliseconds(cfg_.interval_ms);

    try {
        // 다음 샘플을 시작할 시각. 샘플링에 걸린 시간을 주기에서 빼기 위해
        // "끝나고 interval 만큼 쉰다" 가 아니라 "시작 시각 + interval" 에 깨어난다.
        // 그래야 hello.interval_ms 가 실제 주기가 된다 (M5 스펙 4.2).
        auto next_start = Clock::now();
        for (unsigned n = 0; cfg_.iterations == 0 || n < cfg_.iterations; ++n) {
            if (stop_requested_.load()) {
                return;
            }

            // 트래픽 창을 먼저 닫는다 (강제 플러시로 잠깐 걸린다). 그래야 표본 시각 t 가 방송 시각에 가깝다.
            std::optional<RawNetworkTraffic> window;
            if (network_.traffic != nullptr) {
                window = network_.traffic->drain();
            }

            const RawSample sample = reader_.read();
            SystemSnapshot snapshot = aggregator_.aggregate(sample);

            if (network_.scanner != nullptr) {
                const ConnectionScan scan = network_.scanner->scan();
                // 같은 오류가 계속되면 한 번만 알린다 (초당 한 줄씩 쏟아내지 않는다).
                if (scan.error != last_scan_error_) {
                    last_scan_error_ = scan.error;
                    if (!scan.error.empty()) {
                        std::fprintf(stderr, "connections: %s\n", scan.error.c_str());
                    }
                }
                std::unordered_map<uint32_t, std::string> names;
                for (const RawProcess& process : sample.processes) {
                    names[process.pid] = process.name;
                }
                const NetworkView view = aggregateNetwork(scan.connections, names, window);
                snapshot.network = buildNetworkSnapshot(view, snapshot.groups, network_.traffic != nullptr);
            }
            handler_(snapshot);

            const bool is_last = cfg_.iterations != 0 && n + 1 == cfg_.iterations;
            if (is_last) {
                return;
            }

            next_start += interval;
            const auto now = Clock::now();
            if (next_start <= now) {
                // 샘플링이 주기보다 오래 걸렸다. 쉬지 않고 바로 다음 샘플을 뜨고
                // 기준을 지금으로 다시 잡는다 — 밀린 주기를 몰아서 따라잡지 않는다.
                next_start = now;
                continue;
            }
            sleepUntil(next_start);
        }
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void EngineLoop::stop() {
    stop_requested_.store(true);
}

const std::string& EngineLoop::error() const {
    return error_;
}

void EngineLoop::sleepUntil(std::chrono::steady_clock::time_point deadline) {
    using Clock = std::chrono::steady_clock;
    const auto slice = std::chrono::milliseconds(kSleepSliceMs);
    while (!stop_requested_.load()) {
        const auto now = Clock::now();
        if (now >= deadline) {
            return;
        }
        std::this_thread::sleep_for(std::min<Clock::duration>(deadline - now, slice));
    }
}

}  // namespace pulse
