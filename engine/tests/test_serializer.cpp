#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <boost/json.hpp>

#include <string>

#include "network/Serializer.h"

using namespace pulse;
namespace json = boost::json;

namespace {

SystemSnapshot makeSnapshot() {
    SystemSnapshot s;
    s.seq = 1423;
    s.t = 1758531600123ull;
    s.system.cpu_pct = 34.2;
    s.system.mem_used_mb = 18432.0;
    s.system.mem_total_mb = 32553.0;
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
    g.started_at = 1758520000000ull;
    g.account = Account::User;
    g.image_path = "C:/Program Files/Naver/Whale/whale.exe";

    ChildProcess c;
    c.pid = 8400;
    c.name = "whale.exe";
    c.role = "child";
    c.cpu_pct = 4.1;
    c.mem_mb = 570.5;
    c.threads = 18;
    g.children.push_back(c);

    s.groups.push_back(g);

    Flow f;
    f.group = "whale.exe:22008";
    f.core = 3;
    f.weight = 0.42;
    s.flows.push_back(f);

    SpawnedProcess sp;
    sp.pid = 20114;
    sp.ppid = 22008;
    sp.name = "whale.exe";
    sp.group = "whale.exe:22008";
    s.lifecycle.spawned.push_back(sp);
    s.lifecycle.terminated.push_back(18002);

    s.ambient.service_proc_count = 84;
    s.ambient.service_mem_mb = 1400.0;
    return s;
}

json::object parseObject(const std::string& text) {
    return json::parse(text).as_object();
}

}  // namespace

TEST_CASE("hello carries the envelope, capabilities and host", "[serialize]") {
    HelloInfo info;
    info.interval_ms = 1000;
    info.core_count = 28;
    info.elevated = true;
    info.os = "Windows";

    const json::object o = parseObject(serializeHello(info));

    REQUIRE(o.at("type").as_string() == "hello");
    REQUIRE(o.at("v").to_number<int64_t>() == 1);
    REQUIRE(o.at("interval_ms").to_number<int64_t>() == 1000);
    REQUIRE(o.at("core_count").to_number<int64_t>() == 28);
    REQUIRE(o.at("capabilities").as_object().at("thread_mapping").as_string() == "estimated");
    REQUIRE(o.at("host").as_object().at("os").as_string() == "Windows");
    REQUIRE(o.at("host").as_object().at("elevated").as_bool());
}

TEST_CASE("snapshot carries the protocol envelope", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));

    REQUIRE(o.at("type").as_string() == "snapshot");
    REQUIRE(o.at("v").to_number<int64_t>() == 1);
    REQUIRE(o.at("seq").to_number<int64_t>() == 1423);
    REQUIRE(o.at("t").to_number<int64_t>() == 1758531600123ll);
}

