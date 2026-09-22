#include "cli/TableFormatter.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace pulse {
namespace {

std::string formatOptionalPct(const std::optional<double>& value) {
    if (!value.has_value()) {
        return "-";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << *value;
    return out.str();
}

// UTF-8 연속 바이트(10xxxxxx) 한가운데에서 자르지 않는다.
// 그러지 않으면 한글 이름 프로그램에서 깨진 바이트가 출력된다.
std::string truncateUtf8(const std::string& text, size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return text;
    }
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

}  // namespace

std::string formatSnapshotTable(const SystemSnapshot& snapshot) {
    std::ostringstream out;
    out << std::fixed;

    out << "seq " << snapshot.seq << "   t " << snapshot.t << "\n";
    out << "cpu " << formatOptionalPct(snapshot.system.cpu_pct) << " %"
        << "   mem " << std::setprecision(0) << snapshot.system.mem_used_mb << " / "
        << snapshot.system.mem_total_mb << " MB"
        << "   processes " << snapshot.system.process_total << "   threads "
        << snapshot.system.thread_total << "\n";

    out << "cores ";
    for (const CoreLoad& core : snapshot.cores) {
        out << "[" << core.id << "] " << std::setprecision(1) << core.pct << "  ";
    }
    out << "\n\n";

    out << std::left << std::setw(28) << "GROUP" << std::right << std::setw(8) << "PID"
        << std::setw(8) << "PROCS" << std::setw(10) << "CPU%" << std::setw(12) << "MEM MB"
        << std::setw(9) << "THREADS" << "  ACCOUNT" << "\n";
    out << std::string(84, '-') << "\n";

    for (const ProcessGroup& group : snapshot.groups) {
        const std::string name = truncateUtf8(group.name, 27);
        out << std::left << std::setw(28) << name << std::right << std::setw(8)
            << group.root_pid << std::setw(8) << group.proc_count << std::setw(10)
            << formatOptionalPct(group.cpu_pct) << std::setw(12) << std::setprecision(1)
            << group.mem_mb << std::setw(9) << group.thread_count << "  "
            << (group.account == Account::User ? "user" : "system") << "\n";
    }

    out << "\nambient services  " << snapshot.ambient.service_proc_count
        << " procs   " << std::setprecision(0) << snapshot.ambient.service_mem_mb
        << " MB\n";

    if (!snapshot.lifecycle.spawned.empty() || !snapshot.lifecycle.terminated.empty()) {
        out << "lifecycle  spawned " << snapshot.lifecycle.spawned.size()
            << "   terminated " << snapshot.lifecycle.terminated.size() << "\n";
    }

    out << "flows " << snapshot.flows.size() << " (estimated)\n";

    return out.str();
}

}  // namespace pulse
