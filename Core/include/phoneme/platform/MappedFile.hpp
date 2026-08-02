#pragma once

#include <span>
#include <string>

#include "phoneme/base/Error.hpp"

namespace phoneme::platform {

class MappedFile final {
public:
    MappedFile() noexcept = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] static Result<MappedFile> open_readonly(const std::string& path);

    [[nodiscard]] std::span<const u8> bytes() const noexcept {
        return {static_cast<const u8*>(mapping_), size_};
    }

    [[nodiscard]] usize size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
    MappedFile(int descriptor, void* mapping, usize size) noexcept
        : descriptor_(descriptor), mapping_(mapping), size_(size) {}

    void reset() noexcept;

    int descriptor_ {-1};
    void* mapping_ {nullptr};
    usize size_ {0};
};

} // namespace phoneme::platform
