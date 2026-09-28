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

WebPath outside() {
    return WebPath{WebPathStatus::Outside, {}};
}

WebPath unresolvable() {
    return WebPath{WebPathStatus::Unresolvable, {}};
}

}  // namespace

WebPath resolveWebPath(const std::string& web_root, const std::string& target) {
    // 쿼리스트링과 프래그먼트를 떼어낸다.
    std::string path = target.substr(0, target.find_first_of("?#"));

    if (path.empty() || path.front() != '/') {
        return outside();
    }
    // Windows 에서 역슬래시는 경로 구분자다. 요청 경로에 오면 탈출 시도로 본다.
    if (path.find('\\') != std::string::npos) {
        return outside();
    }
    if (path == "/") {
        path = "/index.html";
    }

    namespace fs = std::filesystem;
    std::error_code abs_ec;
    const fs::path absolute_root = fs::absolute(fs::path(web_root), abs_ec);
    if (abs_ec) {
        return unresolvable();
    }
    const fs::path root = absolute_root.lexically_normal();
    const fs::path joined = (root / fs::path(path.substr(1))).lexically_normal();

    // joined 가 root 아래가 아니면 거절한다.
    const std::string relative = joined.lexically_relative(root).generic_string();
    if (relative.empty() || relative == ".." || relative.rfind("../", 0) == 0) {
        return outside();
    }

    // 위 검사는 순수하게 어휘적이라 정션과 심볼릭 링크를 보지 못한다.
    // web_root 안에 루트 밖을 가리키는 링크가 있으면 그대로 통과하므로,
    // 실제 경로로 풀어 한 번 더 확인한다.
    //
    // weakly_canonical 은 존재하지 않는 경로를 오류로 보지 않는다 — 깨진
    // (대상이 지워진) 정션은 여기서 에러가 나지 않고 그냥 "없는 경로" 로
    // 풀린다. 이건 무해하다: 그 경로는 디스크에 없으므로 호출자의 존재
    // 확인에서 걸러진다.
    //
    // 여기서 실제로 에러가 나는 경우는: 자기 자신을 (또는 서로를) 가리켜
    // 순환하는 reparse point, 중간 경로 컴포넌트에 대한 접근 거부, 혹은
    // `a:b` 처럼 대체 데이터 스트림 구문으로 해석되어 이름을 구문 분석할
    // 수 없는 경우다. 이런 입력은 루트 안인지 밖인지 판단할 수 없으므로
    // Unresolvable 로 거절한다 — 어휘 검사로 되돌아가면 링크를 보지 못하므로
    // 그렇게 하지 않는다.
    std::error_code resolve_ec;
    const fs::path real_root = fs::weakly_canonical(root, resolve_ec);
    if (resolve_ec) {
        return unresolvable();
    }

    const fs::path real_target = fs::weakly_canonical(joined, resolve_ec);
    if (resolve_ec) {
        return unresolvable();
    }

    const std::string real_relative = real_target.lexically_relative(real_root).generic_string();
    if (real_relative.empty() || real_relative == ".." || real_relative.rfind("../", 0) == 0) {
        return outside();
    }

    return WebPath{WebPathStatus::Ok, real_target.string()};
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
