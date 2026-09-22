#include "cli/Options.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pulse {
namespace {

// strtoul 은 앞의 '-' 를 받아들여 랩어라운드시킨다. 그래서 숫자만으로
// 이루어진 문자열인지 먼저 확인한다. 그러지 않으면 "-5" 가 42억이 된다.
bool parseUnsigned(const char* text, unsigned& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    for (const char* c = text; *c != '\0'; ++c) {
        if (std::isdigit(static_cast<unsigned char>(*c)) == 0) {
            return false;
        }
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    if (value > std::numeric_limits<unsigned>::max()) {
        return false;
    }

    out = static_cast<unsigned>(value);
    return true;
}

}  // namespace

std::string usageText() {
    return
        "pulse-engine 0.1.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000).\n"
        "  --iterations N    Stop after N snapshots (default: run until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n";
}

ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error) {
    Options parsed;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            parsed.dump = true;
        } else if (std::strcmp(arg, "--interval-ms") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.interval_ms) ||
                parsed.interval_ms < 1) {
                // 0 은 바쁜 대기(busy-spin) 루프가 되어, 이 도구가 측정하려는
                // 바로 그 CPU 를 잡아먹고 모든 cpu_pct 를 "-" 로 만든다.
                error = "invalid --interval-ms";
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--iterations") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.iterations)) {
                error = "invalid --iterations";
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--max-groups") == 0) {
            unsigned value = 0;
            if (i + 1 >= argc || !parseUnsigned(argv[++i], value)) {
                error = "invalid --max-groups";
                return ParseResult::Error;
            }
            parsed.max_groups = value;
        } else {
            error = std::string("unknown argument: ") + arg;
            return ParseResult::Error;
        }
    }

    if (!parsed.dump) {
        return ParseResult::ShowUsage;
    }

    out = parsed;
    return ParseResult::Ok;
}

}  // namespace pulse
