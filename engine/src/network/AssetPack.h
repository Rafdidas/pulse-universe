#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace pulse {

// web.pak 를 읽는다. 포맷은 docs/superpowers/specs/2026-09-30-embedded-web-design.md 3절.
// 바이트를 복사하지 않는다 — parse 에 넘긴 버퍼가 AssetPack 보다 오래 살아야 한다.
class AssetPack {
public:
    // magic·개수·범위·경로 정렬(오름차순, 중복 없음)을 모두 검증한다. 하나라도 어긋나면
    // nullopt — 잘린 pak 이 일부만 서빙되는 일이 없다.
    static std::optional<AssetPack> parse(std::string_view bytes);

    // "/index.html" 처럼 선행 '/' 가 있는 경로로 찾는다. 없으면 nullopt.
    std::optional<std::string_view> find(std::string_view path) const;

    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string_view path;
        std::string_view data;
    };
    std::vector<Entry> entries_;
};

}  // namespace pulse
