#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

namespace {

// read() 마다 정해진 시간만큼 걸리는 리더. 목록이 끝나면 마지막 지연을 반복한다.
class SlowSystemReader final : public ISystemReader {
public:
    explicit SlowSystemReader(std::vector<unsigned> delays_ms)
        : delays_ms_(std::move(delays_ms)) {}

    RawSample read() override {
        starts_.push_back(std::chrono::steady_clock::now());
        const std::size_t index = std::min(next_++, delays_ms_.size() - 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(delays_ms_[index]));
        return makeSample(1000 * (next_ + 1));
    }

    unsigned coreCount() const override { return 4; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

    // read() 가 불린 시각. 루프가 보장하는 것은 샘플을 "시작하는" 간격이다.
    const std::vector<std::chrono::steady_clock::time_point>& starts() const { return starts_; }

private:
    std::vector<unsigned> delays_ms_;
    std::size_t next_ = 0;
    std::vector<std::chrono::steady_clock::time_point> starts_;
};

using Clock = std::chrono::steady_clock;

// 각 핸들러 호출 사이의 간격(ms).
std::vector<double> gapsBetweenCalls(ISystemReader& reader, unsigned interval_ms,
                                     unsigned iterations) {
    EngineLoopConfig cfg;
    cfg.interval_ms = interval_ms;
    cfg.iterations = iterations;

    std::vector<Clock::time_point> calls;
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { calls.push_back(Clock::now()); });
    loop.run();

    std::vector<double> gaps;
    for (std::size_t i = 1; i < calls.size(); ++i) {
        gaps.push_back(std::chrono::duration<double, std::milli>(calls[i] - calls[i - 1]).count());
    }
    return gaps;
}

}  // namespace

// 루프가 보장하는 것은 샘플을 "시작하는" 간격이다. 핸들러 호출 시각은 샘플링이
// 끝난 뒤라 read() 소요 시간의 흔들림(sleep_for 30 ms 가 Windows 타이머 해상도
// 때문에 30~46 ms)이 섞인다 — 첫 샘플이 길고 마지막이 짧으면 합이 200 ms 아래로
// 내려간다(부하 중 실측 189.8 ms). 그래서 read() 가 불린 시각을 잰다. 시작은 마감
// 시각보다 빠를 수 없으므로 4 주기의 합은 200 ms 이상이다.
TEST_CASE("the loop starts samples on a fixed period regardless of how long sampling takes",
          "[loop][timing]") {
    SlowSystemReader reader({30});
    EngineLoopConfig cfg;
    cfg.interval_ms = 50;
    cfg.iterations = 5;
    EngineLoop loop(reader, cfg, [](const SystemSnapshot&) {});
    loop.run();

    const auto& starts = reader.starts();
    REQUIRE(starts.size() == 5);
    const double total =
        std::chrono::duration<double, std::milli>(starts.back() - starts.front()).count();
    // 옛 동작(샘플링 뒤 주기만큼 쉼)이면 시작 간격이 30 + 50 = 80 ms 이상, 합이 320 ms 이상이다.
    // 첫 read() 는 스케줄러 선점(Windows 시간 조각 약 15.6 ms)으로 루프의 기준점보다 늦게
    // 시작할 수 있어, 하한에 그만큼의 여유를 둔다. 옛 동작이면 어차피 320 ms 이상이다.
    REQUIRE(total >= 180.0);
    REQUIRE(total < 280.0);
}

TEST_CASE("a late sample is followed immediately, then the period resumes from now",
          "[loop][timing]") {
    // 첫 샘플만 100 ms (주기 50 ms 를 넘김), 나머지는 즉시.
    // 기준을 다시 잡으면: 2번은 곧바로, 3번은 그로부터 50 ms 뒤.
    // 밀린 주기를 따라잡으면: 2·3번이 연달아 곧바로 온다.
    SlowSystemReader reader({100, 0});
    const auto gaps = gapsBetweenCalls(reader, 50, 4);

    REQUIRE(gaps.size() == 3);
    REQUIRE(gaps[0] < 30.0);   // 1 → 2: 쉬지 않았다
    REQUIRE(gaps[1] >= 35.0);  // 2 → 3: 몰아서 따라잡지 않았다
}

TEST_CASE("stop interrupts a long sleep promptly", "[loop][timing]") {
    FakeSystemReader reader({makeSample(1000)}, 4);
    EngineLoopConfig cfg;
    cfg.interval_ms = 10000;
    cfg.iterations = 0;

    std::atomic<int> calls{0};
    EngineLoop loop(reader, cfg, [&](const SystemSnapshot&) { ++calls; });

    const auto started = Clock::now();
    std::jthread worker([&] { loop.run(); });
    while (calls.load() < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    loop.stop();
    worker.join();

    const double elapsed =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    REQUIRE(elapsed < 500.0);
}
