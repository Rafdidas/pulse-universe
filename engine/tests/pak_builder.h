#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 테스트용 web.pak 생성기. 스펙 3절 포맷을 그대로 만든다.
namespace pulse_test {

inline void putLe(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

// 경로는 호출자가 오름차순으로 넘긴다 (정렬을 어기는 pak 을 만드는 테스트도 있다).
inline std::string makePak(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string table;
    std::size_t table_size = 0;
    for (const auto& file : files) {
        table_size += 2 + file.first.size() + 8 + 8;
    }
    std::uint64_t offset = 8 + 4 + table_size;
    for (const auto& file : files) {
        putLe(table, file.first.size(), 2);
        table += file.first;
        putLe(table, offset, 8);
        putLe(table, file.second.size(), 8);
        offset += file.second.size();
    }
    std::string pak(std::string("PLSPAK1\0", 8));
    putLe(pak, files.size(), 4);
    pak += table;
    for (const auto& file : files) {
        pak += file.second;
    }
    return pak;
}

}  // namespace pulse_test
