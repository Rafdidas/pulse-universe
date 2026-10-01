#include "core/NetworkEvents.h"

#include <cstring>

namespace pulse {
namespace {

struct Kind {
    NetProtocol protocol;
    bool v6;
    bool sent;
};

std::optional<Kind> kindOf(uint16_t id) {
    switch (id) {
        case 10: return Kind{NetProtocol::Tcp, false, true};
        case 11: return Kind{NetProtocol::Tcp, false, false};
        case 26: return Kind{NetProtocol::Tcp, true, true};
        case 27: return Kind{NetProtocol::Tcp, true, false};
        case 42: return Kind{NetProtocol::Udp, false, true};
        case 43: return Kind{NetProtocol::Udp, false, false};
        case 58: return Kind{NetProtocol::Udp, true, true};
        case 59: return Kind{NetProtocol::Udp, true, false};
        default: return std::nullopt;
    }
}

uint32_t readU32(const unsigned char* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// 네트워크 바이트 순서 (빅 엔디언) 포트.
uint16_t readPort(const unsigned char* data, size_t offset) {
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

IpBytes readAddress(const unsigned char* data, size_t offset, bool v6) {
    IpBytes address{};
    if (v6) {
        std::memcpy(address.data(), data + offset, 16);
    } else {
        address[10] = 0xff;
        address[11] = 0xff;
        std::memcpy(address.data() + 12, data + offset, 4);
    }
    return address;
}

}  // namespace

std::optional<NetworkEvent> parseNetworkEvent(uint16_t event_id, const unsigned char* data, size_t length) {
    const std::optional<Kind> kind = kindOf(event_id);
    if (!kind.has_value() || data == nullptr) {
        return std::nullopt;
    }
    const size_t address_bytes = kind->v6 ? 16 : 4;
    // PID, size, daddr, saddr, dport, sport
    const size_t needed = 4 + 4 + address_bytes * 2 + 2 + 2;
    if (length < needed) {
        return std::nullopt;
    }

    NetworkEvent event;
    event.protocol = kind->protocol;
    event.v6 = kind->v6;
    event.sent = kind->sent;
    event.pid = readU32(data, 0);
    event.size = readU32(data, 4);
    size_t offset = 8;
    event.daddr = readAddress(data, offset, kind->v6);
    offset += address_bytes;
    event.saddr = readAddress(data, offset, kind->v6);
    offset += address_bytes;
    event.dport = readPort(data, offset);
    event.sport = readPort(data, offset + 2);
    return event;
}

}  // namespace pulse
