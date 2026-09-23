#include <cstdio>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <memory>
#include <thread>

#include "app/EngineLoop.h"
#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "network/Serializer.h"
#include "network/WebSocketServer.h"
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
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }

    std::printf("%s\n", pulse::serializeSnapshot(latest).c_str());
    return 0;
}

int runServe(pulse::ISystemReader& reader, const pulse::Options& options) {
    namespace net = boost::asio;

    net::io_context ioc;

    pulse::ServerConfig server_cfg;
    server_cfg.port = static_cast<unsigned short>(options.port);

    std::unique_ptr<pulse::WebSocketServer> server;
    try {
        server = std::make_unique<pulse::WebSocketServer>(ioc, server_cfg);
    } catch (const std::exception& e) {
        std::printf("cannot listen on 127.0.0.1:%u — %s\n", options.port, e.what());
        return 2;
    }

    pulse::HelloInfo hello;
    hello.interval_ms = options.interval_ms;
    hello.core_count = reader.coreCount();
    const pulse::HostInfo host = reader.hostInfo();
    hello.os = host.os;
    hello.elevated = host.elevated;
    server->setHello(
        std::make_shared<const std::string>(pulse::serializeHello(hello)));

    pulse::EngineLoopConfig loop_cfg;
    loop_cfg.interval_ms = options.interval_ms;
    loop_cfg.iterations = options.iterations;
    loop_cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::EngineLoop loop(reader, loop_cfg, [&](const pulse::SystemSnapshot& snapshot) {
        server->broadcast(
            std::make_shared<const std::string>(pulse::serializeSnapshot(snapshot)));
    });

    // Ctrl+C 로 종료한다. 신호는 io_context 에서 받고 샘플링 루프에 정지를 알린다.
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        loop.stop();
        server->stop();
    });

    std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                static_cast<unsigned>(server->port()));
    std::fflush(stdout);

    std::thread sampler([&] {
        loop.run();
        // 반복 횟수를 채웠거나 예외로 끝났으면 서버도 접는다.
        server->stop();
        ioc.stop();
    });

    ioc.run();
    loop.stop();
    sampler.join();

    if (!loop.error().empty()) {
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }
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
        case pulse::Mode::Serve:
            return runServe(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
