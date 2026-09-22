#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

#include "core/FlowEstimator.h"

using namespace pulse;

namespace {

ProcessGroup makeGroup(std::string name, uint32_t pid, std::optional<double> cpu_pct) {
    ProcessGroup g;
    g.name = name;
    g.root_pid = pid;
    g.key = name + ":" + std::to_string(pid);
    g.cpu_pct = cpu_pct;
    return g;
}

const Flow* find(const std::vector<Flow>& flows, const std::string& key, uint32_t core) {
    const auto it = std::find_if(flows.begin(), flows.end(), [&](const Flow& f) {
        return f.group == key && f.core == core;
    });
    return it == flows.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("every flow is marked as estimated in M1", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 100.0}});

    REQUIRE_FALSE(flows.empty());
    REQUIRE(flows[0].source == "estimated");
}

TEST_CASE("a single busy core takes the whole weight of the busiest group", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 80.0}});

    const Flow* f = find(flows, "a.exe:1", 0);
    REQUIRE(f != nullptr);
    REQUIRE_THAT(f->weight, Catch::Matchers::WithinAbs(1.0, 0.0001));
}

TEST_CASE("weight splits in proportion to core load", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("a.exe", 1, 50.0)},
        {CoreLoad{0, 75.0}, CoreLoad{1, 25.0}});

    REQUIRE_THAT(find(flows, "a.exe:1", 0)->weight,
                 Catch::Matchers::WithinAbs(0.75, 0.0001));
    REQUIRE_THAT(find(flows, "a.exe:1", 1)->weight,
                 Catch::Matchers::WithinAbs(0.25, 0.0001));
}

TEST_CASE("a quieter group produces proportionally weaker flows", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("busy.exe", 1, 80.0), makeGroup("quiet.exe", 2, 20.0)},
        {CoreLoad{0, 100.0}});

    REQUIRE_THAT(find(flows, "busy.exe:1", 0)->weight,
                 Catch::Matchers::WithinAbs(1.0, 0.0001));
    REQUIRE_THAT(find(flows, "quiet.exe:2", 0)->weight,
                 Catch::Matchers::WithinAbs(0.25, 0.0001));
}

TEST_CASE("flows below the minimum weight are dropped", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.5;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("busy.exe", 1, 100.0), makeGroup("quiet.exe", 2, 1.0)},
        {CoreLoad{0, 100.0}});

    REQUIRE(find(flows, "busy.exe:1", 0) != nullptr);
    REQUIRE(find(flows, "quiet.exe:2", 0) == nullptr);
}

TEST_CASE("only the strongest cores per group are kept", "[flow]") {
    FlowConfig cfg;
    cfg.min_weight = 0.0;
    cfg.max_flows_per_group = 2;
    const FlowEstimator estimator(cfg);

    const auto flows = estimator.estimate(
        {makeGroup("a.exe", 1, 100.0)},
        {CoreLoad{0, 10.0}, CoreLoad{1, 40.0}, CoreLoad{2, 30.0}, CoreLoad{3, 20.0}});

    REQUIRE(flows.size() == 2);
    REQUIRE(find(flows, "a.exe:1", 1) != nullptr);
    REQUIRE(find(flows, "a.exe:1", 2) != nullptr);
}

TEST_CASE("a group without a cpu reading produces no flows", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("warming.exe", 1, std::nullopt)},
                                          {CoreLoad{0, 100.0}});

    REQUIRE(flows.empty());
}

TEST_CASE("fully idle cores produce no flows", "[flow]") {
    const FlowEstimator estimator;

    const auto flows = estimator.estimate({makeGroup("a.exe", 1, 50.0)},
                                          {CoreLoad{0, 0.0}, CoreLoad{1, 0.0}});

    REQUIRE(flows.empty());
}

TEST_CASE("no groups yields no flows", "[flow]") {
    const FlowEstimator estimator;

    REQUIRE(estimator.estimate({}, {CoreLoad{0, 50.0}}).empty());
}
