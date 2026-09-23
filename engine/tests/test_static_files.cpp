#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "network/StaticFiles.h"

using namespace pulse;

namespace {

const std::string kRoot = "C:/web/dist";

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST_CASE("the root path resolves to index.html", "[static]") {
    const auto resolved = resolveWebPath(kRoot, "/");

    REQUIRE(resolved.has_value());
    REQUIRE(endsWith(*resolved, "index.html"));
}

TEST_CASE("a normal file resolves under the root", "[static]") {
    const auto resolved = resolveWebPath(kRoot, "/assets/app.js");

    REQUIRE(resolved.has_value());
    REQUIRE(endsWith(*resolved, "app.js"));
    REQUIRE(resolved->find("web") != std::string::npos);
}

TEST_CASE("a query string is stripped", "[static]") {
    const auto resolved = resolveWebPath(kRoot, "/app.js?v=123");

    REQUIRE(resolved.has_value());
    REQUIRE(endsWith(*resolved, "app.js"));
}

TEST_CASE("a path escaping the root is refused", "[static]") {
    // 이 프로세스는 관리자 권한으로 도는 일이 많다.
    REQUIRE_FALSE(resolveWebPath(kRoot, "/../secret.txt").has_value());
    REQUIRE_FALSE(resolveWebPath(kRoot, "/a/../../secret.txt").has_value());
    REQUIRE_FALSE(resolveWebPath(kRoot, "/..").has_value());
}

TEST_CASE("a backslash in the request path is refused", "[static]") {
    // Windows 에서 역슬래시는 경로 구분자다. 요청 경로에 오면 탈출 시도로 본다.
    REQUIRE_FALSE(resolveWebPath(kRoot, "/..\\secret.txt").has_value());
    REQUIRE_FALSE(resolveWebPath(kRoot, "\\secret.txt").has_value());
}

TEST_CASE("a target that is not an absolute path is refused", "[static]") {
    REQUIRE_FALSE(resolveWebPath(kRoot, "").has_value());
    REQUIRE_FALSE(resolveWebPath(kRoot, "app.js").has_value());
}

TEST_CASE("mime types cover what vite emits", "[static]") {
    REQUIRE(mimeTypeFor("index.html") == "text/html");
    REQUIRE(mimeTypeFor("app.js") == "text/javascript");
    REQUIRE(mimeTypeFor("app.css") == "text/css");
    REQUIRE(mimeTypeFor("data.json") == "application/json");
    REQUIRE(mimeTypeFor("icon.svg") == "image/svg+xml");
    REQUIRE(mimeTypeFor("icon.png") == "image/png");
    REQUIRE(mimeTypeFor("font.woff2") == "font/woff2");
}

TEST_CASE("an unknown extension falls back to a binary type", "[static]") {
    REQUIRE(mimeTypeFor("thing.bin") == "application/octet-stream");
    REQUIRE(mimeTypeFor("noextension") == "application/octet-stream");
}

TEST_CASE("mime lookup ignores case", "[static]") {
    REQUIRE(mimeTypeFor("INDEX.HTML") == "text/html");
}

TEST_CASE("a resolved path stays inside a real web root", "[static]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pulse-static-ok";
    fs::create_directories(root);
    {
        std::ofstream out(root / "index.html", std::ios::binary);
        out << "<html></html>";
    }

    const auto resolved = resolveWebPath(root.string(), "/index.html");

    REQUIRE(resolved.has_value());
    REQUIRE(fs::path(*resolved).filename() == "index.html");

    std::error_code ignored;
    fs::remove_all(root, ignored);
}

TEST_CASE("a junction pointing outside the root is refused", "[static]") {
    // 어휘적 검사만으로는 reparse point 를 볼 수 없다. 정션은 권한 없이
    // 만들 수 있으므로 심볼릭 링크와 달리 어느 기계에서나 검증된다.
    namespace fs = std::filesystem;
    std::error_code ignored;

    const fs::path base = fs::temp_directory_path() / "pulse-static-junction";
    const fs::path root = base / "webroot";
    const fs::path outside = base / "outside";
    fs::remove_all(base, ignored);
    fs::create_directories(root, ignored);
    fs::create_directories(outside, ignored);
    {
        std::ofstream out(outside / "secret.txt", std::ios::binary);
        out << "secret";
    }

    const std::string command =
        "cmd /c mklink /J \"" + (root / "escape").string() + "\" \"" + outside.string() +
        "\" >nul 2>&1";
    const int created = std::system(command.c_str());

    const bool have_junction = created == 0 && fs::exists(root / "escape", ignored);
    REQUIRE(have_junction);

    // 정션을 통해 디스크상으로는 실제로 읽힌다 — 그래서 검사가 필요하다.
    REQUIRE(fs::exists(root / "escape" / "secret.txt", ignored));

    const auto escaped = resolveWebPath(root.string(), "/escape/secret.txt");
    const auto inside = resolveWebPath(root.string(), "/index.html");

    fs::remove_all(base, ignored);

    REQUIRE_FALSE(escaped.has_value());
    REQUIRE(inside.has_value());
}
