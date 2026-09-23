#pragma once

#include <string>
#include <vector>

namespace pulse {

struct ServerConfig {
    // 0 이면 OS 가 임시 포트를 고른다. 테스트에서 쓴다.
    unsigned short port = 9000;

    // 브라우저가 보내는 Origin 헤더의 허용 목록.
    // Origin 이 아예 없는 연결(네이티브 클라이언트, 테스트)은 허용한다 —
    // Origin 검사는 브라우저발 교차 출처 접근을 막는 장치이고,
    // 네이티브 접근은 127.0.0.1 바인딩으로 이미 통제된다.
    std::vector<std::string> allowed_origins = {
        "http://localhost:5173",
        "http://127.0.0.1:5173",
    };
};

}  // namespace pulse
