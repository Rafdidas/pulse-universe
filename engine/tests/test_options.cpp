#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "cli/Options.h"

using namespace pulse;

namespace {

ParseResult parse(std::vector<const char*> args, Options& out, std::string& error) {
    args.insert(args.begin(), "pulse-engine.exe");
    return parseOptions(static_cast<int>(args.size()), args.data(), out, error);
}

}  // namespace

TEST_CASE("no arguments shows usage", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({}, options, error) == ParseResult::ShowUsage);
}

TEST_CASE("dump alone uses the documented defaults", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Dump);
    REQUIRE(options.interval_ms == 1000);
    REQUIRE(options.iterations == 0);
    REQUIRE(options.max_groups == 40);
}

TEST_CASE("json selects its own mode", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--json"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Json);
}

TEST_CASE("two modes at once are rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--json"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("a value is read for each flag", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "500", "--iterations", "3", "--max-groups", "12"},
                  options, error) == ParseResult::Ok);
    REQUIRE(options.interval_ms == 500);
    REQUIRE(options.iterations == 3);
    REQUIRE(options.max_groups == 12);
}

TEST_CASE("a negative number is rejected rather than wrapping around", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "-5"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--max-groups", "-1"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "-1"}, options, error) == ParseResult::Error);
}

TEST_CASE("a non numeric value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--iterations", "abc"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "3x"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--dump", "--iterations", "+3"}, options, error) == ParseResult::Error);
}

TEST_CASE("a zero interval is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "0"}, options, error) == ParseResult::Error);
}

TEST_CASE("a flag missing its value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("an unknown flag is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--colour"}, options, error) == ParseResult::Error);
}

TEST_CASE("options are left untouched when parsing fails", "[options]") {
    Options options;
    options.interval_ms = 7;
    std::string error;

    REQUIRE(parse({"--dump", "--interval-ms", "-5"}, options, error) == ParseResult::Error);
    REQUIRE(options.interval_ms == 7);
}

TEST_CASE("usage mentions every mode", "[options]") {
    const std::string usage = usageText();

    REQUIRE(usage.find("--dump") != std::string::npos);
    REQUIRE(usage.find("--json") != std::string::npos);
    REQUIRE(usage.find("--serve") != std::string::npos);
}

TEST_CASE("serve selects its own mode with a default port", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.port == 9000);
}

TEST_CASE("a port can be given", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--port", "9100"}, options, error) == ParseResult::Ok);
    REQUIRE(options.port == 9100);
}

TEST_CASE("a port outside the valid range is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--port", "0"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--serve", "--port", "70000"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--serve", "--port", "-1"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--serve", "--port", "65536"}, options, error) == ParseResult::Error);
}

TEST_CASE("the highest valid port is accepted", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--port", "65535"}, options, error) == ParseResult::Ok);
    REQUIRE(options.port == 65535);
}

TEST_CASE("serve cannot be combined with another mode", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--dump"}, options, error) == ParseResult::Error);
    REQUIRE(parse({"--json", "--serve"}, options, error) == ParseResult::Error);
}
