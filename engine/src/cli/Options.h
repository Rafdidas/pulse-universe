#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pulse {

enum class Mode { None, Dump, Json, Serve, Connections };

// 스레드-코어 매핑 출처 (ETW 스펙 8절). Auto 는 실측을 시도하고 안 되면 추정한다.
enum class Mapping { Auto, Estimated, Measured };

struct Options {
    Mode mode = Mode::None;
    unsigned interval_ms = 1000;
    unsigned iterations = 0;  // 0 이면 무한 반복
    size_t max_groups = 40;
    unsigned port = 9000;
    std::vector<std::string> allowed_origins;  // --serve 에서만 쓰인다.
    std::string web_root;
    bool use_embedded_web = false;  // --embedded-web. web_root 와 함께 쓰지 않는다.
    Mapping mapping = Mapping::Auto;
};

enum class ParseResult { Ok, ShowUsage, Error };

// argv 를 파싱한다. Ok 일 때만 out 을 덮어쓴다.
// 실패하면 error 에 사람이 읽을 이유를 담는다.
ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error);

// 릴리스 zip 설계 D70, 웹 내장 설계 D77. 인자 없이 exe 를 실행했을 때(더블클릭)의 설정이다.
// exe 옆에 프런트엔드 폴더가 있으면 그것을, 없고 내장본이 있으면 내장본을 서빙하는 --serve 로
// 동작한다. 둘 다 없으면 out 을 건드리지 않고 false 를 돌려주며, 호출자는 지금처럼
// 사용법을 출력한다.
bool applyDefaultLaunch(Options& out, const std::string& web_root, bool web_root_exists,
                        bool has_embedded);

std::string usageText();

}  // namespace pulse
