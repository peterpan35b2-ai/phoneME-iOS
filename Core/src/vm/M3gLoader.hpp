#pragma once

#include <functional>
#include <span>
#include <string_view>

#include "phoneme/base/Types.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm::m3g {

using ExternalReferenceResolver =
    std::function<Result<ObjectRef>(std::string_view uri)>;

[[nodiscard]] Result<ObjectRef> load_m3g(
    Machine& machine,
    std::span<const u8> bytes,
    const ExternalReferenceResolver& resolve_external = {});

} // namespace phoneme::vm::m3g
