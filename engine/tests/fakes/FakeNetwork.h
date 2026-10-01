#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "platform/RawNetwork.h"

namespace pulse {

// 정해진 연결 목록을 돌려준다.
class FakeConnectionScanner final : public IConnectionScanner {
public:
    explicit FakeConnectionScanner(std::vector<RawConnection> connections, std::string error = {})
        : connections_(std::move(connections)), error_(std::move(error)) {}

    ConnectionScan scan() override {
        ++scan_count_;
        ConnectionScan scan;
        scan.connections = connections_;
        scan.error = error_;
        return scan;
    }

    std::size_t scanCount() const { return scan_count_; }

private:
    std::vector<RawConnection> connections_;
    std::string error_;
    std::size_t scan_count_ = 0;
};

// 정해진 창을 순서대로 돌려준다. 첫 호출은 nullopt (창의 시작)이고, 목록이 끝나면 nullopt 를 돌려준다.
class FakeTrafficSource final : public INetworkTrafficSource {
public:
    explicit FakeTrafficSource(std::vector<RawNetworkTraffic> windows) : windows_(std::move(windows)) {}

    std::optional<RawNetworkTraffic> drain() override {
        ++drain_count_;
        if (drain_count_ == 1 || drain_count_ - 2 >= windows_.size()) {
            return std::nullopt;
        }
        return windows_[drain_count_ - 2];
    }

    std::size_t drainCount() const { return drain_count_; }

private:
    std::vector<RawNetworkTraffic> windows_;
    std::size_t drain_count_ = 0;
};

}  // namespace pulse