TEST_CASE("system totals are serialized", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& sys = o.at("system").as_object();

    REQUIRE_THAT(sys.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(34.2, 0.0001));
    REQUIRE_THAT(sys.at("mem_used_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(18432.0, 0.0001));
    REQUIRE_THAT(sys.at("mem_total_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(32553.0, 0.0001));
    REQUIRE(sys.at("process_total").to_number<int64_t>() == 382);
    REQUIRE(sys.at("thread_total").to_number<int64_t>() == 4187);
}

TEST_CASE("an absent cpu reading is serialized as null, not zero", "[serialize]") {
    // 계약서 6.1: 첫 주기에는 CPU 값이 없다. 0.0 과 구분되어야 한다.
    SystemSnapshot s = makeSnapshot();
    s.system.cpu_pct.reset();
    s.groups[0].cpu_pct.reset();
    s.groups[0].children[0].cpu_pct.reset();

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("system").as_object().at("cpu_pct").is_null());
    const json::object& g = o.at("groups").as_array().at(0).as_object();
    REQUIRE(g.at("cpu_pct").is_null());
    REQUIRE(g.at("children").as_array().at(0).as_object().at("cpu_pct").is_null());
}

TEST_CASE("cores are serialized in order", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::array& cores = o.at("cores").as_array();

    REQUIRE(cores.size() == 2);
    REQUIRE(cores.at(0).as_object().at("id").to_number<int64_t>() == 0);
    REQUIRE_THAT(cores.at(0).as_object().at("pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(82.4, 0.0001));
    REQUIRE(cores.at(1).as_object().at("id").to_number<int64_t>() == 1);
}

TEST_CASE("a group carries every contract field", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& g = o.at("groups").as_array().at(0).as_object();

    REQUIRE(g.at("key").as_string() == "whale.exe:22008");
    REQUIRE(g.at("name").as_string() == "whale.exe");
    REQUIRE(g.at("root_pid").to_number<int64_t>() == 22008);
    REQUIRE_THAT(g.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(12.4, 0.0001));
    REQUIRE_THAT(g.at("mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(3626.0, 0.0001));
    REQUIRE(g.at("proc_count").to_number<int64_t>() == 26);
    REQUIRE(g.at("thread_count").to_number<int64_t>() == 412);
    REQUIRE(g.at("started_at").to_number<int64_t>() == 1758520000000ll);
    REQUIRE(g.at("account").as_string() == "user");
    REQUIRE(g.at("image_path").as_string() == "C:/Program Files/Naver/Whale/whale.exe");
    REQUIRE(g.at("children").as_array().size() == 1);
}

TEST_CASE("a system account is serialized as the string system", "[serialize]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].account = Account::System;

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("groups").as_array().at(0).as_object().at("account").as_string() == "system");
}

TEST_CASE("a child carries every contract field", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& c =
        o.at("groups").as_array().at(0).as_object().at("children").as_array().at(0).as_object();

    REQUIRE(c.at("pid").to_number<int64_t>() == 8400);
    REQUIRE(c.at("name").as_string() == "whale.exe");
    REQUIRE(c.at("role").as_string() == "child");
    REQUIRE_THAT(c.at("cpu_pct").to_number<double>(),
                 Catch::Matchers::WithinAbs(4.1, 0.0001));
    REQUIRE_THAT(c.at("mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(570.5, 0.0001));
    REQUIRE(c.at("threads").to_number<int64_t>() == 18);
}

TEST_CASE("flows carry the estimated source marker", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& f = o.at("flows").as_array().at(0).as_object();

    REQUIRE(f.at("group").as_string() == "whale.exe:22008");
    REQUIRE(f.at("core").to_number<int64_t>() == 3);
    REQUIRE_THAT(f.at("weight").to_number<double>(),
                 Catch::Matchers::WithinAbs(0.42, 0.0001));
    REQUIRE(f.at("source").as_string() == "estimated");
}

TEST_CASE("lifecycle carries spawned entries with their group and terminated pids",
          "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& life = o.at("lifecycle").as_object();
    const json::object& sp = life.at("spawned").as_array().at(0).as_object();

    REQUIRE(sp.at("pid").to_number<int64_t>() == 20114);
    REQUIRE(sp.at("ppid").to_number<int64_t>() == 22008);
    REQUIRE(sp.at("name").as_string() == "whale.exe");
    REQUIRE(sp.at("group").as_string() == "whale.exe:22008");

    REQUIRE(life.at("terminated").as_array().size() == 1);
    REQUIRE(life.at("terminated").as_array().at(0).to_number<int64_t>() == 18002);
}

TEST_CASE("ambient service totals are serialized", "[serialize]") {
    const json::object o = parseObject(serializeSnapshot(makeSnapshot()));
    const json::object& a = o.at("ambient").as_object();

    REQUIRE(a.at("service_proc_count").to_number<int64_t>() == 84);
    REQUIRE_THAT(a.at("service_mem_mb").to_number<double>(),
                 Catch::Matchers::WithinAbs(1400.0, 0.0001));
}

TEST_CASE("empty collections are serialized as arrays, not null", "[serialize]") {
    SystemSnapshot s;
    s.seq = 1;

    const json::object o = parseObject(serializeSnapshot(s));

    REQUIRE(o.at("cores").is_array());
    REQUIRE(o.at("cores").as_array().empty());
    REQUIRE(o.at("groups").as_array().empty());
    REQUIRE(o.at("flows").as_array().empty());
    REQUIRE(o.at("lifecycle").as_object().at("spawned").as_array().empty());
    REQUIRE(o.at("lifecycle").as_object().at("terminated").as_array().empty());
}

TEST_CASE("names with quotes, backslashes and non-ascii survive a round trip",
          "[serialize]") {
    SystemSnapshot s = makeSnapshot();
    s.groups[0].name = "he said \"hi\"\\x";
    s.groups[0].image_path = "C:\\Program Files\\한글 폴더\\app.exe";

    const json::object o = parseObject(serializeSnapshot(s));
    const json::object& g = o.at("groups").as_array().at(0).as_object();

    REQUIRE(g.at("name").as_string() == "he said \"hi\"\\x");
    REQUIRE(g.at("image_path").as_string() == "C:\\Program Files\\한글 폴더\\app.exe");
}
