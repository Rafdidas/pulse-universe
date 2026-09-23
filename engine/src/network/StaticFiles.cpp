#include "network/StaticFiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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
    const fs::path root = fs::absolute(fs::path(web_root)).lexically_normal();
    const fs::path joined = (root / fs::path(path.substr(1))).lexically_normal();

    // joined 가 root 아래가 아니면 거절한다.
    const std::string relative = joined.lexically_relative(root).generic_string();
    if (relative.empty() || relative == ".." || relative.rfind("../", 0) == 0) {
        return std::nullopt;
    }

    return joined.string();
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
