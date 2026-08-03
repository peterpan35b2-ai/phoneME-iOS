#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "phoneme/security/PermissionPolicy.hpp"

namespace phoneme::security {

struct PermissionCatalogEntry final {
    std::string_view name;
    PermissionDomain domain {PermissionDomain::unknown};
    bool user_sensitive {true};
};

class PermissionCatalog final {
public:
    [[nodiscard]] static std::span<const PermissionCatalogEntry> entries()
        noexcept;
    [[nodiscard]] static std::optional<PermissionCatalogEntry> find(
        std::string_view permission) noexcept;
    [[nodiscard]] static bool known(std::string_view permission) noexcept;
    [[nodiscard]] static PermissionDomain domain_for(
        std::string_view permission) noexcept;
};

} // namespace phoneme::security
