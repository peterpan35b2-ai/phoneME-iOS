#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "phoneme/base/Error.hpp"

namespace phoneme::network {

enum class Scheme : u8 {
    socket,
    server_socket,
    datagram,
    http,
    https,
};

struct Url final {
    Scheme scheme {Scheme::socket};
    std::string scheme_name;
    std::string host;
    std::optional<u16> port;
    std::string path;
    std::string query;
    std::string fragment;
    bool server_endpoint {false};

    [[nodiscard]] static Result<Url> parse(std::string_view value);
    [[nodiscard]] static Result<Url> resolve(const Url& base,
                                             std::string_view location);

    [[nodiscard]] u16 effective_port() const noexcept;
    [[nodiscard]] std::string authority() const;
    [[nodiscard]] std::string request_target() const;
    [[nodiscard]] std::string to_string() const;
};

} // namespace phoneme::network
