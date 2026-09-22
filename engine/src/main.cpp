#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "cli/TableFormatter.h"
#include "core/DataAggregator.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

struct Options {
    bool dump = false;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
};

void printUsage() {
    std::printf(
        "pulse-engine 0.1.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000).\n"
        "  --iterations N    Stop after N snapshots (default: run until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n");
}

bool parseUnsigned(const char* text, unsigned& out) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    out = static_cast<unsigned>(value);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            options.dump = true;
        } else if (std::strcmp(arg, "--interval-ms") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.interval_ms)) {
                std::printf("invalid --interval-ms\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--iterations") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.iterations)) {
                std::printf("invalid --iterations\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--max-groups") == 0 && i + 1 < argc) {
            unsigned value = 0;
            if (!parseUnsigned(argv[++i], value)) {
                std::printf("invalid --max-groups\n");
                return 2;
            }
            options.max_groups = value;
        } else {
            printUsage();
            return 2;
        }
    }

    if (!options.dump) {
        printUsage();
        return 0;
    }

    pulse::WindowsSystemReader reader;

    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    for (unsigned n = 0; options.iterations == 0 || n < options.iterations; ++n) {
        const pulse::RawSample sample = reader.read();
        const pulse::SystemSnapshot snapshot = aggregator.aggregate(sample);

        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
    }

    return 0;
}
