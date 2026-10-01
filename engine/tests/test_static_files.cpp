#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "network/AssetPack.h"
#include "network/StaticFiles.h"
#include "pak_builder.h"

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

    REQUIRE(resolved.status == WebPathStatus::Ok);
    REQUIRE(endsWith(resolved.path, "index.html"));
}

TEST_CASE("a normal file resolves under the root", "[static]") {
    const auto resolved = resolveWebPath(kRoot, "/assets/app.js");

    REQUIRE(resolved.status == WebPathStatus::Ok);
    REQUIRE(endsWith(resolved.path, "app.js"));
    REQUIRE(resolved.path.find("web") != std::string::npos);
}

TEST_CASE("a query string is stripped", "[static]") {
    const auto resolved = resolveWebPath(kRoot, "/app.js?v=123");

    REQUIRE(resolved.status == WebPathStatus::Ok);
    REQUIRE(endsWith(resolved.path, "app.js"));
}

TEST_CASE("a path escaping the root is refused", "[static]") {
    // 이 프로세스는 관리자 권한으로 도는 일이 많다.
    REQUIRE(resolveWebPath(kRoot, "/../secret.txt").status == WebPathStatus::Outside);
    REQUIRE(resolveWebPath(kRoot, "/a/../../secret.txt").status == WebPathStatus::Outside);
    REQUIRE(resolveWebPath(kRoot, "/..").status == WebPathStatus::Outside);
}

TEST_CASE("a backslash in the request path is refused", "[static]") {
    // Windows 에서 역슬래시는 경로 구분자다. 요청 경로에 오면 탈출 시도로 본다.
    REQUIRE(resolveWebPath(kRoot, "/..\\secret.txt").status == WebPathStatus::Outside);
    REQUIRE(resolveWebPath(kRoot, "\\secret.txt").status == WebPathStatus::Outside);
}

TEST_CASE("a target that is not an absolute path is refused", "[static]") {
    REQUIRE(resolveWebPath(kRoot, "").status == WebPathStatus::Outside);
    REQUIRE(resolveWebPath(kRoot, "app.js").status == WebPathStatus::Outside);
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

    REQUIRE(resolved.status == WebPathStatus::Ok);
    REQUIRE(fs::path(resolved.path).filename() == "index.html");

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

    REQUIRE(escaped.status == WebPathStatus::Outside);
    REQUIRE(inside.status == WebPathStatus::Ok);
}

TEST_CASE("a looping junction is unresolvable, not silently accepted", "[static]") {
    // 자기 자신을 가리키는 정션 생성은 이 환경에서 대상이 아직 없다는 이유로
    // 조용히 실패할 수 있다 (측정됨) — 그래서 서로를 가리키는 두 정션
    // a -> b, b -> a 로 순환을 만든다. weakly_canonical 이 이 순환을 풀다가
    // 에러를 내야 한다.
    namespace fs = std::filesystem;
    std::error_code ignored;

    const fs::path base = fs::temp_directory_path() / "pulse-static-loop";
    const fs::path root = base / "webroot";
    fs::remove_all(base, ignored);
    fs::create_directories(root, ignored);

    // b 를 먼저 평범한 디렉터리로 만들어 a -> b 생성이 성립하게 한 다음,
    // b 를 지우고 b -> a 정션으로 바꿔치기해 순환을 완성한다.
    fs::create_directories(root / "b", ignored);
    const int created_a =
        std::system(("cmd /c mklink /J \"" + (root / "a").string() + "\" \"" +
                     (root / "b").string() + "\" >nul 2>&1")
                        .c_str());
    REQUIRE(created_a == 0);

    fs::remove(root / "b", ignored);
    const int created_b =
        std::system(("cmd /c mklink /J \"" + (root / "b").string() + "\" \"" +
                     (root / "a").string() + "\" >nul 2>&1")
                        .c_str());
    REQUIRE(created_b == 0);

    const auto resolved = resolveWebPath(root.string(), "/a/x");

    fs::remove_all(base, ignored);

    REQUIRE(resolved.status == WebPathStatus::Unresolvable);
    REQUIRE(resolved.status != WebPathStatus::Ok);
}

