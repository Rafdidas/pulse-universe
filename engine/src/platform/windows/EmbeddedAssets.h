#pragma once

#include "network/AssetPack.h"

namespace pulse {

// exe 에 링크된 WEBPAK 리소스(RCDATA)를 읽은 AssetPack. 리소스가 없거나 pak 이 손상됐으면
// nullptr. 결과는 프로세스 수명 동안 유지되며 exe 이미지를 가리킨다.
const AssetPack* embeddedAssetPack();

}  // namespace pulse
