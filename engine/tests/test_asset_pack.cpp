#include <catch2/catch_test_macros.hpp>

#include <string>

#include "network/AssetPack.h"
#include "pak_builder.h"
#include "platform/windows/EmbeddedAssets.h"

using pulse_test::makePak;

TEST_CASE("AssetPack finds every file by its path", "[assetpack]") {
    const std::string pak = makePak({{"/assets/a.js", "console.log(1)"},
                                     {"/index.html", "<html></html>"},
                                     {"/z.css", ""}});
    const auto pack = pulse::AssetPack::parse(pak);
    REQUIRE(pack.has_value());
    CHECK(pack->size() == 3);
    CHECK(pack->find("/index.html").value() == "<html></html>");
    CHECK(pack->find("/assets/a.js").value() == "console.log(1)");
    CHECK(pack->find("/z.css").value().empty());
    CHECK_FALSE(pack->find("/missing").has_value());
    CHECK_FALSE(pack->find("index.html").has_value());
}

TEST_CASE("AssetPack handles an empty pack and UTF-8 paths", "[assetpack]") {
    const auto empty = pulse::AssetPack::parse(makePak({}));
    REQUIRE(empty.has_value());
    CHECK(empty->size() == 0);
    CHECK_FALSE(empty->find("/index.html").has_value());

    const std::string pak = makePak({{"/\xed\x95\x9c\xea\xb8\x80.txt", "x"}});
    const auto pack = pulse::AssetPack::parse(pak);
    REQUIRE(pack.has_value());
    CHECK(pack->find("/\xed\x95\x9c\xea\xb8\x80.txt").value() == "x");
}

TEST_CASE("AssetPack rejects malformed input", "[assetpack]") {
    const std::string good = makePak({{"/a", "hello"}, {"/b", "world"}});
    REQUIRE(pulse::AssetPack::parse(good).has_value());

    SECTION("empty and too short") {
        CHECK_FALSE(pulse::AssetPack::parse("").has_value());
        CHECK_FALSE(pulse::AssetPack::parse(good.substr(0, 11)).has_value());
    }
    SECTION("wrong magic") {
        std::string bad = good;
        bad[0] = 'X';
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("truncated anywhere before the last file ends") {
        for (std::size_t n = 0; n < good.size(); ++n) {
            CHECK_FALSE(pulse::AssetPack::parse(good.substr(0, n)).has_value());
        }
    }
    SECTION("count larger than the table") {
        std::string bad = good;
        bad[8] = 5;
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("offset plus size past the end, including overflow") {
        std::string bad = good;
        // 첫 항목: path_len(2) "/a"(2) offset(8) size(8) 이므로 size 는 12 + 2 + 2 + 8 = 24 바이트 뒤.
        for (int i = 0; i < 8; ++i) {
            bad[8 + 4 + 2 + 2 + 8 + i] = static_cast<char>(0xff);
        }
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("offset alone past the end") {
        std::string bad = good;
        // offset 은 path_len(2) "/a"(2) 뒤, 12 + 2 + 2 = 16 바이트 위치의 8 바이트다. 최상위 바이트만 켠다.
        // size 는 그대로 5 이므로 offset 만 범위를 벗어난다.
        bad[8 + 4 + 2 + 2 + 7] = static_cast<char>(0x7f);
        CHECK_FALSE(pulse::AssetPack::parse(bad).has_value());
    }
    SECTION("paths out of order or duplicated") {
        CHECK_FALSE(pulse::AssetPack::parse(makePak({{"/b", "1"}, {"/a", "2"}})).has_value());
        CHECK_FALSE(pulse::AssetPack::parse(makePak({{"/a", "1"}, {"/a", "2"}})).has_value());
    }
}

TEST_CASE("the test executable embeds no frontend", "[assetpack]") {
    // 리소스는 pulse-engine.exe 에만 링크된다. 테스트 exe 에서는 없어야 한다 (D78).
    CHECK(pulse::embeddedAssetPack() == nullptr);
}
