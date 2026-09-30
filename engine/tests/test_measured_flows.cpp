#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

#include "core/MeasuredFlows.h"

using namespace pulse;
using Catch::Matchers::WithinAbs;

namespace {

ProcessGroup makeGroup(std::string name, uint32_t root_pid, std::vector<uint32_t> child_pids = {}) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = root_pid;
    g.key = name + ":" + std::to_string(root_pid);
    for (const uint32_t pid : child_pids) {
        ChildProcess child;
        child.pid = pid;
        g.children.push_back(child);
    }
    return g;
}

RawThreadMapping window(double seconds, std::vector<RawRunTime> runs) {
    RawThreadMapping m;
    m.window_seconds = seconds;
    m.run_times = std::move(runs);
    return m;
}

const Flow* find(const std::vector<Flow>& flows, const std::string& key, uint32_t core) {
    const auto it = std::find_if(flows.begin(), flows.end(), [&](const Flow& f) {
        return f.group == key && f.core == core;
    });
    return it == flows.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("measured weight is the share of the window a group ran on a core", "[measured]") {
    const auto flows = measuredFlows({makeGroup("a.exe", 10)},
                                     window(2.0, {{10, 3, 0.5}}), FlowConfig{});

    const Flow* f = find(flows, "a.exe:10", 3);
    REQUIRE(f != nullptr);
    REQUIRE_THAT(f->weight, WithinAbs(0.25, 1e-9));
    REQUIRE(f->source == "measured");
}

TEST_CASE("run time of every group member is summed per core", "[measured]") {
    const auto flows = measuredFlows(
        {makeGroup("a.exe", 10, {11, 12})},
        window(1.0, {{10, 0, 0.1}, {11, 0, 0.2}, {12, 0, 0.3}, {12, 1, 0.4}}), FlowConfig{});

    REQUIRE_THAT(find(flows, "a.exe:10", 0)->weight, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(find(flows, "a.exe:10", 1)->weight, WithinAbs(0.4, 1e-9));
}

TEST_CASE("a weight above one core is clamped to one", "[measured]") {
    // 창 경계에서 이월된 조각 때문에 드물게 창보다 길게 잡힐 수 있다.
    const auto flows = measuredFlows({makeGroup("a.exe", 10, {11})},
                                     window(1.0, {{10, 0, 0.7}, {11, 0, 0.6}}), FlowConfig{});

    REQUIRE_THAT(find(flows, "a.exe:10", 0)->weight, WithinAbs(1.0, 1e-9));
}

TEST_CASE("weak flows are dropped and each group keeps its strongest cores", "[measured]") {
    FlowConfig cfg;
    cfg.min_weight = 0.05;
    cfg.max_flows_per_group = 2;

    const auto flows = measuredFlows(
        {makeGroup("a.exe", 10)},
        window(1.0, {{10, 0, 0.04}, {10, 1, 0.3}, {10, 2, 0.1}, {10, 3, 0.2}}), cfg);

    REQUIRE(flows.size() == 2);
    REQUIRE(flows[0].core == 1);
    REQUIRE(flows[1].core == 3);
}

TEST_CASE("processes outside the shown groups are ignored", "[measured]") {
    const auto flows = measuredFlows({makeGroup("a.exe", 10)},
                                     window(1.0, {{10, 0, 0.5}, {99, 1, 0.9}}), FlowConfig{});

    REQUIRE(flows.size() == 1);
    REQUIRE(flows[0].group == "a.exe:10");
}

TEST_CASE("an empty window yields no flows", "[measured]") {
    REQUIRE(measuredFlows({makeGroup("a.exe", 10)}, window(0.0, {{10, 0, 0.5}}), FlowConfig{})
                .empty());
}
