#include "core/GroupBuilder.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace pulse {
namespace {

constexpr int kMaxParentHops = 32;
constexpr double kBytesPerMb = 1024.0 * 1024.0;

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

bool isAmbientService(const RawProcess& p) {
    return equalsIgnoreCase(p.name, "svchost.exe");
}

double toMb(uint64_t bytes) {
    return static_cast<double>(bytes) / kBytesPerMb;
}

}  // namespace

GroupingResult GroupBuilder::build(const std::vector<RawProcess>& processes) const {
    GroupingResult result;

    std::unordered_map<uint32_t, const RawProcess*> by_pid;
    by_pid.reserve(processes.size());
    for (const RawProcess& p : processes) {
        if (isAmbientService(p)) {
            result.ambient.service_proc_count += 1;
            result.ambient.service_mem_mb += toMb(p.mem_bytes);
            continue;
        }
        by_pid.emplace(p.pid, &p);
    }

    // pid -> 그 프로세스가 속한 트리의 루트 pid
    const auto resolveRoot = [&](const RawProcess& start) -> uint32_t {
        const RawProcess* current = &start;
        std::unordered_set<uint32_t> visited;
        for (int hop = 0; hop < kMaxParentHops; ++hop) {
            if (!visited.insert(current->pid).second) {
                break;  // 순환. 현재 지점을 루트로 삼는다.
            }
            const auto parent_it = by_pid.find(current->ppid);
            if (parent_it == by_pid.end()) {
                break;  // 부모가 없거나 제외된 프로세스다.
            }
            const RawProcess* parent = parent_it->second;
            if (!equalsIgnoreCase(parent->name, current->name)) {
                break;  // 이름이 달라지는 지점이 그룹 경계다.
            }
            if (parent->start_time_ms > current->start_time_ms) {
                break;  // pid 재사용. 실제 부모가 아니다.
            }
            current = parent;
        }
        return current->pid;
    };

    // 루트별로 구성원을 모은다. 입력 순서를 보존해 결과를 결정적으로 유지한다.
    std::unordered_map<uint32_t, size_t> index_by_root;
    std::vector<std::vector<const RawProcess*>> members;
    std::vector<uint32_t> root_order;

    for (const RawProcess& p : processes) {
        if (isAmbientService(p)) {
            continue;
        }
        const uint32_t root = resolveRoot(p);
        const auto it = index_by_root.find(root);
        if (it == index_by_root.end()) {
            index_by_root.emplace(root, members.size());
            members.push_back({&p});
            root_order.push_back(root);
        } else {
            members[it->second].push_back(&p);
        }
    }

    result.groups.reserve(root_order.size());
    for (size_t i = 0; i < root_order.size(); ++i) {
        const uint32_t root_pid = root_order[i];
        const auto root_it = by_pid.find(root_pid);
        if (root_it == by_pid.end()) {
            continue;
        }
        const RawProcess& root = *root_it->second;

        ProcessGroup group;
        group.key = root.name + ":" + std::to_string(root.pid);
        group.name = root.name;
        group.root_pid = root.pid;
        group.started_at = root.start_time_ms;
        group.account = root.account;
        group.image_path = root.image_path;

        for (const RawProcess* member : members[i]) {
            group.mem_mb += toMb(member->mem_bytes);
            group.thread_count += member->thread_count;
            group.proc_count += 1;
            if (member->pid == root.pid) {
                continue;
            }
            ChildProcess child;
            child.pid = member->pid;
            child.name = member->name;
            child.mem_mb = toMb(member->mem_bytes);
            child.threads = member->thread_count;
            group.children.push_back(std::move(child));
        }

        result.groups.push_back(std::move(group));
    }

    return result;
}

}  // namespace pulse
