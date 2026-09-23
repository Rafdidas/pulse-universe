#include <cstdio>
#include <string>

#include "app/EngineLoop.h"
#include "app/ServeApp.h"
#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "network/Serializer.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

int runDump(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::EngineLoop loop(reader, cfg, [](const pulse::SystemSnapshot& snapshot) {
        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);
    });
    loop.run();

    if (!loop.error().empty()) {
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }
    return 0;
}

int runJson(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = 2;  // 첫 스냅샷은 CPU 델타가 없어 버린다 — 계약서 6.1 절
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::SystemSnapshot latest;
    pulse::EngineLoop loop(reader, cfg,
                           [&](const pulse::SystemSnapshot& s) { latest = s; });
    loop.run();

    if (!loop.error().empty()) {
        std::fprintf(stderr, "sampling failed: %s\n", loop.error().c_str());
        return 1;
    }

    std::printf("%s\n", pulse::serializeSnapshot(latest).c_str());
    return 0;
}

int runServe(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::ServeConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.max_groups = options.max_groups;
    cfg.server.port = static_cast<unsigned short>(options.port);
    if (!options.allowed_origins.empty()) {
        for (const auto& origin : options.allowed_origins) {
            cfg.server.allowed_origins.push_back(origin);
        }
    }

    const pulse::ServeResult result =
        pulse::runServe(reader, cfg, [](unsigned short port) {
            std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                        static_cast<unsigned>(port));
            std::fflush(stdout);
        });

    if (!result.message.empty()) {
        std::fprintf(stderr, "%s\n", result.message.c_str());
    }
    return result.exit_code;
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
        case pulse::Mode::Serve:
            return runServe(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
