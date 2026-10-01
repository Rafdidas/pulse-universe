#include "network/Serializer.h"

#include <boost/json.hpp>

#include <cmath>

namespace pulse {
namespace {

namespace json = boost::json;

// std::optional<double> 은 값이 없으면 JSON null 이 된다.
// 0.0 과 구분되어야 한다 — 계약서 6.1 절.
json::value optionalNumber(const std::optional<double>& value) {
    if (!value.has_value()) {
        return nullptr;
    }
    return *value;
}

// 속도(바이트/초)는 정수로 보낸다. 값이 없으면 null — 0 과 구분된다 (M12 스펙 D123).
json::value optionalRate(const std::optional<double>& value) {
    if (!value.has_value()) {
        return nullptr;
    }
    return static_cast<std::int64_t>(std::llround(*value));
}

json::array serializeNetworkEndpoints(const std::vector<NetworkEndpointOut>& endpoints) {
    json::array out;
    out.reserve(endpoints.size());
    for (const NetworkEndpointOut& e : endpoints) {
        json::array ports;
        for (const uint16_t port : e.ports) {
            ports.push_back(json::value(port));
        }
        json::array groups;
        for (const std::string& group : e.groups) {
            groups.push_back(json::value(group));
        }
        out.push_back(json::object{
            {"ip", e.ip},
            {"private", e.is_private},
            {"ports", ports},
            {"connections", e.connections},
            {"groups", groups},
            {"down_bps", optionalRate(e.down_bps)},
            {"up_bps", optionalRate(e.up_bps)},
        });
    }
    return out;
}

json::array serializeNetworkConnections(const std::vector<NetworkConnectionOut>& connections) {
    json::array out;
    out.reserve(connections.size());
    for (const NetworkConnectionOut& c : connections) {
        out.push_back(json::object{
            {"group", c.group},
            {"pid", c.pid},
            {"proto", c.proto},
            {"local_port", c.local_port},
            {"remote", c.remote},
            {"remote_port", c.remote_port},
            {"state", c.state},
            {"down_bps", optionalRate(c.down_bps)},
            {"up_bps", optionalRate(c.up_bps)},
        });
    }
    return out;
}

json::object serializeNetwork(const NetworkSnapshot& network) {
    return json::object{
        {"traffic", network.traffic},
        {"summary",
         json::object{
             {"connections", network.summary.connections},
             {"established", network.summary.established},
             {"endpoints", network.summary.endpoints},
             {"udp_sockets", network.summary.udp_sockets},
             {"down_bps", optionalRate(network.summary.down_bps)},
             {"up_bps", optionalRate(network.summary.up_bps)},
         }},
        {"endpoints", serializeNetworkEndpoints(network.endpoints)},
        {"connections", serializeNetworkConnections(network.connections)},
    };
}

json::string accountName(Account account) {
    return account == Account::User ? "user" : "system";
}

json::array serializeChildren(const std::vector<ChildProcess>& children) {
    json::array out;
    out.reserve(children.size());
    for (const ChildProcess& c : children) {
        out.push_back(json::object{
            {"pid", c.pid},
            {"name", c.name},
            {"role", c.role},
            {"cpu_pct", optionalNumber(c.cpu_pct)},
            {"mem_mb", c.mem_mb},
            {"threads", c.threads},
        });
    }
    return out;
}

json::array serializeCores(const std::vector<CoreLoad>& cores) {
    json::array out;
    out.reserve(cores.size());
    for (const CoreLoad& c : cores) {
        out.push_back(json::object{{"id", c.id}, {"pct", c.pct}});
    }
    return out;
}

json::array serializeGroups(const std::vector<ProcessGroup>& groups) {
    json::array out;
    out.reserve(groups.size());
    for (const ProcessGroup& g : groups) {
        out.push_back(json::object{
            {"key", g.key},
            {"name", g.name},
            {"root_pid", g.root_pid},
            {"cpu_pct", optionalNumber(g.cpu_pct)},
            {"mem_mb", g.mem_mb},
            {"proc_count", g.proc_count},
            {"thread_count", g.thread_count},
            {"started_at", g.started_at},
            {"account", accountName(g.account)},
            {"image_path", g.image_path},
            {"children", serializeChildren(g.children)},
        });
    }
    return out;
}

json::array serializeFlows(const std::vector<Flow>& flows) {
    json::array out;
    out.reserve(flows.size());
    for (const Flow& f : flows) {
        out.push_back(json::object{
            {"group", f.group},
            {"core", f.core},
            {"weight", f.weight},
            {"source", f.source},
        });
    }
    return out;
}

json::object serializeLifecycle(const LifecycleDelta& lifecycle) {
    json::array spawned;
    spawned.reserve(lifecycle.spawned.size());
    for (const SpawnedProcess& p : lifecycle.spawned) {
        spawned.push_back(json::object{
            {"pid", p.pid},
            {"ppid", p.ppid},
            {"name", p.name},
            {"group", p.group},
        });
    }

    json::array terminated;
    terminated.reserve(lifecycle.terminated.size());
    for (const uint32_t pid : lifecycle.terminated) {
        terminated.push_back(json::value(pid));
    }

    return json::object{{"spawned", spawned}, {"terminated", terminated}};
}

}  // namespace

std::string serializeHello(const HelloInfo& info) {
    const json::object message{
        {"type", "hello"},
        {"v", kProtocolVersion},
        {"interval_ms", info.interval_ms},
        {"core_count", info.core_count},
        {"capabilities",
         json::object{{"thread_mapping", info.thread_mapping}, {"network_traffic", info.network_traffic}}},
        {"host", json::object{{"os", info.os}, {"elevated", info.elevated}}},
        {"session", info.session},
    };
    return json::serialize(message);
}

std::string serializeSnapshot(const SystemSnapshot& snapshot) {
    const json::object message{
        {"type", "snapshot"},
        {"v", kProtocolVersion},
        {"seq", snapshot.seq},
        {"t", snapshot.t},
        {"system",
         json::object{
             {"cpu_pct", optionalNumber(snapshot.system.cpu_pct)},
             {"mem_used_mb", snapshot.system.mem_used_mb},
             {"mem_total_mb", snapshot.system.mem_total_mb},
             {"process_total", snapshot.system.process_total},
             {"thread_total", snapshot.system.thread_total},
         }},
        {"cores", serializeCores(snapshot.cores)},
        {"groups", serializeGroups(snapshot.groups)},
        {"flows", serializeFlows(snapshot.flows)},
        {"lifecycle", serializeLifecycle(snapshot.lifecycle)},
        {"ambient",
         json::object{
             {"service_proc_count", snapshot.ambient.service_proc_count},
             {"service_mem_mb", snapshot.ambient.service_mem_mb},
         }},
        {"network", serializeNetwork(snapshot.network)},
    };
    return json::serialize(message);
}

}  // namespace pulse
