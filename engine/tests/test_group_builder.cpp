#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#include "core/GroupBuilder.h"

using namespace pulse;

namespace {

RawProcess makeProcess(uint32_t pid,
                       uint32_t ppid,
                       std::string name,
                       uint64_t mem_bytes = 1024ull * 1024ull,
                       uint64_t start_time_ms = 1000,
                       Account account = Account::User) {
    RawProcess p;
    p.pid = pid;
    p.ppid = ppid;
    p.name = std::move(name);
    p.mem_bytes = mem_bytes;
    p.thread_count = 1;
    p.start_time_ms = start_time_ms;
    p.account = account;
    p.image_path = "C:/app/" + p.name;
    return p;
}

const ProcessGroup* findGroup(const GroupingResult& result, const std::string& key) {
    const auto it = std::find_if(result.groups.begin(), result.groups.end(),
                                 [&](const ProcessGroup& g) { return g.key == key; });
    return it == result.groups.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("a lone process becomes its own group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({makeProcess(100, 4, "explorer.exe")});

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].key == "explorer.exe:100");
    REQUIRE(result.groups[0].root_pid == 100);
    REQUIRE(result.groups[0].proc_count == 1);
    REQUIRE(result.groups[0].children.empty());
}

TEST_CASE("children sharing the parent name collapse into one group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(22008, 21876, "whale.exe", 400ull * 1024 * 1024, 1000),
        makeProcess(8400, 22008, "whale.exe", 570ull * 1024 * 1024, 2000),
        makeProcess(22264, 22008, "whale.exe", 238ull * 1024 * 1024, 2000),
    });

    REQUIRE(result.groups.size() == 1);
    const ProcessGroup& g = result.groups[0];
    REQUIRE(g.key == "whale.exe:22008");
    REQUIRE(g.root_pid == 22008);
    REQUIRE(g.proc_count == 3);
    REQUIRE(g.children.size() == 2);
    REQUIRE_THAT(g.mem_mb, Catch::Matchers::WithinAbs(1208.0, 0.01));
}

TEST_CASE("a differently named child starts its own group", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(500, 4, "Code.exe"),
        makeProcess(600, 500, "node.exe"),
    });

    REQUIRE(result.groups.size() == 2);
    REQUIRE(findGroup(result, "Code.exe:500") != nullptr);
    REQUIRE(findGroup(result, "node.exe:600") != nullptr);
}

TEST_CASE("grandchildren of the same name reach the true root", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(10, 4, "app.exe", 1024 * 1024, 1000),
        makeProcess(11, 10, "app.exe", 1024 * 1024, 2000),
        makeProcess(12, 11, "app.exe", 1024 * 1024, 3000),
    });

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].root_pid == 10);
    REQUIRE(result.groups[0].proc_count == 3);
}

TEST_CASE("a parent started after its child is not treated as the parent", "[group]") {
    // pid 재사용 방어. 부모의 시작 시각이 자식보다 늦으면 실제 부모가 아니다.
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(300, 4, "app.exe", 1024 * 1024, 9000),
        makeProcess(301, 300, "app.exe", 1024 * 1024, 1000),
    });

    REQUIRE(result.groups.size() == 2);
}

TEST_CASE("a process whose parent is gone becomes a root", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({makeProcess(700, 99999, "orphan.exe")});

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].root_pid == 700);
}

TEST_CASE("a parent cycle does not hang the builder", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(1, 2, "loop.exe", 1024 * 1024, 1000),
        makeProcess(2, 1, "loop.exe", 1024 * 1024, 1000),
    });

    REQUIRE_FALSE(result.groups.empty());
}

TEST_CASE("svchost is excluded from groups and counted as ambient", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(800, 4, "svchost.exe", 100ull * 1024 * 1024, 1000, Account::System),
        makeProcess(801, 4, "SvcHost.exe", 100ull * 1024 * 1024, 1000, Account::System),
        makeProcess(900, 4, "explorer.exe", 50ull * 1024 * 1024, 1000),
    });

    REQUIRE(result.groups.size() == 1);
    REQUIRE(result.groups[0].name == "explorer.exe");
    REQUIRE(result.ambient.service_proc_count == 2);
    REQUIRE_THAT(result.ambient.service_mem_mb, Catch::Matchers::WithinAbs(200.0, 0.01));
}

TEST_CASE("group metadata is taken from the root process", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(50, 4, "app.exe", 1024 * 1024, 5000, Account::User),
        makeProcess(51, 50, "app.exe", 1024 * 1024, 6000, Account::System),
    });

    REQUIRE(result.groups[0].started_at == 5000);
    REQUIRE(result.groups[0].account == Account::User);
    REQUIRE(result.groups[0].image_path == "C:/app/app.exe");
}

TEST_CASE("thread counts are summed across the group", "[group]") {
    const GroupBuilder builder;
    auto root = makeProcess(60, 4, "app.exe");
    root.thread_count = 12;
    auto child = makeProcess(61, 60, "app.exe", 1024 * 1024, 2000);
    child.thread_count = 30;

    const auto result = builder.build({root, child});

    REQUIRE(result.groups[0].thread_count == 42);
}

TEST_CASE("children carry the placeholder role in M1", "[group]") {
    const GroupBuilder builder;

    const auto result = builder.build({
        makeProcess(70, 4, "app.exe", 1024 * 1024, 1000),
        makeProcess(71, 70, "app.exe", 1024 * 1024, 2000),
    });

    REQUIRE(result.groups[0].children[0].role == "child");
}
