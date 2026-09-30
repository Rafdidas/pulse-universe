#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "app/EngineLoop.h"
#include "app/ServeApp.h"
#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "network/Serializer.h"
#include "platform/windows/EtwSchedulerCollector.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

// Ctrl+C·창 닫기·로그오프로 죽으면 소멸자가 돌지 못해 커널 세션이 남는다. 계속 이벤트를
// 쌓는 세션을 두지 않도록 여기서 이름으로 멈춘다. FALSE 를 돌려 기본 종료 동작은 그대로 둔다.
// --serve 의 asio signal_set 이 Ctrl+C 를 먼저 가로채면 이 핸들러는 불리지 않고, 그
// 경로는 정상 종료하며 소멸자가 세션을 멈춘다.
BOOL WINAPI onConsoleControl(DWORD) {
    pulse::EtwSchedulerCollector::stopSessionByName();
    return FALSE;
}

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
    if (!options.web_root.empty()) {
        std::error_code dir_ec;
        if (!std::filesystem::is_directory(options.web_root, dir_ec) || dir_ec) {
            std::fprintf(stderr, "web root is not a directory: %s\n",
                         options.web_root.c_str());
            return 2;
        }
        cfg.server.web_root = options.web_root;
    }

    const bool has_web_root = !cfg.server.web_root.empty();
    const pulse::ServeResult result =
        pulse::runServe(reader, cfg, [has_web_root](unsigned short port) {
            std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                        static_cast<unsigned>(port));
            if (has_web_root) {
                std::printf("open http://127.0.0.1:%u/ in a browser\n",
                            static_cast<unsigned>(port));
            }
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

    // ETW 스펙 8절. auto 와 measured 는 실측을 시도한다. 결과는 stderr 에 한 줄 남긴다 —
    // --json 의 stdout 을 더럽히지 않는다.
    const bool want_measured = options.mapping != pulse::Mapping::Estimated;
    pulse::WindowsSystemReader reader(want_measured);
    if (want_measured) {
        if (reader.measuringThreads()) {
            ::SetConsoleCtrlHandler(onConsoleControl, TRUE);
            std::fprintf(stderr, "thread mapping: measured (ETW)\n");
        } else if (options.mapping == pulse::Mapping::Measured) {
            std::fprintf(stderr, "thread mapping: cannot measure - %s\n",
                         reader.mappingError().c_str());
            return 1;
        } else {
            std::fprintf(stderr, "thread mapping: estimated - %s\n",
                         reader.mappingError().c_str());
        }
    }

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
