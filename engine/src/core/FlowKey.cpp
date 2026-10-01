#include "core/FlowKey.h"

#include <tuple>

namespace pulse {

FlowKey makeFlowKey(NetProtocol protocol, uint32_t pid, const IpBytes& ip1, uint16_t port1, const IpBytes& ip2,
                    uint16_t port2) {
    FlowKey key;
    key.protocol = protocol;
    key.pid = pid;
    if (std::tie(ip1, port1) <= std::tie(ip2, port2)) {
        key.ip_a = ip1;
        key.port_a = port1;
        key.ip_b = ip2;
        key.port_b = port2;
    } else {
        key.ip_a = ip2;
        key.port_a = port2;
        key.ip_b = ip1;
        key.port_b = port1;
    }
    return key;
}

}  // namespace pulse
