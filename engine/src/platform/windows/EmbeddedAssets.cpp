#include "platform/windows/EmbeddedAssets.h"

#include <windows.h>

#include <optional>
#include <string_view>

namespace pulse {
namespace {

std::optional<AssetPack> loadEmbedded() {
    const HRSRC resource = ::FindResourceW(nullptr, L"WEBPAK", reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (resource == nullptr) {
        return std::nullopt;
    }
    const DWORD size = ::SizeofResource(nullptr, resource);
    const HGLOBAL loaded = ::LoadResource(nullptr, resource);
    if (size == 0 || loaded == nullptr) {
        return std::nullopt;
    }
    const void* data = ::LockResource(loaded);
    if (data == nullptr) {
        return std::nullopt;
    }
    return AssetPack::parse(std::string_view(static_cast<const char*>(data), size));
}

}  // namespace

const AssetPack* embeddedAssetPack() {
    static const std::optional<AssetPack> pack = loadEmbedded();
    return pack ? &*pack : nullptr;
}

}  // namespace pulse
