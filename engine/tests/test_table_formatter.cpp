#include <catch2/catch_test_macros.hpp>

#include <string>

#include "cli/TableFormatter.h"

using namespace pulse;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

SystemSnapshot makeSnapshot() {
    SystemSnapshot s;
    s.seq = 7;
    s.t = 1758531600123;
    s.system.cpu_pct = 34.2;
    s.system.mem_used_mb = 18432.0;
    s.system.mem_total_mb = 32768.0;
    s.system.process_total = 382;
    s.system.thread_total = 4187;
    s.cores = {CoreLoad{0, 82.4}, CoreLoad{1, 12.1}};

    ProcessGroup g;
    g.key = "whale.exe:22008";
    g.name = "whale.exe";
    g.root_pid = 22008;
    g.cpu_pct = 12.4;
    g.mem_mb = 3626.0;
    g.proc_count = 26;
    g.thread_count = 412;
    g.account = Account::User;
    s.groups.push_back(g);

    s.ambient.service_proc_count = 84;
    s.ambient.service_mem_mb = 1400.0;
    return s;
}

}  // namespace

TEST_CASE("the table shows the sequence number and system totals", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "seq 7"));
    REQUIRE(contains(out, "382"));
    REQUIRE(contains(out, "4187"));
}

TEST_CASE("the table lists each group with its figures", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "whale.exe"));
    REQUIRE(contains(out, "22008"));
    REQUIRE(contains(out, "26"));
    REQUIRE(contains(out, "3626"));
    REQUIRE(contains(out, "12.4"));
}

TEST_CASE("a group without a cpu reading shows a dash", "[format]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].cpu_pct.reset();
    s.system.cpu_pct.reset();

    const std::string out = formatSnapshotTable(s);

    REQUIRE(contains(out, "-"));
    REQUIRE_FALSE(contains(out, "12.4"));
}

TEST_CASE("the table shows per core load", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "82.4"));
    REQUIRE(contains(out, "12.1"));
}

TEST_CASE("the table shows the ambient service summary", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "84"));
    REQUIRE(contains(out, "1400"));
}

TEST_CASE("the account is shown per group", "[format]") {
    const std::string out = formatSnapshotTable(makeSnapshot());

    REQUIRE(contains(out, "user"));
}

TEST_CASE("an empty snapshot still renders a header", "[format]") {
    SystemSnapshot s;

    const std::string out = formatSnapshotTable(s);

    REQUIRE(contains(out, "GROUP"));
}

TEST_CASE("a long group name is truncated on a utf-8 boundary", "[format]") {
    SystemSnapshot s = makeSnapshot();
    // 'a' + 가x9 = 1 + 27 = 28 bytes, so a 27-byte cut lands mid-character.
    s.groups[0].name = "a가가가가가가가가가";
    const std::string kept = "a가가가가가가가가";  // 1 + 24 = 25 bytes

    const std::string out = formatSnapshotTable(s);

    REQUIRE(contains(out, kept));
    REQUIRE_FALSE(contains(out, s.groups[0].name));
}
