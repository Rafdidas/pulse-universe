#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "app/EngineLoop.h"
#include "fakes/FakeSystemReader.h"

using namespace pulse;

namespace {

RawSample makeSample(uint64_t timestamp_ms) {
    RawSample s;
    s.timestamp_ms = timestamp_ms;
    s.cores = {RawCore{0, 25.0}};
    s.memory = RawMemory{1024ull * 1024 * 1024, 4096ull * 1024 * 1024};

    RawProcess p;
    p.pid = 100;
    p.ppid = 4;
    p.name = "app.exe";
    p.cpu_cumulative_ms = timestamp_ms / 10;
    p.mem_bytes = 64ull * 1024 * 1024;
    p.thread_count = 4;
    p.start_time_ms = 1000;
    p.account = Account::User;
    s.processes.push_back(p);
    return s;
}

EngineLoopConfig fastConfig(unsigned iterations) {
    EngineLoopConfig cfg;
    cfg.interval_ms = 1;
    cfg.iterations = iterations;
    return cfg;
}

}  // namespace

TEST_CASE("the loop calls the handler once per iteration", "[loop]") {
    FakeSystemReader reader({makeSample(1000), makeSample(2000), makeSample(3000)}, 4);
    int calls = 0;

    EngineLoop loop(reader, fastConfig(3), [&](const SystemSnapshot&) { ++calls; });
    loop.run();

    REQUIRE(calls == 3);
    REQUIRE(reader.readCount() == 3);
    REQUIRE(loop.error().empty());
}

TEST_CASE("sequence numbers advance across iterations", "[loop]") {
    FakeSystemReader reader({makeSample(1000), makeSample(2000)}, 4);
    std::vector<uint64_t> seqs;

    EngineLoop loop(reader, fastConfig(2),
                    [&](const SystemSnapshot& s) { seqs.push_back(s.seq); });
    loop.run();

    REQUIRE(seqs == std::vector<uint64_t>{1, 2});
}

TEST_CASE("stop ends a loop that would otherwise run forever", "[loop]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    std::atomic<int> calls{0};

    EngineLoopConfig cfg;
    cfg.interval_ms = 5;
    cfg.iterations = 0;  // 무한

    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { ++calls; });

    std::thread worker([&] { loop.run(); });
    while (calls.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    loop.stop();
    worker.join();

    REQUIRE(calls.load() >= 2);
    REQUIRE(loop.error().empty());
}

TEST_CASE("an exception from the reader stops the loop and is reported", "[loop]") {
    ThrowingSystemReader reader(2);
    int calls = 0;

    EngineLoop loop(reader, fastConfig(10), [&](const SystemSnapshot&) { ++calls; });
    loop.run();

    REQUIRE(calls == 2);
    REQUIRE(loop.error() == "reader exploded");
}

TEST_CASE("the aggregator config reaches the snapshot", "[loop]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    EngineLoopConfig cfg = fastConfig(1);
    cfg.aggregator.filter.max_groups = 0;

    std::size_t groups = 99;
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot& s) { groups = s.groups.size(); });
    loop.run();

    REQUIRE(groups == 0);
}
