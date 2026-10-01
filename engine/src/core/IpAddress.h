#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pulse {

// 사람이 읽는 IP 문자열을 바이트로 푼 것. IPv4 는 16 바이트의 IPv4-mapped 형태(::ffff:a.b.c.d)로 둔다.
// OS 헤더를 쓰지 않는 순수 코드다 — core/ 는 플랫폼을 모른다.
struct IpAddress {
    std::array<uint8_t, 16> bytes{};
    bool v6 = false;  // 원래 문자열이 IPv6 였는가

    bool isUnspecified() const;  // 0.0.0.0, ::
    bool isLoopback() const;     // 127.0.0.0/8, ::1, ::ffff:127.x.x.x
    // 사설망·링크 로컬: 10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10 (IPv4-mapped 도 같은 규칙)
    bool isPrivate() const;
};

// "192.168.0.10", "fe80::1%12", "::ffff:127.0.0.1", "2001:db8::1" 을 푼다. 형식이 틀리면 nullopt.
// IPv6 의 '%scope' 접미사는 버린다.
std::optional<IpAddress> parseIp(std::string_view text);

}  // namespace pulse
