#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#include "core/RunTimeTable.h"

using namespace pulse;
using Catch::Matchers::WithinAbs;

namespace {

// 1 틱 = 1 ms 로 두면 초 단위를 읽기 쉽다.
constexpr double kTicksPerSecond = 1000.0;

double secondsFor(const RawThreadMapping& mapping, uint32_t pid, uint32_t core) {
    const auto it = std::find_if(mapping.run_times.begin(), mapping.run_times.end(),
                                 [&](const RawRunTime& r) { return r.pid == pid && r.core == core; });
    return it == mapping.run_times.end() ? 0.0 : it->seconds;
}

}  // namespace

TEST_CASE("an empty table drains an empty zero-length window", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);

    const RawThreadMapping mapping = table.drain();

    REQUIRE(mapping.window_seconds == 0.0);
    REQUIRE(mapping.run_times.empty());
}

TEST_CASE("a slice is credited to the thread's process on the next switch", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);    // 코어 0: 스레드 1 (pid 100)
    table.contextSwitch(0, 2, 300);  // 300 ms 뒤 스레드 2 (pid 200)
    table.contextSwitch(0, 1, 500);  // 200 ms 뒤 다시 스레드 1

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(mapping.window_seconds, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 100, 0), WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 200, 0), WithinAbs(0.2, 1e-9));
}

TEST_CASE("cores are accounted separately", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(100, 2);
    table.threadStarted(300, 3);

    table.contextSwitch(0, 1, 0);
    table.contextSwitch(1, 2, 0);
    table.contextSwitch(0, 3, 400);
    table.contextSwitch(1, 3, 1000);

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(secondsFor(mapping, 100, 0), WithinAbs(0.4, 1e-9));
    REQUIRE_THAT(secondsFor(mapping, 100, 1), WithinAbs(1.0, 1e-9));
    // 코어 0 의 스레드 3 은 400 ms 부터 창 끝(1000 ms)까지 돌고 있었다.
    REQUIRE_THAT(secondsFor(mapping, 300, 0), WithinAbs(0.6, 1e-9));
}

TEST_CASE("a thread that never switches out is credited up to the drain and carried over",
          "[runtime]") {
    // 한 코어를 독점한 바쁜 루프에는 문맥 전환이 없다. 그래도 창마다 세어야 한다.
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);    // 코어 0 을 스레드 1 이 차지한다
    table.contextSwitch(1, 2, 1000); // 다른 코어의 이벤트가 시각을 1 초까지 밀어 준다

    const RawThreadMapping first = table.drain();
    REQUIRE_THAT(first.window_seconds, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(first, 100, 0), WithinAbs(1.0, 1e-9));

    table.contextSwitch(1, 2, 2000);
    const RawThreadMapping second = table.drain();
    REQUIRE_THAT(second.window_seconds, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(second, 100, 0), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(secondsFor(second, 200, 1), WithinAbs(1.0, 1e-9));
}

TEST_CASE("idle and unknown threads are not credited", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(0, 0);  // Idle 은 pid 0, tid 0 이다

    table.contextSwitch(0, 0, 0);
    table.contextSwitch(0, 77, 100);  // 모르는 스레드
    table.contextSwitch(0, 0, 300);

    const RawThreadMapping mapping = table.drain();

    REQUIRE_THAT(mapping.window_seconds, WithinAbs(0.3, 1e-9));
    REQUIRE(mapping.run_times.empty());
}

TEST_CASE("a thread learned after it was switched in is still credited", "[runtime]") {
    // 세션을 켠 직후에는 런다운(DCStart)보다 전환 이벤트가 먼저 올 수 있다.
    RunTimeTable table(kTicksPerSecond);

    table.contextSwitch(0, 1, 0);
    table.threadStarted(100, 1);
    table.contextSwitch(0, 0, 250);

    REQUIRE_THAT(secondsFor(table.drain(), 100, 0), WithinAbs(0.25, 1e-9));
}

TEST_CASE("a thread that ended while running is credited to the pid it had", "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);

    table.contextSwitch(0, 1, 0);
    table.threadEnded(1);  // 스레드는 자기 문맥에서 끝난 뒤 코어를 내놓는다
    table.contextSwitch(0, 0, 400);

    REQUIRE_THAT(secondsFor(table.drain(), 100, 0), WithinAbs(0.4, 1e-9));
}

TEST_CASE("an event older than the last drain does not reach back into the closed window",
          "[runtime]") {
    RunTimeTable table(kTicksPerSecond);
    table.threadStarted(100, 1);
    table.threadStarted(200, 2);

    table.contextSwitch(0, 1, 0);
    table.contextSwitch(1, 2, 1000);
    const RawThreadMapping first = table.drain();  // 창 [0, 1000]
    REQUIRE_THAT(secondsFor(first, 100, 0), WithinAbs(1.0, 1e-9));

    // 코어 0 의 전환이 900 ms 시각으로 늦게 도착한다. 이미 정산한 구간이다.
    table.contextSwitch(0, 2, 900);
    table.contextSwitch(0, 1, 1500);
    const RawThreadMapping second = table.drain();

    REQUIRE_THAT(second.window_seconds, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(secondsFor(second, 100, 0), WithinAbs(0.0, 1e-9));
    // 스레드 2 의 코어 0 조각은 창 시작(1000)부터 1500 까지다.
    REQUIRE_THAT(secondsFor(second, 200, 0), WithinAbs(0.5, 1e-9));
}
