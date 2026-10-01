#pragma once

#include "platform/RawNetwork.h"

namespace pulse {

// IP Helper 의 소유 PID 표(GetExtendedTcpTable, GetExtendedUdpTable)를 IPv4·IPv6 로 읽는다.
// 관리자 권한이 필요 없다. M10 스펙 3절.
class WindowsConnectionScanner final : public IConnectionScanner {
public:
    ConnectionScan scan() override;
};

}  // namespace pulse
