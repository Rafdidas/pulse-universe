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

TEST_CASE("an allow-origin flag is captured", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--allow-origin", "http://localhost:5174"}, options, error) ==
            ParseResult::Ok);
    REQUIRE(options.allowed_origins == std::vector<std::string>{"http://localhost:5174"});
}

TEST_CASE("allow-origin can be given more than once, in order", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--allow-origin", "http://localhost:5174", "--allow-origin",
                   "http://localhost:5175"},
                  options, error) == ParseResult::Ok);
    REQUIRE(options.allowed_origins ==
            std::vector<std::string>{"http://localhost:5174", "http://localhost:5175"});
}

TEST_CASE("allow-origin missing its value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--allow-origin"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("iterations with json is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--json", "--iterations", "3"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("iterations before json is rejected regardless of order", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--iterations", "3", "--json"}, options, error) == ParseResult::Error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("iterations with serve is accepted", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--iterations", "3"}, options, error) == ParseResult::Ok);
    REQUIRE(options.iterations == 3);
}

TEST_CASE("a web root can be given", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--web-root", "C:/web/dist"}, options, error) == ParseResult::Ok);
    REQUIRE(options.web_root == "C:/web/dist");
}

TEST_CASE("web root defaults to empty", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve"}, options, error) == ParseResult::Ok);
    REQUIRE(options.web_root.empty());
}

TEST_CASE("a web root without a value is rejected", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--web-root"}, options, error) == ParseResult::Error);
}

TEST_CASE("usage mentions the web root", "[options]") {
    REQUIRE(usageText().find("--web-root") != std::string::npos);
}

TEST_CASE("mapping defaults to auto", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Auto);
    REQUIRE(usageText().find("--mapping") != std::string::npos);
}

TEST_CASE("mapping accepts auto, estimated and measured", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--dump", "--mapping", "estimated"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Estimated);
    REQUIRE(parse({"--serve", "--mapping", "measured"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Measured);
    REQUIRE(parse({"--json", "--mapping", "auto"}, options, error) == ParseResult::Ok);
    REQUIRE(options.mapping == Mapping::Auto);
}

TEST_CASE("mapping rejects unknown or missing values", "[options]") {
    Options options;
    std::string error;

    REQUIRE(parse({"--serve", "--mapping", "exact"}, options, error) == ParseResult::Error);
    REQUIRE(error == "invalid --mapping");
    REQUIRE(parse({"--serve", "--mapping"}, options, error) == ParseResult::Error);
    REQUIRE(error == "invalid --mapping");
}

TEST_CASE("the default launch serves the web folder with the usual defaults", "[options]") {
    Options options;

    REQUIRE(applyDefaultLaunch(options, "C:\\app\\web", true, false));

    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.web_root == "C:\\app\\web");
    REQUIRE_FALSE(options.use_embedded_web);
    REQUIRE(options.port == 9000);
    REQUIRE(options.interval_ms == 1000);
    REQUIRE(options.mapping == Mapping::Auto);
}

TEST_CASE("the web folder beats the embedded assets", "[options]") {
    Options options;

    REQUIRE(applyDefaultLaunch(options, "C:\\app\\web", true, true));

    REQUIRE(options.web_root == "C:\\app\\web");
    REQUIRE_FALSE(options.use_embedded_web);
}

TEST_CASE("the default launch falls back to the embedded assets without a web folder",
          "[options]") {
    Options missing;
    REQUIRE(applyDefaultLaunch(missing, "C:\\app\\web", false, true));
    REQUIRE(missing.mode == Mode::Serve);
    REQUIRE(missing.use_embedded_web);
    REQUIRE(missing.web_root.empty());

    Options empty_path;
    REQUIRE(applyDefaultLaunch(empty_path, "", true, true));
    REQUIRE(empty_path.use_embedded_web);
    REQUIRE(empty_path.web_root.empty());
}

TEST_CASE("the default launch leaves the options alone with neither folder nor embedded assets",
          "[options]") {
    Options options;
    options.port = 1234;

    REQUIRE_FALSE(applyDefaultLaunch(options, "C:\\app\\web", false, false));
    REQUIRE_FALSE(applyDefaultLaunch(options, "", true, false));

    REQUIRE(options.mode == Mode::None);
    REQUIRE(options.port == 1234);
}

TEST_CASE("usage mentions the no-argument launch", "[options]") {
    REQUIRE(usageText().find("With no arguments") != std::string::npos);
}

TEST_CASE("usage mentions the embedded web assets", "[options]") {
    REQUIRE(usageText().find("--embedded-web") != std::string::npos);
}

TEST_CASE("--embedded-web selects the embedded assets for --serve", "[options]") {
    Options options;
    std::string error;
    const char* argv[] = {"pulse-engine", "--serve", "--embedded-web"};

    REQUIRE(parseOptions(3, argv, options, error) == ParseResult::Ok);

    REQUIRE(options.mode == Mode::Serve);
    REQUIRE(options.use_embedded_web);
    REQUIRE(options.web_root.empty());
}

TEST_CASE("--embedded-web is refused with --web-root and without --serve", "[options]") {
    Options options;
    std::string error;

    const char* both[] = {"pulse-engine", "--serve", "--embedded-web", "--web-root", "web"};
    REQUIRE(parseOptions(5, both, options, error) == ParseResult::Error);
    REQUIRE(error == "--embedded-web cannot be combined with --web-root");

    const char* dump[] = {"pulse-engine", "--dump", "--embedded-web"};
    REQUIRE(parseOptions(3, dump, options, error) == ParseResult::Error);
    REQUIRE(error == "--embedded-web needs --serve");
}

TEST_CASE("--connections selects the connections mode and takes interval and iterations", "[options]") {
    Options options;
    std::string error;
    const char* argv[] = {"pulse-engine", "--connections", "--interval-ms", "500", "--iterations", "3"};

    REQUIRE(parseOptions(6, argv, options, error) == ParseResult::Ok);

    REQUIRE(options.mode == Mode::Connections);
    REQUIRE(options.interval_ms == 500);
    REQUIRE(options.iterations == 3);
}

TEST_CASE("--connections cannot be combined with another mode", "[options]") {
    Options options;
    std::string error;

    const char* with_dump[] = {"pulse-engine", "--connections", "--dump"};
    REQUIRE(parseOptions(3, with_dump, options, error) == ParseResult::Error);
    REQUIRE(error == "only one mode may be given, saw --dump");

    const char* with_serve[] = {"pulse-engine", "--serve", "--connections"};
    REQUIRE(parseOptions(3, with_serve, options, error) == ParseResult::Error);
    REQUIRE(error == "only one mode may be given, saw --connections");
}

TEST_CASE("--connections accepts --max-groups and --mapping like the other modes", "[options]") {
    Options options;
    std::string error;
    const char* argv[] = {"pulse-engine", "--connections", "--max-groups", "10", "--mapping", "estimated"};

    REQUIRE(parseOptions(6, argv, options, error) == ParseResult::Ok);
    REQUIRE(options.mode == Mode::Connections);
}

TEST_CASE("usage lists --connections", "[options]") {
    REQUIRE(usageText().find("--connections") != std::string::npos);
}
