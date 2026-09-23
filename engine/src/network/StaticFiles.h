#pragma once

#include <optional>
#include <string>

namespace pulse {

// HTTP 요청 대상을 web_root 아래의 실제 파일 경로로 바꾼다.
// 어휘적으로든, 정션/심볼릭 링크를 실제로 풀어본 뒤로든 루트 밖을 가리키면
// nullopt — 이 프로세스는 관리자 권한으로 도는 일이 많고, 접근 통제가
// 루프백 바인딩과 이 검사에 달려 있다.
// 돌려주는 경로는 링크를 해소한 실제 경로다. 그 경로가 실제로 존재하는지는
// 확인하지 않는다 (SPA 폴백 등은 존재하지 않을 수 있다) — 호출자의 일이다.
std::optional<std::string> resolveWebPath(const std::string& web_root,
                                          const std::string& target);

// 확장자로 Content-Type 을 고른다. 모르는 확장자는 application/octet-stream.
std::string mimeTypeFor(const std::string& path);

}  // namespace pulse
