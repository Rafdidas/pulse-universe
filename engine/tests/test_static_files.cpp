#include <catch2/catch_test_macros.hpp>

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
