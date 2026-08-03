#include "phoneme/security/PermissionCatalog.hpp"

#include <array>

namespace phoneme::security {
namespace {

constexpr std::array<PermissionCatalogEntry, 23U> kEntries {{
    {permissions::connector_http, PermissionDomain::network, true},
    {permissions::connector_https, PermissionDomain::network, true},
    {permissions::connector_ssl, PermissionDomain::network, true},
    {permissions::connector_comm, PermissionDomain::network, true},
    {permissions::connector_socket, PermissionDomain::network, true},
    {permissions::connector_server_socket, PermissionDomain::network, true},
    {permissions::connector_datagram, PermissionDomain::network, true},
    {permissions::connector_datagram_receiver, PermissionDomain::network, true},
    {permissions::connector_file_read, PermissionDomain::filesystem, true},
    {permissions::connector_file_write, PermissionDomain::filesystem, true},
    {permissions::wireless_sms_send, PermissionDomain::network, true},
    {permissions::wireless_sms_receive, PermissionDomain::network, true},
    {permissions::wireless_mms_send, PermissionDomain::network, true},
    {permissions::wireless_mms_receive, PermissionDomain::network, true},
    {permissions::wireless_cbs_receive, PermissionDomain::network, true},
    {permissions::media_record, PermissionDomain::media, true},
    {permissions::media_snapshot, PermissionDomain::media, true},
    {permissions::media_capture_audio, PermissionDomain::media, true},
    {permissions::media_capture_video, PermissionDomain::media, true},
    {permissions::media_capture_image, PermissionDomain::media, true},
    {permissions::push_registry, PermissionDomain::push, true},
    {permissions::platform_request, PermissionDomain::platform, true},
    {"javax.microedition.io.Connector.obex", PermissionDomain::network, true},
}};

[[nodiscard]] bool starts_with(std::string_view value,
                               std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.substr(0U, prefix.size()) == prefix;
}

} // namespace

std::span<const PermissionCatalogEntry> PermissionCatalog::entries() noexcept {
    return kEntries;
}

std::optional<PermissionCatalogEntry> PermissionCatalog::find(
    std::string_view permission) noexcept {
    for (const PermissionCatalogEntry& entry : kEntries) {
        if (entry.name == permission) return entry;
    }
    return std::nullopt;
}

bool PermissionCatalog::known(std::string_view permission) noexcept {
    return find(permission).has_value();
}

PermissionDomain PermissionCatalog::domain_for(
    std::string_view permission) noexcept {
    if (const auto exact = find(permission); exact.has_value()) {
        return exact->domain;
    }
    if (starts_with(permission,
                    "javax.microedition.io.Connector.file.")) {
        return PermissionDomain::filesystem;
    }
    if (starts_with(permission, "javax.microedition.media.") ||
        starts_with(permission, "javax.microedition.amms.")) {
        return PermissionDomain::media;
    }
    if (starts_with(permission, "javax.microedition.io.PushRegistry")) {
        return PermissionDomain::push;
    }
    if (starts_with(permission,
                    "javax.microedition.midlet.platformRequest")) {
        return PermissionDomain::platform;
    }
    if (starts_with(permission, "javax.microedition.io.Connector.") ||
        starts_with(permission, "javax.wireless.messaging.") ||
        starts_with(permission, "javax.microedition.sip.")) {
        return PermissionDomain::network;
    }
    return PermissionDomain::unknown;
}

} // namespace phoneme::security
