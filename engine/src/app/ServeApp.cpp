#include "app/ServeApp.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>

#include <memory>
#include <thread>

#include "app/EngineLoop.h"
#include "network/Serializer.h"
#include "network/WebSocketServer.h"

namespace pulse {

ServeResult runServe(ISystemReader& reader, const ServeConfig& cfg,
                     std::function<void(unsigned short)> on_listening) {
    namespace net = boost::asio;

    net::io_context ioc;

    std::unique_ptr<WebSocketServer> server;
    try {
        server = std::make_unique<WebSocketServer>(ioc, cfg.server);
    } catch (const std::exception& e) {
        return {2, "cannot listen on 127.0.0.1:" + std::to_string(cfg.server.port) +
                        " — " + e.what()};
    }

    if (on_listening) {
        on_listening(server->port());
    }

    HelloInfo hello;
    hello.interval_ms = cfg.interval_ms;
    hello.core_count = reader.coreCount();
    const HostInfo host = reader.hostInfo();
    hello.os = host.os;
    hello.elevated = host.elevated;
    server->setHello(std::make_shared<const std::string>(serializeHello(hello)));

    EngineLoopConfig loop_cfg;
    loop_cfg.interval_ms = cfg.interval_ms;
    loop_cfg.iterations = cfg.iterations;
    loop_cfg.aggregator.filter.max_groups = cfg.max_groups;

    EngineLoop loop(reader, loop_cfg, [&](const SystemSnapshot& snapshot) {
        server->broadcast(
            std::make_shared<const std::string>(serializeSnapshot(snapshot)));
    });

    // Ctrl+C 로 종료한다. 신호는 io_context 에서 받고 샘플링 루프에 정지를 알린다.
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        loop.stop();
        server->stop();
    });

    std::thread sampler([&] {
        loop.run();

        // ioc.stop() 을 부르지 않는다. run() 은 남은 작업이 없을 때 돌아오고,
        // post 된 채 아직 실행되지 않은 핸들러도 작업으로 친다. 따라서 stop()
        // 이 post 한 세션 정리는 반드시 실행된 뒤에야 run() 이 돌아온다.
        // io_context 를 살려두는 것은 signal_set 의 대기뿐이므로 그것만 취소한다.
        server->stop();
        net::post(ioc, [&] {
            boost::system::error_code ignored;
            signals.cancel(ignored);
        });
    });

    ioc.run();
    loop.stop();
    sampler.join();

    if (!loop.error().empty()) {
        return {1, "sampling failed: " + loop.error()};
    }
    return {0, ""};
}

}  // namespace pulse
