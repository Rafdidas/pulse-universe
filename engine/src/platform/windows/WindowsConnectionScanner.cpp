#include "platform/windows/WindowsConnectionScanner.h"

// winsock2.h 는 windows.h 보다 먼저 와야 한다.
#include <winsock2.h>
#include <ws2tcpip.h>
// iphlpapi.h 는 winsock2.h 뒤에 온다.
#include <iphlpapi.h>
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace pulse {
namespace {

// 표를 읽는 사이에 표가 커지면 크기를 다시 묻는다.
constexpr int kMaxAttempts = 5;
// 크기를 물은 뒤 표가 조금 자라도 한 번에 읽히도록 두는 여유 (바이트).
constexpr ULONG kSlackBytes = 16 * 1024;

// call(buffer, &size) 는 GetExtended*Table 한 번이다. 성공하면 NO_ERROR 를 돌려주고 buffer 에 표가 있다.
DWORD readTable(std::vector<unsigned char>& buffer, const std::function<DWORD(void*, ULONG*)>& call) {
    ULONG size = 0;
    DWORD result = call(nullptr, &size);
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (result != ERROR_INSUFFICIENT_BUFFER) {
            return result;
        }
        buffer.assign(static_cast<size_t>(size) + kSlackBytes, 0);
        ULONG capacity = static_cast<ULONG>(buffer.size());
        result = call(buffer.data(), &capacity);
        if (result == NO_ERROR) {
            return NO_ERROR;
        }
        size = capacity;
    }
    return result;
}

std::string ip4ToString(DWORD address) {
    char text[INET_ADDRSTRLEN] = {};
    IN_ADDR in{};
    in.S_un.S_addr = address;
    return ::InetNtopA(AF_INET, &in, text, sizeof(text)) != nullptr ? text : "";
}

std::string ip6ToString(const UCHAR (&address)[16]) {
    char text[INET6_ADDRSTRLEN] = {};
    IN6_ADDR in{};
    static_assert(sizeof(in.u.Byte) == sizeof(address));
    for (size_t i = 0; i < sizeof(address); ++i) {
        in.u.Byte[i] = address[i];
    }
    return ::InetNtopA(AF_INET6, &in, text, sizeof(text)) != nullptr ? text : "";
}

// dwLocalPort·dwRemotePort 는 네트워크 바이트 순서로 아래 16 비트에 들어 있다.
uint16_t portOf(DWORD raw) {
    return ::ntohs(static_cast<u_short>(raw));
}

TcpState tcpStateOf(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED: return TcpState::Closed;
        case MIB_TCP_STATE_LISTEN: return TcpState::Listen;
        case MIB_TCP_STATE_SYN_SENT: return TcpState::SynSent;
        case MIB_TCP_STATE_SYN_RCVD: return TcpState::SynReceived;
        case MIB_TCP_STATE_ESTAB: return TcpState::Established;
        case MIB_TCP_STATE_FIN_WAIT1: return TcpState::FinWait1;
        case MIB_TCP_STATE_FIN_WAIT2: return TcpState::FinWait2;
        case MIB_TCP_STATE_CLOSE_WAIT: return TcpState::CloseWait;
        case MIB_TCP_STATE_CLOSING: return TcpState::Closing;
        case MIB_TCP_STATE_LAST_ACK: return TcpState::LastAck;
        case MIB_TCP_STATE_TIME_WAIT: return TcpState::TimeWait;
        case MIB_TCP_STATE_DELETE_TCB: return TcpState::DeleteTcb;
        default: return TcpState::Closed;  // 모르는 값은 집계에서 빠지게 한다 (스펙 2절)
    }
}

void note(ConnectionScan& scan, const char* table, DWORD error) {
    if (!scan.error.empty()) {
        scan.error += "; ";
    }
    scan.error += std::string(table) + " failed with error " + std::to_string(error);
}

