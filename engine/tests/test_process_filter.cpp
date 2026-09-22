#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/ProcessFilter.h"

using namespace pulse;

namespace {

ProcessGroup makeGroup(std::string name,
                       uint32_t root_pid,
                       double mem_mb,
                       std::optional<double> cpu_pct = 0.0,
                       Account account = Account::User) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = root_pid;
    g.key = name + ":" + std::to_string(root_pid);
    g.mem_mb = mem_mb;
    g.cpu_pct = cpu_pct;
    g.account = account;
    g.proc_count = 1;
    return g;
}

}  // namespace

TEST_CASE("fewer groups than the cap are all returned", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 40;
    const ProcessFilter filter(cfg);

    const auto out = filter.select({makeGroup("a.exe", 1, 10.0)}, 32768.0);

    REQUIRE(out.size() == 1);
}

TEST_CASE("the cap is honoured", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 2;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("small.exe", 1, 10.0),
            makeGroup("big.exe", 2, 3000.0),
            makeGroup("mid.exe", 3, 500.0),
        },
        32768.0);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].name == "big.exe");
    REQUIRE(out[1].name == "mid.exe");
}

TEST_CASE("cpu outweighs memory when its weight is higher", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    cfg.cpu_weight = 10.0;
    cfg.mem_weight = 1.0;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("busy.exe", 1, 10.0, 80.0),
            makeGroup("fat.exe", 2, 3000.0, 0.0),
        },
        32768.0);

    REQUIRE(out[0].name == "busy.exe");
}

TEST_CASE("a user account group outranks a system group of equal weight", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    cfg.user_account_bonus = 1.5;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("service.exe", 1, 500.0, 5.0, Account::System),
            makeGroup("mine.exe", 2, 500.0, 5.0, Account::User),
        },
        32768.0);

    REQUIRE(out[0].name == "mine.exe");
}

TEST_CASE("a missing cpu reading scores as zero rather than excluding the group", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 2;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("warming.exe", 1, 900.0, std::nullopt),
            makeGroup("tiny.exe", 2, 1.0, 0.0),
        },
        32768.0);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].name == "warming.exe");
}

TEST_CASE("ties preserve input order", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 3;
    const ProcessFilter filter(cfg);

    const auto out = filter.select(
        {
            makeGroup("first.exe", 1, 100.0, 1.0),
            makeGroup("second.exe", 2, 100.0, 1.0),
            makeGroup("third.exe", 3, 100.0, 1.0),
        },
        32768.0);

    REQUIRE(out[0].name == "first.exe");
    REQUIRE(out[1].name == "second.exe");
    REQUIRE(out[2].name == "third.exe");
}

TEST_CASE("a zero total memory does not produce a division by zero", "[filter]") {
    FilterConfig cfg;
    cfg.max_groups = 1;
    const ProcessFilter filter(cfg);

    const auto out = filter.select({makeGroup("a.exe", 1, 100.0, 4.0)}, 0.0);

    REQUIRE(out.size() == 1);
}

TEST_CASE("an empty input yields an empty result", "[filter]") {
    const ProcessFilter filter;

    REQUIRE(filter.select({}, 32768.0).empty());
}

TEST_CASE("the configured weights change the ranking", "[filter]") {
    // mem_norm: 6553.6 / 32768 * 100 = 20.0,  1638.4 / 32768 * 100 = 5.0
    const std::vector<ProcessGroup> groups = {
        makeGroup("mem_heavy.exe", 1, 6553.6, 10.0),
        makeGroup("cpu_heavy.exe", 2, 1638.4, 30.0),
    };

    FilterConfig balanced;
    balanced.max_groups = 2;
    // 10*1.0 + 20*1.0 = 30  loses to  30*1.0 + 5*1.0 = 35
    const auto by_balanced = ProcessFilter(balanced).select(groups, 32768.0);
    REQUIRE(by_balanced[0].name == "cpu_heavy.exe");

    FilterConfig memory_led;
    memory_led.max_groups = 2;
    memory_led.cpu_weight = 0.1;
    // 10*0.1 + 20*1.0 = 21  beats  30*0.1 + 5*1.0 = 8
    const auto by_memory = ProcessFilter(memory_led).select(groups, 32768.0);
    REQUIRE(by_memory[0].name == "mem_heavy.exe");
}
