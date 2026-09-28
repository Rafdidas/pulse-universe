#pragma once

#include <string>

namespace pulse {

// resolveWebPath 의 결과 상태.
//
//  - Ok:           경로가 링크(정션/심볼릭 링크)까지 실제로 풀어본 뒤에도
//                   web_root 안이다. WebPath::path 는 그 링크-해소된 실제
//                   경로다. 그 경로가 실제로 존재하는지는 확인하지 않는다
//                   (SPA 폴백 등은 존재하지 않을 수 있다) — 호출자의 일이다.
//  - Outside:      어휘적으로든, 링크를 풀어본 뒤로든 web_root 밖을 가리킨다.
//                   요청 대상의 모양 자체가 틀린 경우(빈 문자열, 선행 `/`
//                   없음, 역슬래시 포함)도 여기 포함된다. 403 으로 답한다 —
//                   이 프로세스는 관리자 권한으로 도는 일이 많고, 접근 통제가
//                   루프백 바인딩과 이 검사에 달려 있다.
//  - Unresolvable: 경로를 해석할 수 없다 (순환하는 reparse point, 접근이
//                   막힌 중간 경로, `a:b` 같은 대체 데이터 스트림 구문 등).
//                   루트 안인지 밖인지 판단할 수 없으므로 이 경로는 절대
//                   서빙하지 않는다 — 이 상태에서는 WebPath::path 가 채워지지
//                   않는다. 호출자는 이 상태를 "없는 파일" 처럼 취급해
//                   SPA 폴백을 태우면 된다 (403 이 아니다: 이건 탈출 시도라는
//                   증거가 없는, 그저 풀 수 없는 경로다).
//
// 이렇게 나누어도 폐쇄성은 그대로다: Ok 가 아닌 두 상태 모두 "이 경로를
// 서빙하지 말라" 는 뜻이고, 풀리지 않은 경로가 그대로 반환되는 경우는 없다.
enum class WebPathStatus {
    Ok,
    Outside,
    Unresolvable,
};

struct WebPath {
    WebPathStatus status = WebPathStatus::Outside;
    std::string path;  // status == Ok 일 때만 의미가 있다
};

// HTTP 요청 대상을 web_root 아래의 실제 파일 경로로 바꾼다.
// 각 상태의 의미는 WebPathStatus 주석을 보라.
WebPath resolveWebPath(const std::string& web_root, const std::string& target);

// 확장자로 Content-Type 을 고른다. 모르는 확장자는 application/octet-stream.
std::string mimeTypeFor(const std::string& path);

}  // namespace pulse
