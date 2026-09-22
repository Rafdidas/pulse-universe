#pragma once

#include "platform/RawTypes.h"

namespace pulse {

// OS 접근의 유일한 경계. core/ 의 모든 로직은 이 뒤의 구현을 모른다.
class ISystemReader {
public:
    virtual ~ISystemReader() = default;
    virtual RawSample read() = 0;
    virtual unsigned coreCount() const = 0;
};

}  // namespace pulse
