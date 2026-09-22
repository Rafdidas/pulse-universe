#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "core/DataAggregator.h"
#include "platform/windows/WindowsSystemReader.h"

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
