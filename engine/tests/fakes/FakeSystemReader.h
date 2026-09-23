#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "platform/ISystemReader.h"

namespace pulse {

// 정해진 표본을 순서대로 돌려준다. 목록이 끝나면 마지막 것을 반복한다.
class FakeSystemReader final : public ISystemReader {
public:
    FakeSystemReader(std::vector<RawSample> samples, unsigned core_count)
        : samples_(std::move(samples)), core_count_(core_count == 0 ? 1u : core_count) {}

    RawSample read() override {
        ++read_count_;
        if (samples_.empty()) {
            return RawSample{};
        }
        const std::size_t index = std::min(next_++, samples_.size() - 1);
        return samples_[index];
    }

    unsigned coreCount() const override { return core_count_; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

    std::size_t readCount() const { return read_count_; }

private:
    std::vector<RawSample> samples_;
    unsigned core_count_ = 1;
    std::size_t next_ = 0;
    std::size_t read_count_ = 0;
};

// 지정한 횟수만큼 정상 반환한 뒤 던진다.
class ThrowingSystemReader final : public ISystemReader {
public:
    explicit ThrowingSystemReader(std::size_t reads_before_throw)
        : reads_before_throw_(reads_before_throw) {}

    RawSample read() override {
        if (read_count_++ >= reads_before_throw_) {
            throw std::runtime_error("reader exploded");
        }
        return RawSample{};
    }

    unsigned coreCount() const override { return 4; }

    HostInfo hostInfo() const override { return HostInfo{"FakeOS", false}; }

private:
    std::size_t reads_before_throw_ = 0;
    std::size_t read_count_ = 0;
};

}  // namespace pulse