TEST_CASE("a broken junction is caught by the caller's existence check, not misresolved", "[static]") {
    // 대상이 지워진 정션은 weakly_canonical 에서 에러가 나지 않는다 — MSVC 는
    // 매달린 reparse point 를 "없음" 으로 본다 (측정됨). Outside 로 잘못
    // 분류되지 않고, Ok 라면 그 경로가 디스크에 없어야 한다.
    namespace fs = std::filesystem;
    std::error_code ignored;

    const fs::path base = fs::temp_directory_path() / "pulse-static-broken";
    const fs::path root = base / "webroot";
    const fs::path target = base / "target";
    fs::remove_all(base, ignored);
    fs::create_directories(root, ignored);
    fs::create_directories(target, ignored);

    const int created =
        std::system(("cmd /c mklink /J \"" + (root / "broken").string() + "\" \"" +
                     target.string() + "\" >nul 2>&1")
                        .c_str());
    REQUIRE(created == 0);

    // 대상을 지워 정션을 깨뜨린다.
    fs::remove_all(target, ignored);

    const auto resolved = resolveWebPath(root.string(), "/broken/secret.txt");

    fs::remove_all(base, ignored);

    REQUIRE(resolved.status != WebPathStatus::Outside);
    if (resolved.status == WebPathStatus::Ok) {
        std::error_code exists_ec;
        REQUIRE_FALSE(fs::exists(resolved.path, exists_ec));
    }
}

TEST_CASE("a colon-style segment is not treated as an escape", "[static]") {
    // Windows 는 최상위 세그먼트의 콜론을 대체 데이터 스트림 구문으로 본다.
    // 이 프로젝트의 그룹 키는 "chrome.exe:1234" 처럼 생겼으므로, 클라이언트
    // 라우트가 이런 값을 담으면 새로고침 시 이 경로로 요청이 온다.
    const auto resolved = resolveWebPath(kRoot, "/chrome.exe:1234");

    REQUIRE(resolved.status != WebPathStatus::Outside);
}

namespace {

// 스펙 3절 pak. 경로는 오름차순.
std::string samplePak() {
    return pulse_test::makePak({{"/assets/app.js", "console.log(1);"},
                                {"/index.html", "<html>index</html>"}});
}

}  // namespace

TEST_CASE("a pack lookup finds an asset by its path", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    const PackAsset asset = lookupPackAsset(*pack, "/assets/app.js");

    REQUIRE(asset.status == PackLookupStatus::Found);
    REQUIRE(asset.path == "/assets/app.js");
    REQUIRE(asset.data == "console.log(1);");
}

TEST_CASE("a pack lookup maps the root to index.html and strips the query", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    REQUIRE(lookupPackAsset(*pack, "/").path == "/index.html");
    REQUIRE(lookupPackAsset(*pack, "/assets/app.js?v=3#x").path == "/assets/app.js");
}

TEST_CASE("a pack lookup falls back to index.html for unknown and colon paths", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    for (const char* target : {"/universe", "/app.js:stream", "/assets/"}) {
        const PackAsset asset = lookupPackAsset(*pack, target);
        REQUIRE(asset.status == PackLookupStatus::Found);
        REQUIRE(asset.path == "/index.html");
        REQUIRE(asset.data == "<html>index</html>");
    }
}

TEST_CASE("a pack lookup refuses malformed and escaping targets", "[static][pack]") {
    const std::string bytes = samplePak();
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    for (const char* target : {"", "index.html", "/..", "/../x", "/a/../index.html",
                               "/assets\\app.js"}) {
        REQUIRE(lookupPackAsset(*pack, target).status == PackLookupStatus::Forbidden);
    }
}

TEST_CASE("a pack without index.html answers 404 for unknown paths", "[static][pack]") {
    const std::string bytes = pulse_test::makePak({{"/only.txt", "x"}});
    const auto pack = AssetPack::parse(bytes);
    REQUIRE(pack.has_value());

    REQUIRE(lookupPackAsset(*pack, "/only.txt").status == PackLookupStatus::Found);
    REQUIRE(lookupPackAsset(*pack, "/missing").status == PackLookupStatus::NotFound);
}
