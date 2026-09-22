#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/DataAggregator.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid, uint32_t ppid, std::string name,
                       uint64_t cpu_cumulative_ms, uint64_t mem_bytes,
                       uint32_t thread_count = 1, uint64_t start_time_ms = 1000) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.cpu_cumulative_ms = cpu_cumulative_ms;
    p.mem_bytes = mem_bytes;
    p.thread_count = thread_count;
    p.start_time_ms = start_time_ms;
    p.account = Account::User;
    return p;
}

RawSample makeSample(std::vector<RawProcess> processes, uint64_t timestamp_ms) {
    RawSample s;
    s.processes = std::move(processes);
    s.cores = {RawCore{0, 50.0}, RawCore{1, 50.0}};
    s.memory = RawMemory{8ull * 1024 * 1024 * 1024, 32ull * 1024 * 1024 * 1024};
    s.timestamp_ms = timestamp_ms;
    return s;
}

}  // namespace

TEST_CASE("the first snapshot has no cpu readings", "[aggregate]") {
    // 스펙 6.1: 기동 직후 첫 주기에는 CPU 값이 존재하지 않는다.
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE_FALSE(snap.groups[0].cpu_pct.has_value());
    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("system cpu is present on the first snapshot when cores are populated",
         "[aggregate]") {
    // PDH 는 리더 생성자에서 이미 첫 수집을 해 두므로, 델타 기반 그룹 cpu 와
    // 달리 system.cpu_pct 는 seq 1 부터 유효하다.
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("an empty cores list leaves system cpu empty", "[aggregate]") {
    DataAggregator aggregator(2);

    RawSample sample = makeSample({}, 1000);
    sample.cores.clear();

    const auto snap = aggregator.aggregate(sample);

    REQUIRE_FALSE(snap.system.cpu_pct.has_value());
}

TEST_CASE("the second snapshot carries cpu readings", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));

    const auto snap = aggregator.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024)}, 2000));

    REQUIRE(snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(5.0, 0.0001));
}

TEST_CASE("group cpu is the sum of its member processes", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 0, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 0, 1024 * 1024, 1, 2000),
        },
        1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 100, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 100, 1024 * 1024, 1, 2000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(10.0, 0.0001));
}

TEST_CASE("a pid duplicated within one sample keeps its cpu reading", "[aggregate]") {
    // 같은 pid 가 한 표본에 두 번 들어오면, 두 번째 update() 호출은 방금
    // 저장한 표본과 비교해 "시계가 안 흘렀다" 로 판정되어 nullopt 를 반환한다.
    // 첫 항목의 유효한 값을 덮어써서는 안 된다.
    DataAggregator single(2);
    single.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));
    const auto single_snap = single.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024)}, 2000));

    DataAggregator duped(2);
    duped.aggregate(
        makeSample({makeProcess(1, 0, "a.exe", 1000, 100ull * 1024 * 1024)}, 1000));
    const auto duped_snap = duped.aggregate(makeSample(
        {
            makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024),
            makeProcess(1, 0, "a.exe", 1100, 100ull * 1024 * 1024),
        },
        2000));

    REQUIRE(duped_snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*duped_snap.groups[0].cpu_pct,
                Catch::Matchers::WithinAbs(*single_snap.groups[0].cpu_pct, 0.0001));
}

TEST_CASE("the sequence number advances with each snapshot", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto first = aggregator.aggregate(makeSample({}, 1000));
    const auto second = aggregator.aggregate(makeSample({}, 2000));

    REQUIRE(first.seq == 1);
    REQUIRE(second.seq == 2);
}

TEST_CASE("system totals count every process including filtered ones", "[aggregate]") {
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 10),
            makeProcess(2, 0, "small.exe", 0, 1024 * 1024, 5),
            makeProcess(3, 0, "svchost.exe", 0, 1024 * 1024, 3),
        },
        1000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.system.process_total == 3);
    REQUIRE(snap.system.thread_total == 18);
}

TEST_CASE("ambient service totals survive filtering", "[aggregate]") {
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024),
            makeProcess(2, 0, "svchost.exe", 0, 100ull * 1024 * 1024),
        },
        1000));

    REQUIRE(snap.ambient.service_proc_count == 1);
    REQUIRE_THAT(snap.ambient.service_mem_mb, Catch::Matchers::WithinAbs(100.0, 0.01));
}

