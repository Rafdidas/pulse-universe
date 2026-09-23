#include "network/StaticFiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace pulse {
namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string extensionOf(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    return toLower(path.substr(dot));
}

}  // namespace

std::optional<std::string> resolveWebPath(const std::string& web_root,
                                          const std::string& target) {
    // 쿼리스트링과 프래그먼트를 떼어낸다.
    std::string path = target.substr(0, target.find_first_of("?#"));

    if (path.empty() || path.front() != '/') {
        return std::nullopt;
    }
    // Windows 에서 역슬래시는 경로 구분자다. 요청 경로에 오면 탈출 시도로 본다.
    if (path.find('\\') != std::string::npos) {
        return std::nullopt;
    }
    if (path == "/") {
        path = "/index.html";
    }

    namespace fs = std::filesystem;
    std::error_code abs_ec;
    const fs::path absolute_root = fs::absolute(fs::path(web_root), abs_ec);
    if (abs_ec) {
        return std::nullopt;
    }
    const fs::path root = absolute_root.lexically_normal();
    const fs::path joined = (root / fs::path(path.substr(1))).lexically_normal();

    // joined 가 root 아래가 아니면 거절한다.
    const std::string relative = joined.lexically_relative(root).generic_string();
    if (relative.empty() || relative == ".." || relative.rfind("../", 0) == 0) {
        return std::nullopt;
    }

    // 위 검사는 순수하게 어휘적이라 정션과 심볼릭 링크를 보지 못한다.
    // web_root 안에 루트 밖을 가리키는 링크가 있으면 그대로 통과하므로,
    // 실제 경로로 풀어 한 번 더 확인한다.
    //
    // weakly_canonical 은 존재하지 않는 경로를 오류로 보지 않는다. 여기서
    // 오류가 났다면 깨졌거나 순환하는 reparse point, 혹은 접근이 막힌 중간
    // 경로다 — 바로 이 검사가 있어야 할 입력이다. 어휘 검사로 되돌아가면
    // 링크를 보지 못하므로 거절한다.
    std::error_code resolve_ec;
    const fs::path real_root = fs::weakly_canonical(root, resolve_ec);
    if (resolve_ec) {
        return std::nullopt;
    }

    const fs::path real_target = fs::weakly_canonical(joined, resolve_ec);
    if (resolve_ec) {
        return std::nullopt;
    }

    const std::string real_relative = real_target.lexically_relative(real_root).generic_string();
    if (real_relative.empty() || real_relative == ".." || real_relative.rfind("../", 0) == 0) {
        return std::nullopt;
    }

    return real_target.string();
}

std::string mimeTypeFor(const std::string& path) {
    const std::string extension = extensionOf(path);

    if (extension == ".html" || extension == ".htm") return "text/html";
    if (extension == ".js" || extension == ".mjs") return "text/javascript";
    if (extension == ".css") return "text/css";
    if (extension == ".json") return "application/json";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".woff2") return "font/woff2";

    return "application/octet-stream";
}

}  // namespace pulse
