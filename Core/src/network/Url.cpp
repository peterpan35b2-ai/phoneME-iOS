#include "phoneme/network/Url.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace phoneme::network {
namespace {

[[nodiscard]] std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

[[nodiscard]] bool valid_scheme(std::string_view value) noexcept {
    if (value.empty() ||
        !std::isalpha(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    for (const char character : value.substr(1)) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '+' &&
            character != '-' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Status validate_url_characters(std::string_view value) {
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20U || byte == 0x7FU) {
            return fail(ErrorCode::invalid_argument,
                        "URL contains an unescaped control or space");
        }
    }
    return {};
}

[[nodiscard]] Status validate_percent_encoding(std::string_view value) {
    for (usize index = 0; index < value.size(); ++index) {
        if (value[index] != '%') continue;
        if (index + 2U >= value.size() ||
            !std::isxdigit(static_cast<unsigned char>(value[index + 1U])) ||
            !std::isxdigit(static_cast<unsigned char>(value[index + 2U]))) {
            return fail(ErrorCode::invalid_argument,
                        "URL contains malformed percent encoding");
        }
        index += 2U;
    }
    return {};
}

[[nodiscard]] Result<u16> parse_port(std::string_view value) {
    if (value.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "URL port is empty");
    }
    u32 parsed = 0;
    const auto converted = std::from_chars(value.data(),
                                           value.data() + value.size(),
                                           parsed);
    if (converted.ec != std::errc {} ||
        converted.ptr != value.data() + value.size() ||
        parsed > std::numeric_limits<u16>::max()) {
        return fail(ErrorCode::invalid_argument,
                    "URL port is outside 0..65535");
    }
    return static_cast<u16>(parsed);
}

[[nodiscard]] Result<Scheme> parse_scheme(std::string_view value) {
    const std::string normalized = lowercase(value);
    if (normalized == "socket") return Scheme::socket;
    if (normalized == "serversocket") return Scheme::server_socket;
    if (normalized == "datagram") return Scheme::datagram;
    if (normalized == "http") return Scheme::http;
    if (normalized == "https") return Scheme::https;
    return fail(ErrorCode::unsupported_feature,
                "unsupported GCF URL scheme: " + normalized);
}

[[nodiscard]] std::string normalize_path(std::string_view path) {
    const bool absolute = !path.empty() && path.front() == '/';
    const bool trailing_separator =
        path.ends_with('/') || path.ends_with("/.") || path.ends_with("/..");
    std::vector<std::string> components;
    usize start = 0;
    while (start <= path.size()) {
        const usize separator = path.find('/', start);
        const usize end = separator == std::string_view::npos
                              ? path.size()
                              : separator;
        const std::string_view component = path.substr(start, end - start);
        if (component == "..") {
            if (!components.empty()) components.pop_back();
        } else if (!component.empty() && component != ".") {
            components.emplace_back(component);
        }
        if (separator == std::string_view::npos) break;
        start = separator + 1U;
    }

    std::string result = absolute ? "/" : "";
    for (usize index = 0; index < components.size(); ++index) {
        if (index != 0U) result.push_back('/');
        result.append(components[index]);
    }
    if (result.empty() && absolute) result = "/";
    if (trailing_separator && !result.empty() && result.back() != '/') {
        result.push_back('/');
    }
    return result;
}

} // namespace

