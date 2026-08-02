#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::filesystem {

class ResourceLoader final {
public:
    [[nodiscard]] static Result<std::vector<u8>> read(
        std::span<const std::string> archive_paths,
        std::string_view resource_path);
};

} // namespace phoneme::filesystem
