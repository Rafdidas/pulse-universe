#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "platform/windows/EtwSchedulerCollector.h"

using namespace pulse;

// ETW 스펙 11절. 관리자 권한으로 돌 때만 실제 세션을 연다. 권한이 없으면 SKIP 이다.
TEST_CASE("the ETW collector measures this process's run time when elevated", "[etw]") {
    std::string error;
    std::unique_ptr<EtwSchedulerCollector> collector = EtwSchedulerCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW collector unavailable: " + error);
    }

    // 첫 창은 세션을 켠 뒤부터다. 비워 두고 새 창을 잰다.
    collector->drain();

    // 이 스레드가 0.5 초 동안 코어를 쓴다.
    const auto busy_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    volatile unsigned long long spin = 0;
    while (std::chrono::steady_clock::now() < busy_until) {
        spin = spin + 1;
    }
    // 이벤트는 버퍼 플러시(1 초)마다 전달된다. 넉넉히 기다린다.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    const std::optional<RawThreadMapping> mapping = collector->drain();
    REQUIRE(mapping.has_value());
    REQUIRE(mapping->window_seconds > 1.0);

    const uint32_t self = ::GetCurrentProcessId();
    double seconds = 0.0;
    for (const RawRunTime& run : mapping->run_times) {
        if (run.pid == self) {
            seconds += run.seconds;
        }
    }
    CAPTURE(seconds, mapping->run_times.size());
    REQUIRE(seconds > 0.4);
}

TEST_CASE("the ETW session is gone after the collector is destroyed", "[etw]") {
    std::string error;
    std::unique_ptr<EtwSchedulerCollector> collector = EtwSchedulerCollector::start(error);
    if (collector == nullptr) {
        SKIP("ETW collector unavailable: " + error);
    }
    collector.reset();

    // 같은 이름으로 곧바로 다시 열 수 있어야 한다 (남은 세션이 없다).
    std::unique_ptr<EtwSchedulerCollector> again = EtwSchedulerCollector::start(error);
    REQUIRE(again != nullptr);
}

TEST_CASE("a second collector does not take over a live session", "[etw]") {
    std::string error;
    std::unique_ptr<EtwSchedulerCollector> first = EtwSchedulerCollector::start(error);
    if (first == nullptr) {
        SKIP("ETW collector unavailable: " + error);
    }

    std::string second_error;
    std::unique_ptr<EtwSchedulerCollector> second = EtwSchedulerCollector::start(second_error);

    REQUIRE(second == nullptr);
    REQUIRE(second_error == "another pulse-engine is already measuring thread mapping");
    // 첫 수집기의 세션은 그대로 살아서 창을 만든다 (플러시 1 초를 넘겨 기다린다).
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    REQUIRE(first->drain().has_value());
}
