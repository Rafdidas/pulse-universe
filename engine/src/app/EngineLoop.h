#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "core/DataAggregator.h"
#include "core/Snapshot.h"
#include "platform/ISystemReader.h"

namespace pulse {

struct EngineLoopConfig {
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 stop() 까지 계속
    AggregatorConfig aggregator;
};

// 표본을 주기적으로 떠서 스냅샷으로 만들고 콜백에 넘긴다.
// WebSocket 도 콘솔도 모른다 — 콜백이 무엇을 하는지는 호출자의 일이다.
class EngineLoop {
public:
    using SnapshotHandler = std::function<void(const SystemSnapshot&)>;

    EngineLoop(ISystemReader& reader, EngineLoopConfig cfg, SnapshotHandler handler);

    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    // 호출한 스레드에서 루프를 돈다. stop() 이 불리거나 iterations 를 채우면
    // 돌아온다. 표본 수집 중 예외는 잡아 error() 에 담고 멈춘다 —
    // 샘플링 스레드에서는 프로세스를 끝낼 수 없기 때문이다.
    void run();

    // 다른 스레드에서 부를 수 있다.
    void stop();

    // run() 이 예외로 끝났으면 사람이 읽을 이유, 아니면 빈 문자열.
    // run() 을 도는 스레드만 이 값을 쓴다 — 그 스레드가 돌아왔거나 join 된
    // 뒤에만 읽어야 한다.
    const std::string& error() const;

private:
    // 정지 요청에 빨리 반응하도록 주기를 잘게 쪼개 잔다.
    void sleepInterval();

    ISystemReader& reader_;
    EngineLoopConfig cfg_;
    SnapshotHandler handler_;
    DataAggregator aggregator_;
    std::atomic<bool> stop_requested_{false};
    std::string error_;
};

}  // namespace pulse