void readTcp4(ConnectionScan& scan) {
    std::vector<unsigned char> buffer;
    const DWORD result = readTable(buffer, [](void* table, ULONG* size) {
        return ::GetExtendedTcpTable(table, size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (result != NO_ERROR) {
        note(scan, "GetExtendedTcpTable(AF_INET)", result);
        return;
    }
    if (buffer.empty()) {
        return;  // 크기를 물은 첫 호출이 곧바로 성공하면 표가 비어 있다.
    }
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID& row = table->table[i];
        RawConnection c;
        c.pid = row.dwOwningPid;
        c.protocol = NetProtocol::Tcp;
        c.local_ip = ip4ToString(row.dwLocalAddr);
        c.local_port = portOf(row.dwLocalPort);
        c.remote_ip = ip4ToString(row.dwRemoteAddr);
        c.remote_port = portOf(row.dwRemotePort);
        c.state = tcpStateOf(row.dwState);
        scan.connections.push_back(std::move(c));
    }
}

void readTcp6(ConnectionScan& scan) {
    std::vector<unsigned char> buffer;
    const DWORD result = readTable(buffer, [](void* table, ULONG* size) {
        return ::GetExtendedTcpTable(table, size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (result != NO_ERROR) {
        note(scan, "GetExtendedTcpTable(AF_INET6)", result);
        return;
    }
    if (buffer.empty()) {
        return;  // 크기를 물은 첫 호출이 곧바로 성공하면 표가 비어 있다.
    }
    const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCP6ROW_OWNER_PID& row = table->table[i];
        RawConnection c;
        c.pid = row.dwOwningPid;
        c.protocol = NetProtocol::Tcp;
        c.local_ip = ip6ToString(row.ucLocalAddr);
        c.local_port = portOf(row.dwLocalPort);
        c.remote_ip = ip6ToString(row.ucRemoteAddr);
        c.remote_port = portOf(row.dwRemotePort);
        c.state = tcpStateOf(row.dwState);
        scan.connections.push_back(std::move(c));
    }
}

void readUdp4(ConnectionScan& scan) {
    std::vector<unsigned char> buffer;
    const DWORD result = readTable(buffer, [](void* table, ULONG* size) {
        return ::GetExtendedUdpTable(table, size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    });
    if (result != NO_ERROR) {
        note(scan, "GetExtendedUdpTable(AF_INET)", result);
        return;
    }
    if (buffer.empty()) {
        return;  // 크기를 물은 첫 호출이 곧바로 성공하면 표가 비어 있다.
    }
    const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_UDPROW_OWNER_PID& row = table->table[i];
        RawConnection c;
        c.pid = row.dwOwningPid;
        c.protocol = NetProtocol::Udp;
        c.local_ip = ip4ToString(row.dwLocalAddr);
        c.local_port = portOf(row.dwLocalPort);
        scan.connections.push_back(std::move(c));
    }
}

void readUdp6(ConnectionScan& scan) {
    std::vector<unsigned char> buffer;
    const DWORD result = readTable(buffer, [](void* table, ULONG* size) {
        return ::GetExtendedUdpTable(table, size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    });
    if (result != NO_ERROR) {
        note(scan, "GetExtendedUdpTable(AF_INET6)", result);
        return;
    }
    if (buffer.empty()) {
        return;  // 크기를 물은 첫 호출이 곧바로 성공하면 표가 비어 있다.
    }
    const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_UDP6ROW_OWNER_PID& row = table->table[i];
        RawConnection c;
        c.pid = row.dwOwningPid;
        c.protocol = NetProtocol::Udp;
        c.local_ip = ip6ToString(row.ucLocalAddr);
        c.local_port = portOf(row.dwLocalPort);
        scan.connections.push_back(std::move(c));
    }
}

}  // namespace

ConnectionScan WindowsConnectionScanner::scan() {
    ConnectionScan result;
    readTcp4(result);
    readTcp6(result);
    readUdp4(result);
    readUdp6(result);
    return result;
}

}  // namespace pulse
