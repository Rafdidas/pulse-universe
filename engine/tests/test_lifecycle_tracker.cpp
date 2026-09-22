#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/LifecycleTracker.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid, uint32_t ppid, std::string name,
                       uint64_t start_time_ms = 1000) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.start_time_ms = start_time_ms;
    return p;
}

bool containsPid(const std::vector<uint32_t>& pids, uint32_t pid) {
    return std::find(pids.begin(), pids.end(), pid) != pids.end();
}

}  // namespace

TEST_CASE("the first update reports nothing", "[lifecycle]") {
    // 기동 시 이미 떠 있던 수백 개가 전부 생성으로 잡히면
    // 프론트엔드가 생성 애니메이션을 수백 번 재생하게 된다.
    LifecycleTracker tracker;

    const auto delta = tracker.update({
        makeProcess(1, 0, "a.exe"),
        makeProcess(2, 0, "b.exe"),
    });

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("a new process is reported as spawned", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe")});

    const auto delta = tracker.update({
        makeProcess(1, 0, "a.exe"),
        makeProcess(2, 1, "child.exe"),
    });

    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].pid == 2);
    REQUIRE(delta.spawned[0].ppid == 1);
    REQUIRE(delta.spawned[0].name == "child.exe");
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("a vanished process is reported as terminated", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe"), makeProcess(2, 1, "b.exe")});

    const auto delta = tracker.update({makeProcess(1, 0, "a.exe")});

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.size() == 1);
    REQUIRE(delta.terminated[0] == 2);
}

TEST_CASE("a reused pid counts as both a termination and a spawn", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(5, 0, "old.exe", 1000)});

    const auto delta = tracker.update({makeProcess(5, 0, "new.exe", 9000)});

    REQUIRE(containsPid(delta.terminated, 5));
    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].pid == 5);
    REQUIRE(delta.spawned[0].name == "new.exe");
}

TEST_CASE("a steady process set produces no events", "[lifecycle]") {
    LifecycleTracker tracker;
    const std::vector<RawProcess> set = {makeProcess(1, 0, "a.exe"),
                                         makeProcess(2, 0, "b.exe")};
    tracker.update(set);

    const auto delta = tracker.update(set);

    REQUIRE(delta.spawned.empty());
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("everything disappearing reports every pid as terminated", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe"), makeProcess(2, 0, "b.exe")});

    const auto delta = tracker.update({});

    REQUIRE(delta.terminated.size() == 2);
    REQUIRE(containsPid(delta.terminated, 1));
    REQUIRE(containsPid(delta.terminated, 2));
}

TEST_CASE("a pid repeated within one sample is reported once", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(1, 0, "a.exe")});

    const auto delta = tracker.update({
        makeProcess(1, 0, "a.exe"),
        makeProcess(2, 1, "dup.exe", 5000),
        makeProcess(2, 1, "dup.exe", 5000),
    });

    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].pid == 2);
    REQUIRE(delta.terminated.empty());
}

TEST_CASE("a reused pid repeated within one sample is reported once", "[lifecycle]") {
    LifecycleTracker tracker;
    tracker.update({makeProcess(5, 0, "old.exe", 1000)});

    const auto delta = tracker.update({
        makeProcess(5, 0, "new.exe", 9000),
        makeProcess(5, 0, "new.exe", 9000),
    });

    REQUIRE(delta.terminated.size() == 1);
    REQUIRE(delta.terminated[0] == 5);
    REQUIRE(delta.spawned.size() == 1);
    REQUIRE(delta.spawned[0].name == "new.exe");
}