Result<Url> Url::parse(std::string_view value) {
    if (value.empty()) {
        return fail(ErrorCode::invalid_argument, "GCF URL is empty");
    }
    auto characters = validate_url_characters(value);
    if (!characters) return std::unexpected(characters.error());
    auto percent = validate_percent_encoding(value);
    if (!percent) return std::unexpected(percent.error());

    const usize colon = value.find(':');
    if (colon == std::string_view::npos ||
        !valid_scheme(value.substr(0, colon))) {
        return fail(ErrorCode::invalid_argument,
                    "GCF URL has no valid scheme");
    }
    auto parsed_scheme = parse_scheme(value.substr(0, colon));
    if (!parsed_scheme) return std::unexpected(parsed_scheme.error());

    Url result;
    result.scheme = *parsed_scheme;
    result.scheme_name = lowercase(value.substr(0, colon));

    std::string_view remainder = value.substr(colon + 1U);
    if (!remainder.starts_with("//")) {
        return fail(ErrorCode::invalid_argument,
                    "GCF URL must use // after the scheme");
    }
    remainder.remove_prefix(2U);

    const usize authority_end = remainder.find_first_of("/?#");
    const std::string_view authority = authority_end == std::string_view::npos
                                           ? remainder
                                           : remainder.substr(0, authority_end);
    remainder = authority_end == std::string_view::npos
                    ? std::string_view {}
                    : remainder.substr(authority_end);

    if (authority.find('@') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "userinfo is not supported in GCF URLs");
    }

    if (!authority.empty() && authority.front() == '[') {
        const usize close = authority.find(']');
        if (close == std::string_view::npos) {
            return fail(ErrorCode::invalid_argument,
                        "IPv6 URL host is missing ]");
        }
        result.host = std::string(authority.substr(1U, close - 1U));
        if (result.host.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "IPv6 URL host is empty");
        }
        const std::string_view suffix = authority.substr(close + 1U);
        if (!suffix.empty()) {
            if (suffix.front() != ':') {
                return fail(ErrorCode::invalid_argument,
                            "unexpected data after IPv6 URL host");
            }
            auto port = parse_port(suffix.substr(1U));
            if (!port) return std::unexpected(port.error());
            result.port = *port;
        }
    } else {
        const usize port_separator = authority.rfind(':');
        if (port_separator != std::string_view::npos) {
            if (authority.find(':') != port_separator) {
                return fail(ErrorCode::invalid_argument,
                            "IPv6 URL hosts must be enclosed in []");
            }
            result.host = std::string(authority.substr(0, port_separator));
            auto port = parse_port(authority.substr(port_separator + 1U));
            if (!port) return std::unexpected(port.error());
            result.port = *port;
        } else {
            result.host = std::string(authority);
        }
    }

    if (result.host.find('\\') != std::string::npos) {
        return fail(ErrorCode::invalid_argument,
                    "URL host contains a backslash");
    }

    const usize fragment_position = remainder.find('#');
    if (fragment_position != std::string_view::npos) {
        result.fragment = std::string(remainder.substr(fragment_position + 1U));
        remainder = remainder.substr(0, fragment_position);
    }
    const usize query_position = remainder.find('?');
    if (query_position != std::string_view::npos) {
        result.query = std::string(remainder.substr(query_position + 1U));
        remainder = remainder.substr(0, query_position);
    }
    result.path = std::string(remainder);

    switch (result.scheme) {
    case Scheme::http:
    case Scheme::https:
        if (result.host.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "HTTP URL host is empty");
        }
        if (result.port.has_value() && *result.port == 0U) {
            return fail(ErrorCode::invalid_argument,
                        "HTTP URL port must be within 1..65535");
        }
        if (result.path.empty()) result.path = "/";
        break;
    case Scheme::socket:
        if (!result.port.has_value()) {
            return fail(ErrorCode::invalid_argument,
                        "socket URL requires a port");
        }
        result.server_endpoint = result.host.empty();
        if (!result.server_endpoint && *result.port == 0U) {
            return fail(ErrorCode::invalid_argument,
                        "remote socket URL port must be within 1..65535");
        }
        if (!result.path.empty() || !result.query.empty() ||
            !result.fragment.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "socket URL cannot contain path, query or fragment");
        }
        break;
    case Scheme::server_socket:
        if (!result.host.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "serversocket URL must not contain a remote host");
        }
        if (!result.port.has_value()) {
            return fail(ErrorCode::invalid_argument,
                        "serversocket URL requires a port");
        }
        result.server_endpoint = true;
        if (!result.path.empty() || !result.query.empty() ||
            !result.fragment.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "serversocket URL cannot contain path, query or fragment");
        }
        break;
    case Scheme::datagram:
        if (!result.port.has_value()) {
            return fail(ErrorCode::invalid_argument,
                        "datagram URL requires a port");
        }
        result.server_endpoint = result.host.empty();
        if (!result.server_endpoint && *result.port == 0U) {
            return fail(ErrorCode::invalid_argument,
                        "remote datagram URL port must be within 1..65535");
        }
        if (!result.path.empty() || !result.query.empty() ||
            !result.fragment.empty()) {
            return fail(ErrorCode::invalid_argument,
                        "datagram URL cannot contain path, query or fragment");
        }
        break;
    }

    return result;
}

