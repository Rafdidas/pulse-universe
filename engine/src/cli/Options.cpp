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
        "pulse-engine 0.2.0\n"
        "\n"
        "Usage:\n"
        "  pulse-engine --dump  [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "  pulse-engine --json  [--interval-ms N] [--max-groups N]\n"
        "  pulse-engine --serve [--port N] [--interval-ms N] [--iterations N] [--max-groups N]\n"
        "\n"
        "  --dump            Print a process group table every interval.\n"
        "  --json            Print one snapshot as contract-shaped JSON and exit.\n"
        "  --serve           Stream snapshots over WebSocket on 127.0.0.1.\n"
        "  --port N          Listen port for --serve (default 9000).\n"
        "  --allow-origin V  Allow an additional Origin for --serve (repeatable).\n"
        "  --interval-ms N   Sampling interval in milliseconds (default 1000, minimum 1).\n"
        "  --iterations N    Stop after N snapshots for --dump and --serve (default: run "
        "until Ctrl+C).\n"
        "  --max-groups N    Number of groups to show (default 40).\n";
}

ParseResult parseOptions(int argc, const char* const* argv, Options& out, std::string& error) {
    Options parsed;

    const auto setMode = [&](Mode mode, const char* flag) {
        if (parsed.mode != Mode::None) {
            error = std::string("only one mode may be given, saw ") + flag;
            return false;
        }
        parsed.mode = mode;
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            if (!setMode(Mode::Dump, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--json") == 0) {
            if (!setMode(Mode::Json, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--serve") == 0) {
            if (!setMode(Mode::Serve, arg)) {
                return ParseResult::Error;
            }
        } else if (std::strcmp(arg, "--port") == 0) {
            unsigned value = 0;
            if (i + 1 >= argc || !parseUnsigned(argv[++i], value) || value == 0 ||
                value > 65535) {
                error = "invalid --port";
                return ParseResult::Error;
            }
            parsed.port = value;
        } else if (std::strcmp(arg, "--allow-origin") == 0) {
            if (i + 1 >= argc) {
                error = "invalid --allow-origin";
                return ParseResult::Error;
            }
            parsed.allowed_origins.emplace_back(argv[++i]);
        } else if (std::strcmp(arg, "--interval-ms") == 0) {
            if (i + 1 >= argc || !parseUnsigned(argv[++i], parsed.interval_ms) ||
                // 0 을 허용하면 표본 사이에 잠들지 않는 바쁜 루프가 되어, 이 도구가
                // 측정하려는 바로 그 CPU 를 잡아먹는다. 게다가 시계가 전진하지 않아
                // CpuDelta 가 값을 내지 못해 모든 cpu_pct 가 '-' 로 나온다.
                parsed.interval_ms == 0) {
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

    if (parsed.mode == Mode::None) {
        return ParseResult::ShowUsage;
    }

    // 플래그는 어떤 순서로도 올 수 있으므로, 모드가 확정된 뒤인 여기서
    // 한 번에 검사한다. --json 은 늘 정확히 두 번 표본을 뜨므로
    // --iterations 는 조용히 무시하는 대신 거절한다.
    if (parsed.mode == Mode::Json && parsed.iterations != 0) {
        error = "--iterations cannot be combined with --json";
        return ParseResult::Error;
    }

    out = parsed;
    return ParseResult::Ok;
}

}  // namespace pulse
