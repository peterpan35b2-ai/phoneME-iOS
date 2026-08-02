#pragma once

#include <memory>
#include <string_view>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm {

[[nodiscard]] bool is_builtin_class(std::string_view internal_name) noexcept;
[[nodiscard]] Result<std::shared_ptr<const classfile::ClassFile>>
load_builtin_class(std::string_view internal_name);

} // namespace phoneme::vm
