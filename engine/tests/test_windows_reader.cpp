#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "platform/windows/WindowsSystemReader.h"

#include <windows.h>

using namespace pulse;

TEST_CASE("the reader returns a plausible process list", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(sample.processes.size() > 10);
    REQUIRE(sample.timestamp_ms > 0);

    for (const RawProcess& p : sample.processes) {
        REQUIRE(p.pid != 0);
        REQUIRE_FALSE(p.name.empty());
    }
}

TEST_CASE("the reader finds this very test process", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();
    const auto self = static_cast<uint32_t>(::GetCurrentProcessId());

    const auto it = std::find_if(sample.processes.begin(), sample.processes.end(),
                                 [&](const RawProcess& p) { return p.pid == self; });

    REQUIRE(it != sample.processes.end());
    REQUIRE(it->name == "pulse-tests.exe");
    REQUIRE(it->mem_bytes > 0);
    REQUIRE(it->thread_count > 0);
    REQUIRE(it->start_time_ms > 0);
    REQUIRE(it->account == Account::User);
    REQUIRE_FALSE(it->image_path.empty());
}

TEST_CASE("the reader reports one entry per logical core", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(reader.coreCount() > 0);
    REQUIRE(sample.cores.size() == reader.coreCount());
    for (const RawCore& c : sample.cores) {
        REQUIRE(c.pct >= 0.0);
        REQUIRE(c.pct <= 100.0);
    }
}

TEST_CASE("the reader reports plausible memory totals", "[reader][integration]") {
    WindowsSystemReader reader;

    const RawSample sample = reader.read();

    REQUIRE(sample.memory.total_bytes > 1024ull * 1024ull * 1024ull);
    REQUIRE(sample.memory.used_bytes > 0);
    REQUIRE(sample.memory.used_bytes < sample.memory.total_bytes);
}

TEST_CASE("cpu time never goes backwards between reads", "[reader][integration]") {
    WindowsSystemReader reader;
    const auto self = static_cast<uint32_t>(::GetCurrentProcessId());

    const auto findSelf = [&](const RawSample& s) {
        return std::find_if(s.processes.begin(), s.processes.end(),
                            [&](const RawProcess& p) { return p.pid == self; });
    };

    const RawSample first = reader.read();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const RawSample second = reader.read();

    const auto a = findSelf(first);
    const auto b = findSelf(second);
    REQUIRE(a != first.processes.end());
    REQUIRE(b != second.processes.end());
    REQUIRE(b->cpu_cumulative_ms >= a->cpu_cumulative_ms);
    REQUIRE(second.timestamp_ms > first.timestamp_ms);
}

