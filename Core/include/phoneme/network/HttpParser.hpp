#pragma once

#include <span>
#include <string_view>

#include "phoneme/network/AsyncNetworkAdapter.hpp"

namespace phoneme::network::detail {

[[nodiscard]] Result<HttpResponse> parse_http_response_bytes(
    const Url& url,
    std::string_view request_method,
    std::span<const u8> bytes);

[[nodiscard]] Result<HttpResponse> perform_plain_http_request(
    HttpRequest request);

void set_address_resolution_delay_for_tests(i32 milliseconds) noexcept;

} // namespace phoneme::network::detail
