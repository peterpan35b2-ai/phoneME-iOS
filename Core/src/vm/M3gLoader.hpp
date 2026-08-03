#pragma once

#include <span>

#include "phoneme/base/Types.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm::m3g {

[[nodiscard]] Result<ObjectRef> load_m3g(
    Machine& machine,
    std::span<const u8> bytes);

} // namespace phoneme::vm::m3g
