#include "app/EngineLoop.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

namespace pulse {
namespace {

// stop() 이 최대 이만큼만 기다리면 반응한다.
constexpr unsigned kSleepSliceMs = 20;

}  // namespace

EngineLoop::EngineLoop(ISystemReader& reader, EngineLoopConfig cfg, SnapshotHandler handler)
    : reader_(reader),
      cfg_(std::move(cfg)),
      handler_(std::move(handler)),
      aggregator_(reader.coreCount(), cfg_.aggregator) {}

void EngineLoop::run() {
    try {
        for (unsigned n = 0; cfg_.iterations == 0 || n < cfg_.iterations; ++n) {
            if (stop_requested_.load()) {
                return;
            }

            const SystemSnapshot snapshot = aggregator_.aggregate(reader_.read());
            handler_(snapshot);

            const bool is_last = cfg_.iterations != 0 && n + 1 == cfg_.iterations;
            if (is_last) {
                return;
            }
            sleepInterval();
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

void EngineLoop::sleepInterval() {
    unsigned remaining = cfg_.interval_ms;
    while (remaining > 0 && !stop_requested_.load()) {
        const unsigned slice = std::min(remaining, kSleepSliceMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
}

}  // namespace pulse
