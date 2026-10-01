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
    // 엔진이 시작할 때 ETW 수집기가 돌았으면 "measured", 아니면 "estimated"
    // (HostInfo::thread_mapping). core/Snapshot.h 의 Flow::source 는 스냅샷마다의
    // 실제 출처다 — 수집기가 살아 있는 동안 두 값은 같다 (ETW 스펙 8절).
    std::string thread_mapping = "estimated";
    // 엔진이 시작할 때 네트워크 트래픽 ETW 수집기가 돌았으면 "measured", 아니면 "unavailable"
    // (SystemSnapshot::network.traffic 과 같은 값이다 - M12 스펙 D124).
    std::string network_traffic = "unavailable";
    // 엔진 프로세스의 한 번의 실행을 식별한다. 재시작할 때마다 바뀐다.
    // 클라이언트는 이 값으로 재연결(같은 세션)과 재시작(다른 세션)을 구별한다.
    std::string session;
};

// 계약서 4.3 절 형식.
std::string serializeHello(const HelloInfo& info);

// 계약서 4.4 절 형식. type 과 v 는 SystemSnapshot 에 없고 여기서 붙인다 —
// 구조체는 도메인 값이고 프로토콜 봉투는 전송 계층의 관심사다.
std::string serializeSnapshot(const SystemSnapshot& snapshot);

}  // namespace pulse
