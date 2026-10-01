#include "network/AssetPack.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace pulse {
namespace {

constexpr std::string_view kMagic("PLSPAK1\0", 8);

// 리틀 엔디언 정수를 범위 검사와 함께 읽는다. 범위를 넘으면 false.
template <typename T>
bool readLe(std::string_view bytes, std::size_t& cursor, T& out) {
    if (bytes.size() - cursor < sizeof(T) || cursor > bytes.size()) {
        return false;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<unsigned char>(bytes[cursor + i])) << (8 * i);
    }
    cursor += sizeof(T);
    out = value;
    return true;
}

}  // namespace

std::optional<AssetPack> AssetPack::parse(std::string_view bytes) {
    if (bytes.size() < kMagic.size() + 4 || bytes.substr(0, kMagic.size()) != kMagic) {
        return std::nullopt;
    }
    std::size_t cursor = kMagic.size();
    std::uint32_t count = 0;
    if (!readLe(bytes, cursor, count)) {
        return std::nullopt;
    }

    AssetPack pack;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t path_len = 0;
        if (!readLe(bytes, cursor, path_len) || bytes.size() - cursor < path_len) {
            return std::nullopt;
        }
        const std::string_view path = bytes.substr(cursor, path_len);
        cursor += path_len;

        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        if (!readLe(bytes, cursor, offset) || !readLe(bytes, cursor, size)) {
            return std::nullopt;
        }
        // offset + size 가 넘치지 않게 뺄셈으로 비교한다.
        if (offset > bytes.size() || size > bytes.size() - offset) {
            return std::nullopt;
        }
        // find 가 이분 탐색을 하므로 경로는 엄격한 오름차순이어야 한다 (중복도 거부).
        if (!pack.entries_.empty() && !(pack.entries_.back().path < path)) {
            return std::nullopt;
        }
        pack.entries_.push_back(
            Entry{path, bytes.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(size))});
    }
    return pack;
}

std::optional<std::string_view> AssetPack::find(std::string_view path) const {
    const auto it = std::lower_bound(
        entries_.begin(), entries_.end(), path,
        [](const Entry& entry, std::string_view key) { return entry.path < key; });
    if (it == entries_.end() || it->path != path) {
        return std::nullopt;
    }
    return it->data;
}

}  // namespace pulse