TEST_CASE("lifecycle is tracked across the unfiltered set", "[aggregate]") {
    // 필터 상한이 1이어도, 목록에 오르지 못한 프로세스의 생성이 보고되어야 한다.
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024)}, 1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024),
            makeProcess(2, 0, "tiny.exe", 0, 1024 * 1024),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].name == "big.exe");
    REQUIRE(snap.lifecycle.spawned.size() == 1);
    REQUIRE(snap.lifecycle.spawned[0].name == "tiny.exe");
}

TEST_CASE("memory totals are converted to megabytes", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE_THAT(snap.system.mem_total_mb, Catch::Matchers::WithinAbs(32768.0, 0.01));
    REQUIRE_THAT(snap.system.mem_used_mb, Catch::Matchers::WithinAbs(8192.0, 0.01));
}

TEST_CASE("core loads are carried through", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1000));

    REQUIRE(snap.cores.size() == 2);
    REQUIRE(snap.cores[0].id == 0);
    REQUIRE_THAT(snap.cores[0].pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("system cpu is the mean of core loads", "[aggregate]") {
    DataAggregator aggregator(2);
    aggregator.aggregate(makeSample({}, 1000));

    const auto snap = aggregator.aggregate(makeSample({}, 2000));

    REQUIRE(snap.system.cpu_pct.has_value());
    REQUIRE_THAT(*snap.system.cpu_pct, Catch::Matchers::WithinAbs(50.0, 0.0001));
}

TEST_CASE("timestamps are carried through", "[aggregate]") {
    DataAggregator aggregator(2);

    const auto snap = aggregator.aggregate(makeSample({}, 1758531600123));

    REQUIRE(snap.t == 1758531600123);
}

TEST_CASE("a spawned process into a filtered-out group still reports its group key",
         "[aggregate]") {
    // 필터 상한이 1이면 두 그룹 중 하나만 화면에 남는다. 새로 생긴 프로세스가
    // 살아남지 못한 그룹에 속하더라도, group 필드는 필터 전 grouping 결과에서
    // 구해지므로 올바른 key 를 담아야 한다.
    AggregatorConfig cfg;
    cfg.filter.max_groups = 1;
    DataAggregator aggregator(2, cfg);

    aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 1, 1000),
            makeProcess(2, 0, "small.exe", 0, 1ull * 1024 * 1024, 1, 2000),
        },
        1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "big.exe", 0, 900ull * 1024 * 1024, 1, 1000),
            makeProcess(2, 0, "small.exe", 0, 1ull * 1024 * 1024, 1, 2000),
            makeProcess(3, 2, "small.exe", 0, 1ull * 1024 * 1024, 1, 3000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].name == "big.exe");

    REQUIRE(snap.lifecycle.spawned.size() == 1);
    REQUIRE(snap.lifecycle.spawned[0].pid == 3);
    REQUIRE(snap.lifecycle.spawned[0].group == "small.exe:2");
}

TEST_CASE("a group's cpu is the sum of members that have a reading", "[aggregate]") {
    // 루트는 값을 갖지만 막 생겨난 자식은 아직 두 번째 표본을 못 받아 값이
    // 없다. 그룹 cpu 는 값을 가진 구성원의 합, 즉 루트만의 기여분이어야 한다
    // -- 자식이 하나 늘 때마다 그룹 전체가 비어버리면 안 된다.
    DataAggregator aggregator(2);
    aggregator.aggregate(
        makeSample({makeProcess(1, 0, "app.exe", 0, 1024 * 1024, 1, 1000)}, 1000));

    const auto snap = aggregator.aggregate(makeSample(
        {
            makeProcess(1, 0, "app.exe", 100, 1024 * 1024, 1, 1000),
            makeProcess(2, 1, "app.exe", 0, 1024 * 1024, 1, 2000),
        },
        2000));

    REQUIRE(snap.groups.size() == 1);
    REQUIRE(snap.groups[0].cpu_pct.has_value());
    REQUIRE_THAT(*snap.groups[0].cpu_pct, Catch::Matchers::WithinAbs(5.0, 0.0001));
}
