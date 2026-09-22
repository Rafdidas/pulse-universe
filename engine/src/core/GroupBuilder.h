#pragma once

#include <vector>

#include "core/Snapshot.h"
#include "platform/RawTypes.h"

namespace pulse {

struct GroupingResult {
    std::vector<ProcessGroup> groups;
    Ambient ambient;
};

// 프로세스 목록을 트리 루트 기준으로 묶는다.
// 부모와 자식의 이미지 이름이 같으면 부모 쪽으로 계속 올라가고,
// 이름이 달라지는 지점에서 멈춘다. 특정 프로그램 이름을 하드코딩하지 않는다.
class GroupBuilder {
public:
    GroupingResult build(const std::vector<RawProcess>& processes) const;
};

}  // namespace pulse
