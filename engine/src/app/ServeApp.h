#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "network/ServerConfig.h"
#include "platform/ISystemReader.h"

namespace pulse {

struct ServeConfig {
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 신호가 올 때까지
    std::size_t max_groups = 40;
    ServerConfig server;
};

struct ServeResult {
    int exit_code = 0;
    // 실패 이유. 성공이면 빈 문자열.
    std::string message;
};

// 서버를 띄우고 샘플링 루프를 돌린다. iterations 를 채우거나 SIGINT/SIGTERM 이
// 오면 돌아온다. 호출한 스레드를 점유한다.
//
// on_listening 은 바인딩 직후 실제 포트와 함께 정확히 한 번 불린다. 포트 0 을
// 요청한 테스트가 접속할 주소를 알 수 있게 하는 용도다. 비어 있어도 된다.
ServeResult runServe(ISystemReader& reader, const ServeConfig& cfg,
                     std::function<void(unsigned short)> on_listening = {});

}  // namespace pulse
