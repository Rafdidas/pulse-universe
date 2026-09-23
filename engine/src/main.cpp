#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "core/DataAggregator.h"
#include "network/Serializer.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

int runDump(pulse::WindowsSystemReader& reader, const pulse::Options& options) {
    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    for (unsigned n = 0; options.iterations == 0 || n < options.iterations; ++n) {
        const pulse::SystemSnapshot snapshot = aggregator.aggregate(reader.read());

        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);

        const bool is_last = options.iterations != 0 && n + 1 == options.iterations;
        if (!is_last) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
    }
    return 0;
}

int runJson(pulse::WindowsSystemReader& reader, const pulse::Options& options) {
    pulse::AggregatorConfig config;
    config.filter.max_groups = options.max_groups;
    pulse::DataAggregator aggregator(reader.coreCount(), config);

    // 첫 표본은 CPU 델타가 없어 모든 cpu_pct 가 null 이다. 버린다.
    aggregator.aggregate(reader.read());
    std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));

    const pulse::SystemSnapshot snapshot = aggregator.aggregate(reader.read());
    std::printf("%s\n", pulse::serializeSnapshot(snapshot).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    pulse::Options options;
    std::string error;

    switch (pulse::parseOptions(argc, argv, options, error)) {
        case pulse::ParseResult::Ok:
            break;
        case pulse::ParseResult::ShowUsage:
            std::printf("%s", pulse::usageText().c_str());
            return 0;
        case pulse::ParseResult::Error:
            std::printf("%s\n\n%s", error.c_str(), pulse::usageText().c_str());
            return 2;
    }

    pulse::WindowsSystemReader reader;

    switch (options.mode) {
        case pulse::Mode::Dump:
            return runDump(reader, options);
        case pulse::Mode::Json:
            return runJson(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
