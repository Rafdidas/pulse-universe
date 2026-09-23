#pragma once

#include <string>

#include "core/Snapshot.h"

namespace pulse {

// 계약서 4.3~4.4 절 메시지의 v 필드.
inline constexpr int kProtocolVersion = 1;

struct HelloInfo {
    unsigned interval_ms = 1000;
    unsigned core_count = 0;
    bool elevated = false;
    std::string os;
    // M2 는 항상 추정이다. ETW 수집기가 들어오면 "measured" 가 된다.
    // core/Snapshot.h 의 Flow::source 가 도메인 계층에서 같은 값을 나른다 —
    // 이걸 바꾸면 그쪽도 같이 바꿔야 한다.
    std::string thread_mapping = "estimated";
};

// 계약서 4.3 절 형식.
std::string serializeHello(const HelloInfo& info);

// 계약서 4.4 절 형식. type 과 v 는 SystemSnapshot 에 없고 여기서 붙인다 —
// 구조체는 도메인 값이고 프로토콜 봉투는 전송 계층의 관심사다.
std::string serializeSnapshot(const SystemSnapshot& snapshot);

}  // namespace pulse