Result<Url> Url::resolve(const Url& base, std::string_view location) {
    if (location.empty()) return base;
    auto characters = validate_url_characters(location);
    if (!characters) return std::unexpected(characters.error());
    auto percent = validate_percent_encoding(location);
    if (!percent) return std::unexpected(percent.error());
    const usize colon = location.find(':');
    const usize slash = location.find('/');
    const usize query = location.find('?');
    const usize fragment = location.find('#');
    const usize first_delimiter = std::min({slash == std::string_view::npos
                                                ? location.size() : slash,
                                            query == std::string_view::npos
                                                ? location.size() : query,
                                            fragment == std::string_view::npos
                                                ? location.size() : fragment});
    if (colon != std::string_view::npos && colon < first_delimiter) {
        return parse(location);
    }
    if (base.scheme != Scheme::http && base.scheme != Scheme::https) {
        return fail(ErrorCode::invalid_argument,
                    "relative URL resolution requires HTTP(S) base");
    }

    if (location.starts_with("//")) {
        return parse(base.scheme_name + ":" + std::string(location));
    }

    Url result = base;
    if (location.starts_with('#')) {
        result.fragment = std::string(location.substr(1U));
        return result;
    }
    result.query.clear();
    result.fragment.clear();

    std::string_view path_and_more = location;
    const usize fragment_position = path_and_more.find('#');
    if (fragment_position != std::string_view::npos) {
        result.fragment = std::string(path_and_more.substr(fragment_position + 1U));
        path_and_more = path_and_more.substr(0, fragment_position);
    }
    const usize query_position = path_and_more.find('?');
    if (query_position != std::string_view::npos) {
        result.query = std::string(path_and_more.substr(query_position + 1U));
        path_and_more = path_and_more.substr(0, query_position);
    }

    if (path_and_more.empty()) {
        if (location.starts_with('?')) result.path = base.path;
        return result;
    }
    if (path_and_more.front() == '/') {
        result.path = normalize_path(path_and_more);
    } else {
        std::string directory = base.path;
        const usize last_slash = directory.rfind('/');
        directory = last_slash == std::string::npos
                        ? "/"
                        : directory.substr(0, last_slash + 1U);
        result.path = normalize_path(directory + std::string(path_and_more));
    }
    if (result.path.empty()) result.path = "/";
    return result;
}

u16 Url::effective_port() const noexcept {
    if (port.has_value()) return *port;
    if (scheme == Scheme::https) return 443;
    if (scheme == Scheme::http) return 80;
    return 0;
}

std::string Url::authority() const {
    std::string result;
    const bool ipv6 = host.find(':') != std::string::npos;
    if (ipv6) result.push_back('[');
    result.append(host);
    if (ipv6) result.push_back(']');
    const u16 effective = effective_port();
    const bool default_port =
        (scheme == Scheme::http && effective == 80) ||
        (scheme == Scheme::https && effective == 443);
    if (effective != 0 && !default_port) {
        result.push_back(':');
        result.append(std::to_string(effective));
    }
    return result;
}

std::string Url::request_target() const {
    std::string result = path.empty() ? "/" : path;
    if (!query.empty()) {
        result.push_back('?');
        result.append(query);
    }
    return result;
}

std::string Url::to_string() const {
    std::string result = scheme_name + "://" + authority();
    if (scheme == Scheme::http || scheme == Scheme::https) {
        result.append(path.empty() ? "/" : path);
        if (!query.empty()) {
            result.push_back('?');
            result.append(query);
        }
        if (!fragment.empty()) {
            result.push_back('#');
            result.append(fragment);
        }
    }
    return result;
}

} // namespace phoneme::network
