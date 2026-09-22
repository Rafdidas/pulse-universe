#pragma once

#include <string>

#include "core/Snapshot.h"

namespace pulse {

// 스냅샷을 작업 관리자와 대조하기 좋은 콘솔 표로 만든다.
std::string formatSnapshotTable(const SystemSnapshot& snapshot);

}  // namespace pulse
