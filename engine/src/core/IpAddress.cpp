#include "core/IpAddress.h"

#include <cstddef>
#include <string>
#include <vector>

namespace pulse {
namespace {

// 점으로 나뉜 십진수 네 개 ("1.2.3.4"). 각 0~255, 앞자리 0 은 허용하지 않는다 ("01" 거부).
bool parseIpv4(std::string_view text, std::array<uint8_t, 4>& out) {
    size_t index = 0;
    for (int part = 0; part < 4; ++part) {
        if (index >= text.size()) {
            return false;
        }
        size_t digits = 0;
        unsigned value = 0;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            value = value * 10 + static_cast<unsigned>(text[index] - '0');
            ++index;
            ++digits;
            if (digits > 3 || value > 255) {
                return false;
            }
        }
        if (digits == 0 || (digits > 1 && text[index - digits] == '0')) {
            return false;
        }
        out[static_cast<size_t>(part)] = static_cast<uint8_t>(value);
        if (part < 3) {
            if (index >= text.size() || text[index] != '.') {
                return false;
            }
            ++index;
        }
    }
    return index == text.size();
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 콜론으로 나뉜 16 비트 그룹들. 끝의 IPv4 ("::ffff:1.2.3.4") 는 그룹 두 개로 센다.
bool parseGroups(std::string_view text, std::vector<uint16_t>& groups, bool allow_ipv4_tail) {
    if (text.empty()) {
        return true;
    }
    size_t start = 0;
    while (true) {
        const size_t colon = text.find(':', start);
        const std::string_view piece =
            text.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
        const bool last = colon == std::string_view::npos;
        if (last && allow_ipv4_tail && piece.find('.') != std::string_view::npos) {
            std::array<uint8_t, 4> v4{};
            if (!parseIpv4(piece, v4)) {
                return false;
            }
            groups.push_back(static_cast<uint16_t>((v4[0] << 8) | v4[1]));
            groups.push_back(static_cast<uint16_t>((v4[2] << 8) | v4[3]));
        } else {
            if (piece.empty() || piece.size() > 4) {
                return false;
            }
            unsigned value = 0;
            for (const char c : piece) {
                const int digit = hexValue(c);
                if (digit < 0) {
                    return false;
                }
                value = value * 16 + static_cast<unsigned>(digit);
            }
            groups.push_back(static_cast<uint16_t>(value));
        }
        if (last) {
            return true;
        }
        start = colon + 1;
    }
}

bool parseIpv6(std::string_view text, std::array<uint8_t, 16>& out) {
    const size_t scope = text.find('%');
    if (scope != std::string_view::npos) {
        text = text.substr(0, scope);
    }
    const size_t gap = text.find("::");
    std::vector<uint16_t> head;
    std::vector<uint16_t> tail;
    if (gap == std::string_view::npos) {
        if (!parseGroups(text, head, true) || head.size() != 8) {
            return false;
        }
    } else {
        // "::" 는 한 번만 나올 수 있다.
        if (text.find("::", gap + 1) != std::string_view::npos) {
            return false;
        }
        // 점 표기 IPv4 는 주소 전체의 마지막 조각에만 올 수 있다 ("::" 뒤쪽 끝).
        if (!parseGroups(text.substr(0, gap), head, false) || !parseGroups(text.substr(gap + 2), tail, true)) {
            return false;
        }
        if (head.size() + tail.size() > 7) {
            return false;
        }
    }
    std::vector<uint16_t> groups = head;
    groups.resize(8 - tail.size(), 0);
    groups.insert(groups.end(), tail.begin(), tail.end());
    for (size_t i = 0; i < 8; ++i) {
        out[2 * i] = static_cast<uint8_t>(groups[i] >> 8);
        out[2 * i + 1] = static_cast<uint8_t>(groups[i] & 0xff);
    }
    return true;
}

// 16 바이트가 IPv4-mapped (::ffff:a.b.c.d) 인가.
bool isMapped(const std::array<uint8_t, 16>& b) {
    for (size_t i = 0; i < 10; ++i) {
        if (b[i] != 0) {
            return false;
        }
    }
    return b[10] == 0xff && b[11] == 0xff;
}

}  // namespace

bool IpAddress::isUnspecified() const {
    // IPv4 0.0.0.0 은 ::ffff:0.0.0.0 으로 저장되므로 mapped 형태의 끝 4 바이트도 본다.
    const size_t from = isMapped(bytes) ? 12 : 0;
    for (size_t i = from; i < 16; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

bool IpAddress::isLoopback() const {
    if (isMapped(bytes)) {
        return bytes[12] == 127;
    }
    for (size_t i = 0; i < 15; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return bytes[15] == 1;
}

bool IpAddress::isPrivate() const {
    if (isMapped(bytes)) {
        const uint8_t a = bytes[12];
        const uint8_t b = bytes[13];
        return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
               (a == 169 && b == 254);
    }
    // fc00::/7 (고유 로컬), fe80::/10 (링크 로컬)
    return (bytes[0] & 0xfe) == 0xfc || (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
}

IpAddress makeIpAddress(const std::array<uint8_t, 16>& bytes, bool v6) {
    IpAddress address;
    address.bytes = bytes;
    address.v6 = v6;
    return address;
}

std::string formatIp(const IpAddress& address) {
    const auto dotted = [&address] {
        return std::to_string(address.bytes[12]) + "." + std::to_string(address.bytes[13]) + "." +
               std::to_string(address.bytes[14]) + "." + std::to_string(address.bytes[15]);
    };
    if (!address.v6) {
        return dotted();
    }
    if (isMapped(address.bytes)) {
        return "::ffff:" + dotted();
    }

    uint16_t groups[8];
    for (size_t i = 0; i < 8; ++i) {
        groups[i] = static_cast<uint16_t>((address.bytes[2 * i] << 8) | address.bytes[2 * i + 1]);
    }
    // 가장 긴 0 구간(길이 2 이상, 같으면 앞쪽)을 "::" 로 줄인다.
    size_t best_start = 0;
    size_t best_length = 0;
    for (size_t i = 0; i < 8;) {
        if (groups[i] != 0) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < 8 && groups[j] == 0) {
            ++j;
        }
        if (j - i > best_length) {
            best_start = i;
            best_length = j - i;
        }
        i = j;
    }
    if (best_length < 2) {
        best_length = 0;
    }

    static const char kHex[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < 8; ++i) {
        if (best_length > 0 && i == best_start) {
            text += "::";
            i += best_length - 1;
            continue;
        }
        if (!text.empty() && text.back() != ':') {
            text += ':';
        }
        char group[5];
        size_t digits = 0;
        for (int shift = 12; shift >= 0; shift -= 4) {
            const unsigned nibble = (groups[i] >> shift) & 0xf;
            if (digits > 0 || nibble != 0 || shift == 0) {
                group[digits++] = kHex[nibble];
            }
        }
        text.append(group, digits);
    }
    return text;
}

std::optional<IpAddress> parseIp(std::string_view text) {
    IpAddress address;
    if (text.find(':') != std::string_view::npos) {
        if (!parseIpv6(text, address.bytes)) {
            return std::nullopt;
        }
        address.v6 = true;
        return address;
    }
    std::array<uint8_t, 4> v4{};
    if (!parseIpv4(text, v4)) {
        return std::nullopt;
    }
    address.bytes[10] = 0xff;
    address.bytes[11] = 0xff;
    for (size_t i = 0; i < 4; ++i) {
        address.bytes[12 + i] = v4[i];
    }
    return address;
}

}  // namespace pulse
