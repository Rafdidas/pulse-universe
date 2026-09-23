#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pulse {

enum class Mode { None, Dump, Json, Serve };

struct Options {
    Mode mode = Mode::None;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
    unsigned port = 9000;
    std::vector<std::string> allowed_origins;  // --serve 에서만 쓰인다.
};

enum class ParseResult { Ok, ShowUsage, Error };

// argv 를 파싱한다. Ok 일 때만 out 을 덮어쓴다.
// 실패하면 error 에 사람이 읽을 이유를 담는다.
ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error);

std::string usageText();

}  // namespace pulse
