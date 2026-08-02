#include "phoneme/network/AsyncNetworkAdapter.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
extern "C" {
__attribute__((weak)) int32_t phoneme_ios_https_execute(
    const char*, const char*, const char*, const uint8_t*, int32_t, int32_t) {
    return -1;
}
__attribute__((weak)) int32_t phoneme_ios_https_get_status_code(int32_t) {
    return -1;
}
__attribute__((weak)) int32_t phoneme_ios_https_copy_string(
    int32_t, int32_t, char*, int32_t) {
    return -1;
}
__attribute__((weak)) int32_t phoneme_ios_https_copy_body(
    int32_t, uint8_t*, int32_t) {
    return -1;
}
__attribute__((weak)) int64_t phoneme_ios_https_get_long(int32_t, int32_t) {
    return 0;
}
__attribute__((weak)) void phoneme_ios_https_close(int32_t) {}
}
#endif

namespace phoneme::network {
namespace {

constexpr usize kMaximumHttpResponseBytes = 32U * 1024U * 1024U;
constexpr usize kIoChunkSize = 16U * 1024U;

[[nodiscard]] std::unexpected<Error> io_failure(std::string message) {
    if (errno != 0) {
        message.append(": ");
        message.append(std::strerror(errno));
    }
    return fail(ErrorCode::io_error, std::move(message));
}

[[nodiscard]] std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       if (character >= 'A' && character <= 'Z') {
                           return static_cast<char>(character - 'A' + 'a');
                       }
                       return static_cast<char>(character);
                   });
    return result;
}

[[nodiscard]] std::string trim(std::string_view value) {
    usize first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    usize last = value.size();
    while (last > first &&
           (value[last - 1U] == ' ' || value[last - 1U] == '\t')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

[[nodiscard]] Status wait_fd(int descriptor,
                             short events,
                             i32 timeout_ms) {
    pollfd poll_descriptor {
        .fd = descriptor,
        .events = events,
        .revents = 0,
    };
    const int timeout = timeout_ms <= 0 ? -1 : timeout_ms;
    while (true) {
        const int result = ::poll(&poll_descriptor, 1, timeout);
        if (result > 0) {
            if ((poll_descriptor.revents &
                 static_cast<short>(POLLERR | POLLHUP | POLLNVAL)) != 0 &&
                (poll_descriptor.revents & events) == 0) {
                return fail(ErrorCode::io_error,
                            "network descriptor reported an error");
            }
            return {};
        }
        if (result == 0) {
            return fail(ErrorCode::io_error,
                        "network operation timed out");
        }
        if (errno != EINTR) return io_failure("poll failed");
    }
}

[[nodiscard]] Status set_nonblocking(int descriptor, bool enabled) {
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) return io_failure("fcntl(F_GETFL) failed");
    const int updated = enabled ? (flags | O_NONBLOCK)
                                : (flags & ~O_NONBLOCK);
    if (::fcntl(descriptor, F_SETFL, updated) < 0) {
        return io_failure("fcntl(F_SETFL) failed");
    }
    return {};
}

void configure_descriptor(int descriptor) noexcept {
#if defined(__APPLE__)
    int enabled = 1;
    (void)::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE,
                       &enabled, static_cast<socklen_t>(sizeof(enabled)));
#else
    (void)descriptor;
#endif
}

[[nodiscard]] Result<Endpoint> endpoint_from_sockaddr(
    const sockaddr* address,
    socklen_t address_length) {
    std::array<char, NI_MAXHOST> host {};
    std::array<char, NI_MAXSERV> service {};
    const int result = ::getnameinfo(address, address_length,
                                     host.data(),
                                     static_cast<socklen_t>(host.size()),
                                     service.data(),
                                     static_cast<socklen_t>(service.size()),
                                     NI_NUMERICHOST | NI_NUMERICSERV);
    if (result != 0) {
        return fail(ErrorCode::io_error,
                    std::string("getnameinfo failed: ") +
                        ::gai_strerror(result));
    }
    u32 port = 0;
    const std::string_view service_view(service.data());
    const auto converted = std::from_chars(service_view.data(),
                                           service_view.data() +
                                               service_view.size(),
                                           port);
    if (converted.ec != std::errc {} ||
        port > std::numeric_limits<u16>::max()) {
        return fail(ErrorCode::io_error,
                    "resolved endpoint has invalid port");
    }
    return Endpoint {
        .host = host.data(),
        .port = static_cast<u16>(port),
    };
}

[[nodiscard]] Result<Endpoint> socket_endpoint(int descriptor,
                                               bool peer) {
    sockaddr_storage address {};
    socklen_t length = static_cast<socklen_t>(sizeof(address));
    const int result = peer
        ? ::getpeername(descriptor,
                        reinterpret_cast<sockaddr*>(&address), &length)
        : ::getsockname(descriptor,
                        reinterpret_cast<sockaddr*>(&address), &length);
    if (result != 0) {
        return io_failure(peer ? "getpeername failed" : "getsockname failed");
    }
    return endpoint_from_sockaddr(
        reinterpret_cast<const sockaddr*>(&address), length);
}

struct AddressList final {
    addrinfo* value {nullptr};

    AddressList() = default;
    AddressList(const AddressList&) = delete;
    AddressList& operator=(const AddressList&) = delete;
    AddressList(AddressList&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    AddressList& operator=(AddressList&& other) noexcept {
        if (this != &other) {
            if (value != nullptr) ::freeaddrinfo(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~AddressList() {
        if (value != nullptr) ::freeaddrinfo(value);
    }
};

[[nodiscard]] Result<AddressList> resolve_addresses(std::string_view host,
                                                    u16 port,
                                                    int socket_type,
                                                    bool passive) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socket_type;
    hints.ai_protocol = socket_type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    AddressList list;
    const std::string service = std::to_string(port);
    const std::string host_text(host);
    const char* host_pointer = host.empty() ? nullptr : host_text.c_str();
    const int result = ::getaddrinfo(host_pointer, service.c_str(),
                                     &hints, &list.value);
    if (result != 0) {
        return fail(ErrorCode::io_error,
                    std::string("DNS resolution failed: ") +
                        ::gai_strerror(result));
    }
    return list;
}

[[nodiscard]] Result<int> connect_stream_descriptor(const Url& url,
                                                    i32 timeout_ms) {
    auto addresses = resolve_addresses(url.host, url.effective_port(),
                                       SOCK_STREAM, false);
    if (!addresses) return std::unexpected(addresses.error());

    Error last_error = Error::make(ErrorCode::io_error,
                                   "no resolved address could be connected");
    for (addrinfo* address = addresses->value;
         address != nullptr;
         address = address->ai_next) {
        const int descriptor = ::socket(address->ai_family,
                                        address->ai_socktype,
                                        address->ai_protocol);
        if (descriptor < 0) {
            last_error = Error::make(ErrorCode::io_error,
                                     "socket creation failed");
            continue;
        }
        configure_descriptor(descriptor);
        auto nonblocking = set_nonblocking(descriptor, true);
        if (!nonblocking) {
            last_error = nonblocking.error();
            ::close(descriptor);
            continue;
        }
        int result = ::connect(descriptor, address->ai_addr,
                               static_cast<socklen_t>(address->ai_addrlen));
        if (result != 0 && errno == EINPROGRESS) {
            auto ready = wait_fd(descriptor, POLLOUT, timeout_ms);
            if (!ready) {
                last_error = ready.error();
                ::close(descriptor);
                continue;
            }
            int socket_error_value = 0;
            socklen_t length = static_cast<socklen_t>(
                sizeof(socket_error_value));
            if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR,
                             &socket_error_value, &length) != 0 ||
                socket_error_value != 0) {
                last_error = Error::make(
                    ErrorCode::io_error,
                    socket_error_value == 0
                        ? "connect failed"
                        : std::string("connect failed: ") +
                              std::strerror(socket_error_value));
                ::close(descriptor);
                continue;
            }
            result = 0;
        }
        if (result != 0) {
            last_error = Error::make(ErrorCode::io_error,
                                     std::string("connect failed: ") +
                                         std::strerror(errno));
            ::close(descriptor);
            continue;
        }
        auto blocking = set_nonblocking(descriptor, false);
        if (!blocking) {
            last_error = blocking.error();
            ::close(descriptor);
            continue;
        }
        return descriptor;
    }
    return std::unexpected(std::move(last_error));
}

[[nodiscard]] Result<int> create_server_descriptor(const Url& url) {
    auto addresses = resolve_addresses(url.host, url.effective_port(),
                                       SOCK_STREAM, true);
    if (!addresses) return std::unexpected(addresses.error());

    Error last_error = Error::make(ErrorCode::io_error,
                                   "no local address could be bound");
    for (addrinfo* address = addresses->value;
         address != nullptr;
         address = address->ai_next) {
        const int descriptor = ::socket(address->ai_family,
                                        address->ai_socktype,
                                        address->ai_protocol);
        if (descriptor < 0) continue;
        configure_descriptor(descriptor);
        int enabled = 1;
        (void)::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,
                           &enabled, static_cast<socklen_t>(sizeof(enabled)));
        if (::bind(descriptor, address->ai_addr,
                   static_cast<socklen_t>(address->ai_addrlen)) == 0 &&
            ::listen(descriptor, 16) == 0) {
            return descriptor;
        }
        last_error = Error::make(ErrorCode::io_error,
                                 std::string("bind/listen failed: ") +
                                     std::strerror(errno));
        ::close(descriptor);
    }
    return std::unexpected(std::move(last_error));
}

[[nodiscard]] Result<int> create_datagram_descriptor(const Url& url) {
    const bool local = url.host.empty();
    auto addresses = resolve_addresses(url.host, url.effective_port(),
                                       SOCK_DGRAM, local);
    if (!addresses) return std::unexpected(addresses.error());

    Error last_error = Error::make(ErrorCode::io_error,
                                   "no datagram address could be opened");
    for (addrinfo* address = addresses->value;
         address != nullptr;
         address = address->ai_next) {
        const int descriptor = ::socket(address->ai_family,
                                        address->ai_socktype,
                                        address->ai_protocol);
        if (descriptor < 0) continue;
        configure_descriptor(descriptor);
        const int result = local
            ? ::bind(descriptor, address->ai_addr,
                     static_cast<socklen_t>(address->ai_addrlen))
            : ::connect(descriptor, address->ai_addr,
                        static_cast<socklen_t>(address->ai_addrlen));
        if (result == 0) return descriptor;
        last_error = Error::make(ErrorCode::io_error,
                                 std::string(local ? "UDP bind failed: "
                                                   : "UDP connect failed: ") +
                                     std::strerror(errno));
        ::close(descriptor);
    }
    return std::unexpected(std::move(last_error));
}

[[nodiscard]] Status send_all(int descriptor,
                              std::span<const u8> bytes,
                              i32 timeout_ms) {
    usize offset = 0;
    while (offset < bytes.size()) {
        auto ready = wait_fd(descriptor, POLLOUT, timeout_ms);
        if (!ready) return ready;
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t written = ::send(
            descriptor,
            bytes.data() + offset,
            bytes.size() - offset,
            flags);
        if (written > 0) {
            offset += static_cast<usize>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        return io_failure("socket write failed");
    }
    return {};
}

[[nodiscard]] Result<std::vector<u8>> receive_all(int descriptor,
                                                  i32 timeout_ms) {
    std::vector<u8> result;
    std::array<u8, kIoChunkSize> buffer {};
    while (true) {
        auto ready = wait_fd(descriptor, POLLIN, timeout_ms);
        if (!ready) {
            if (!result.empty()) return result;
            return std::unexpected(ready.error());
        }
        const ssize_t count = ::recv(descriptor, buffer.data(),
                                     buffer.size(), 0);
        if (count == 0) return result;
        if (count < 0) {
            if (errno == EINTR) continue;
            return io_failure("socket read failed");
        }
        const usize amount = static_cast<usize>(count);
        if (amount > kMaximumHttpResponseBytes - result.size()) {
            return fail(ErrorCode::overflow,
                        "HTTP response exceeds maximum size");
        }
        result.insert(result.end(), buffer.begin(),
                      buffer.begin() + static_cast<isize>(amount));
    }
}

[[nodiscard]] Result<std::vector<u8>> decode_chunked(
    std::span<const u8> encoded) {
    std::vector<u8> result;
    usize cursor = 0;
    while (true) {
        usize line_end = cursor;
        while (line_end + 1U < encoded.size() &&
               !(encoded[line_end] == '\r' &&
                 encoded[line_end + 1U] == '\n')) {
            ++line_end;
        }
        if (line_end + 1U >= encoded.size()) {
            return fail(ErrorCode::io_error,
                        "truncated chunked HTTP body");
        }
        std::string_view line(
            reinterpret_cast<const char*>(encoded.data() + cursor),
            line_end - cursor);
        const usize extension = line.find(';');
        if (extension != std::string_view::npos) {
            line = line.substr(0, extension);
        }
        usize chunk_size = 0;
        const auto converted = std::from_chars(line.data(),
                                               line.data() + line.size(),
                                               chunk_size, 16);
        if (converted.ec != std::errc {} ||
            converted.ptr != line.data() + line.size()) {
            return fail(ErrorCode::io_error,
                        "invalid HTTP chunk size");
        }
        cursor = line_end + 2U;
        if (chunk_size == 0U) return result;
        if (chunk_size > encoded.size() - cursor ||
            chunk_size + 2U > encoded.size() - cursor) {
            return fail(ErrorCode::io_error,
                        "HTTP chunk exceeds received body");
        }
        if (chunk_size > kMaximumHttpResponseBytes - result.size()) {
            return fail(ErrorCode::overflow,
                        "decoded HTTP body exceeds maximum size");
        }
        result.insert(result.end(),
                      encoded.begin() + static_cast<isize>(cursor),
                      encoded.begin() +
                          static_cast<isize>(cursor + chunk_size));
        cursor += chunk_size;
        if (encoded[cursor] != '\r' || encoded[cursor + 1U] != '\n') {
            return fail(ErrorCode::io_error,
                        "HTTP chunk is missing terminator");
        }
        cursor += 2U;
    }
}

[[nodiscard]] std::optional<std::string> header_value(
    const std::vector<Header>& headers,
    std::string_view name) {
    const std::string key = lowercase(name);
    for (const auto& [header_name, value] : headers) {
        if (lowercase(header_name) == key) return value;
    }
    return std::nullopt;
}

[[nodiscard]] Result<HttpResponse> parse_http_response(
    const Url& url,
    std::span<const u8> bytes) {
    usize header_end = 0;
    bool found = false;
    while (header_end + 3U < bytes.size()) {
        if (bytes[header_end] == '\r' && bytes[header_end + 1U] == '\n' &&
            bytes[header_end + 2U] == '\r' &&
            bytes[header_end + 3U] == '\n') {
            found = true;
            break;
        }
        ++header_end;
    }
    if (!found) {
        return fail(ErrorCode::io_error,
                    "HTTP response has no complete header block");
    }

    const std::string_view header_text(
        reinterpret_cast<const char*>(bytes.data()), header_end);
    const usize status_end = header_text.find("\r\n");
    if (status_end == std::string_view::npos) {
        return fail(ErrorCode::io_error,
                    "HTTP response has malformed status line");
    }
    const std::string_view status_line = header_text.substr(0, status_end);
    const usize first_space = status_line.find(' ');
    const usize second_space = first_space == std::string_view::npos
        ? std::string_view::npos
        : status_line.find(' ', first_space + 1U);
    if (!status_line.starts_with("HTTP/") ||
        first_space == std::string_view::npos) {
        return fail(ErrorCode::io_error,
                    "HTTP response status line is invalid");
    }
    const std::string_view code_text = second_space == std::string_view::npos
        ? status_line.substr(first_space + 1U)
        : status_line.substr(first_space + 1U,
                             second_space - first_space - 1U);
    i32 status_code = -1;
    const auto code_converted = std::from_chars(
        code_text.data(), code_text.data() + code_text.size(), status_code);
    if (code_converted.ec != std::errc {} || status_code < 100 ||
        status_code > 999) {
        return fail(ErrorCode::io_error,
                    "HTTP response status code is invalid");
    }

    HttpResponse response;
    response.final_url = url;
    response.status_code = status_code;
    if (second_space != std::string_view::npos) {
        response.reason = std::string(status_line.substr(second_space + 1U));
    }

    usize cursor = status_end + 2U;
    while (cursor < header_text.size()) {
        const usize line_end = header_text.find("\r\n", cursor);
        const usize end = line_end == std::string_view::npos
                              ? header_text.size()
                              : line_end;
        const std::string_view line = header_text.substr(cursor, end - cursor);
        const usize separator = line.find(':');
        if (separator == std::string_view::npos || separator == 0U) {
            return fail(ErrorCode::io_error,
                        "HTTP response contains malformed header");
        }
        response.headers.emplace_back(
            std::string(line.substr(0, separator)),
            trim(line.substr(separator + 1U)));
        if (line_end == std::string_view::npos) break;
        cursor = line_end + 2U;
    }

    const usize body_offset = header_end + 4U;
    std::span<const u8> body = bytes.subspan(body_offset);
    auto transfer_encoding = header_value(response.headers,
                                          "Transfer-Encoding");
    if (transfer_encoding.has_value() &&
        lowercase(*transfer_encoding).find("chunked") != std::string::npos) {
        auto decoded = decode_chunked(body);
        if (!decoded) return std::unexpected(decoded.error());
        response.body = std::move(*decoded);
    } else {
        auto length_header = header_value(response.headers, "Content-Length");
        if (length_header.has_value()) {
            usize length = 0;
            const auto converted = std::from_chars(
                length_header->data(),
                length_header->data() + length_header->size(), length);
            if (converted.ec != std::errc {} || length > body.size()) {
                return fail(ErrorCode::io_error,
                            "HTTP response Content-Length is invalid");
            }
            body = body.first(length);
        }
        response.body.assign(body.begin(), body.end());
    }
    return response;
}

#if defined(__APPLE__)
struct AppleHttpsHandle final {
    i32 value {0};

    AppleHttpsHandle() = default;
    explicit AppleHttpsHandle(i32 handle) : value(handle) {}
    AppleHttpsHandle(const AppleHttpsHandle&) = delete;
    AppleHttpsHandle& operator=(const AppleHttpsHandle&) = delete;
    ~AppleHttpsHandle() {
        if (value > 0) phoneme_ios_https_close(value);
    }
};

[[nodiscard]] Result<std::optional<std::string>> copy_apple_https_string(
    i32 handle,
    i32 field) {
    const i32 length = phoneme_ios_https_copy_string(handle, field,
                                                     nullptr, 0);
    if (length < 0) return std::optional<std::string> {};
    if (length == std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "HTTPS bridge string is too large");
    }
    std::vector<char> buffer(static_cast<usize>(length) + 1U, '\0');
    const i32 copied = phoneme_ios_https_copy_string(
        handle, field, buffer.data(), static_cast<i32>(buffer.size()));
    if (copied != length) {
        return fail(ErrorCode::io_error,
                    "HTTPS bridge could not copy response string");
    }
    return std::optional<std::string>(
        std::string(buffer.data(), static_cast<usize>(length)));
}

[[nodiscard]] std::vector<Header> parse_apple_https_headers(
    std::string_view serialized) {
    std::vector<Header> headers;
    usize cursor = 0;
    while (cursor < serialized.size()) {
        usize end = serialized.find('\n', cursor);
        if (end == std::string_view::npos) end = serialized.size();
        std::string_view line = serialized.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        const usize separator = line.find(':');
        if (separator != std::string_view::npos && separator != 0U) {
            headers.emplace_back(std::string(line.substr(0, separator)),
                                 trim(line.substr(separator + 1U)));
        }
        cursor = end == serialized.size() ? end : end + 1U;
    }
    return headers;
}

[[nodiscard]] Result<HttpResponse> perform_apple_https(
    const HttpRequest& request) {
    if (request.body.size() >
        static_cast<usize>(std::numeric_limits<i32>::max())) {
        return fail(ErrorCode::overflow,
                    "HTTPS request body exceeds bridge limit");
    }

    std::string serialized_headers;
    for (const auto& [name, value] : request.headers) {
        serialized_headers.append(name);
        serialized_headers.append(": ");
        serialized_headers.append(value);
        serialized_headers.append("\r\n");
    }
    const std::string url = request.url.to_string();
    const i32 raw_handle = phoneme_ios_https_execute(
        url.c_str(), request.method.c_str(), serialized_headers.c_str(),
        request.body.empty() ? nullptr : request.body.data(),
        static_cast<i32>(request.body.size()), request.timeout_ms);
    AppleHttpsHandle handle(raw_handle);
    if (handle.value <= 0) {
        return fail(ErrorCode::io_error,
                    "HTTPS bridge failed to create a request result");
    }

    auto error = copy_apple_https_string(handle.value, 10);
    if (!error) return std::unexpected(error.error());
    if (error->has_value() && !(**error).empty()) {
        return fail(ErrorCode::io_error, **error);
    }

    HttpResponse response;
    response.status_code = phoneme_ios_https_get_status_code(handle.value);
    if (response.status_code < 100) {
        return fail(ErrorCode::io_error,
                    "HTTPS bridge returned an invalid status code");
    }
    auto reason = copy_apple_https_string(handle.value, 1);
    auto headers = copy_apple_https_string(handle.value, 2);
    auto final_url = copy_apple_https_string(handle.value, 3);
    if (!reason) return std::unexpected(reason.error());
    if (!headers) return std::unexpected(headers.error());
    if (!final_url) return std::unexpected(final_url.error());
    response.reason = reason->value_or(std::string {});
    response.headers = parse_apple_https_headers(
        headers->value_or(std::string {}));
    response.final_url = request.url;
    if (final_url->has_value() && !(**final_url).empty()) {
        auto parsed = Url::parse(**final_url);
        if (parsed) response.final_url = std::move(*parsed);
    }

    const i32 body_length = phoneme_ios_https_copy_body(handle.value,
                                                        nullptr, 0);
    if (body_length < 0) {
        return fail(ErrorCode::io_error,
                    "HTTPS bridge could not report response body length");
    }
    response.body.resize(static_cast<usize>(body_length));
    if (body_length > 0) {
        const i32 copied = phoneme_ios_https_copy_body(
            handle.value, response.body.data(), body_length);
        if (copied != body_length) {
            return fail(ErrorCode::io_error,
                        "HTTPS bridge could not copy response body");
        }
    }

    auto protocol_name = copy_apple_https_string(handle.value, 4);
    auto protocol_version = copy_apple_https_string(handle.value, 5);
    auto cipher_suite = copy_apple_https_string(handle.value, 6);
    auto certificate_subject = copy_apple_https_string(handle.value, 7);
    auto certificate_issuer = copy_apple_https_string(handle.value, 8);
    auto certificate_serial = copy_apple_https_string(handle.value, 9);
    if (!protocol_name) return std::unexpected(protocol_name.error());
    if (!protocol_version) return std::unexpected(protocol_version.error());
    if (!cipher_suite) return std::unexpected(cipher_suite.error());
    if (!certificate_subject)
        return std::unexpected(certificate_subject.error());
    if (!certificate_issuer)
        return std::unexpected(certificate_issuer.error());
    if (!certificate_serial)
        return std::unexpected(certificate_serial.error());
    if (protocol_name->has_value() || protocol_version->has_value() ||
        cipher_suite->has_value() || certificate_subject->has_value() ||
        certificate_issuer->has_value() || certificate_serial->has_value()) {
        response.security = SecurityMetadata {
            .protocol_name = protocol_name->value_or(std::string {}),
            .protocol_version = protocol_version->value_or(std::string {}),
            .cipher_suite = cipher_suite->value_or(std::string {}),
            .certificate_subject = certificate_subject->value_or(
                std::string {}),
            .certificate_issuer = certificate_issuer->value_or(
                std::string {}),
            .certificate_serial = certificate_serial->value_or(
                std::string {}),
            .certificate_not_before = phoneme_ios_https_get_long(
                handle.value, 1),
            .certificate_not_after = phoneme_ios_https_get_long(
                handle.value, 2),
        };
    }
    return response;
}
#endif

[[nodiscard]] Result<HttpResponse> perform_plain_http(HttpRequest request) {
    if (request.url.scheme == Scheme::https) {
#if defined(__APPLE__)
        return perform_apple_https(request);
#else
        return fail(ErrorCode::unsupported_feature,
                    "HTTPS requires a platform TLS AsyncNetworkAdapter");
#endif
    }
    if (request.url.scheme != Scheme::http) {
        return fail(ErrorCode::invalid_argument,
                    "HTTP adapter received a non-HTTP URL");
    }

    auto descriptor = connect_stream_descriptor(request.url,
                                                request.timeout_ms);
    if (!descriptor) return std::unexpected(descriptor.error());
    const int socket = *descriptor;

    std::string request_text = request.method + " " +
                               request.url.request_target() +
                               " HTTP/1.1\r\n";
    bool has_host = false;
    bool has_length = false;
    bool has_connection = false;
    for (const auto& [name, value] : request.headers) {
        const std::string normalized = lowercase(name);
        has_host = has_host || normalized == "host";
        has_length = has_length || normalized == "content-length";
        has_connection = has_connection || normalized == "connection";
        request_text.append(name);
        request_text.append(": ");
        request_text.append(value);
        request_text.append("\r\n");
    }
    if (!has_host) {
        request_text.append("Host: ");
        request_text.append(request.url.authority());
        request_text.append("\r\n");
    }
    if (!has_connection) request_text.append("Connection: close\r\n");
    if (!request.body.empty() && !has_length) {
        request_text.append("Content-Length: ");
        request_text.append(std::to_string(request.body.size()));
        request_text.append("\r\n");
    }
    request_text.append("\r\n");

    std::vector<u8> wire;
    wire.reserve(request_text.size() + request.body.size());
    wire.insert(wire.end(), request_text.begin(), request_text.end());
    wire.insert(wire.end(), request.body.begin(), request.body.end());
    auto sent = send_all(socket, wire, request.timeout_ms);
    if (!sent) {
        ::close(socket);
        return std::unexpected(sent.error());
    }
    auto received = receive_all(socket, request.timeout_ms);
    ::close(socket);
    if (!received) return std::unexpected(received.error());

    auto response = parse_http_response(request.url, *received);
    if (!response) return std::unexpected(response.error());
    const bool redirect = response->status_code == 301 ||
                          response->status_code == 302 ||
                          response->status_code == 303 ||
                          response->status_code == 307 ||
                          response->status_code == 308;
    if (!redirect || request.redirect_limit == 0U) return response;
    auto location = header_value(response->headers, "Location");
    if (!location.has_value()) return response;
    auto redirected = Url::resolve(request.url, *location);
    if (!redirected) return std::unexpected(redirected.error());
    request.url = std::move(*redirected);
    --request.redirect_limit;
    if (response->status_code == 303 ||
        ((response->status_code == 301 || response->status_code == 302) &&
         request.method == "POST")) {
        request.method = "GET";
        request.body.clear();
    }
    return perform_plain_http(std::move(request));
}

class PosixNetworkAdapter final : public AsyncNetworkAdapter {
public:
    ~PosixNetworkAdapter() override {
        std::unordered_map<u64, int> handles;
        {
            std::scoped_lock lock(mutex_);
            handles.swap(handles_);
        }
        for (const auto& [unused, descriptor] : handles) {
            (void)unused;
            ::close(descriptor);
        }
    }

    Result<OperationId> open_stream(
        const Url& url,
        i32 timeout_ms,
        Completion<NativeConnection> completion) override {
        auto descriptor = connect_stream_descriptor(url, timeout_ms);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto connection = store_connection(*descriptor, true);
        if (!connection) {
            ::close(*descriptor);
            return finish(std::move(completion),
                          std::unexpected(connection.error()));
        }
        return finish(std::move(completion), std::move(*connection));
    }

    Result<OperationId> open_server(
        const Url& url,
        i32,
        Completion<NativeConnection> completion) override {
        auto descriptor = create_server_descriptor(url);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto connection = store_connection(*descriptor, false);
        if (!connection) {
            ::close(*descriptor);
            return finish(std::move(completion),
                          std::unexpected(connection.error()));
        }
        return finish(std::move(completion), std::move(*connection));
    }

    Result<OperationId> accept(
        NativeHandle server,
        i32 timeout_ms,
        Completion<NativeConnection> completion) override {
        auto descriptor = lookup(server);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto ready = wait_fd(*descriptor, POLLIN, timeout_ms);
        if (!ready) return finish(std::move(completion),
                                  std::unexpected(ready.error()));
        sockaddr_storage address {};
        socklen_t length = static_cast<socklen_t>(sizeof(address));
        const int accepted = ::accept(
            *descriptor, reinterpret_cast<sockaddr*>(&address), &length);
        if (accepted < 0) {
            return finish<NativeConnection>(std::move(completion),
                                            io_failure("accept failed"));
        }
        configure_descriptor(accepted);
        auto connection = store_connection(accepted, true);
        if (!connection) {
            ::close(accepted);
            return finish(std::move(completion),
                          std::unexpected(connection.error()));
        }
        return finish(std::move(completion), std::move(*connection));
    }

    Result<OperationId> open_datagram(
        const Url& url,
        i32,
        Completion<NativeConnection> completion) override {
        auto descriptor = create_datagram_descriptor(url);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto connection = store_connection(*descriptor, !url.host.empty());
        if (!connection) {
            ::close(*descriptor);
            return finish(std::move(completion),
                          std::unexpected(connection.error()));
        }
        return finish(std::move(completion), std::move(*connection));
    }

    Result<OperationId> read(
        NativeHandle handle,
        usize maximum_bytes,
        i32 timeout_ms,
        Completion<std::vector<u8>> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        if (maximum_bytes == 0U) {
            return finish(std::move(completion), std::vector<u8> {});
        }
        auto ready = wait_fd(*descriptor, POLLIN, timeout_ms);
        if (!ready) return finish(std::move(completion),
                                  std::unexpected(ready.error()));
        std::vector<u8> bytes(std::min(maximum_bytes, kIoChunkSize));
        while (true) {
            const ssize_t count = ::recv(*descriptor, bytes.data(),
                                         bytes.size(), 0);
            if (count >= 0) {
                bytes.resize(static_cast<usize>(count));
                return finish(std::move(completion), std::move(bytes));
            }
            if (errno != EINTR) {
                return finish<std::vector<u8>>(
                    std::move(completion),
                    io_failure("socket read failed"));
            }
        }
    }

    Result<OperationId> write(
        NativeHandle handle,
        std::vector<u8> bytes,
        i32 timeout_ms,
        Completion<usize> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto written = send_all(*descriptor, bytes, timeout_ms);
        if (!written) return finish(std::move(completion),
                                    std::unexpected(written.error()));
        return finish(std::move(completion), bytes.size());
    }

    Result<OperationId> available(
        NativeHandle handle,
        Completion<usize> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        int count = 0;
        if (::ioctl(*descriptor, FIONREAD, &count) != 0) {
            return finish<usize>(std::move(completion),
                                 io_failure("FIONREAD failed"));
        }
        return finish(std::move(completion),
                      static_cast<usize>(std::max(count, 0)));
    }

    Result<OperationId> send_datagram(
        NativeHandle handle,
        DatagramPacket packet,
        i32 timeout_ms,
        Completion<usize> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto ready = wait_fd(*descriptor, POLLOUT, timeout_ms);
        if (!ready) return finish(std::move(completion),
                                  std::unexpected(ready.error()));
        ssize_t sent = -1;
        if (packet.peer.host.empty()) {
            sent = ::send(*descriptor, packet.bytes.data(),
                          packet.bytes.size(), 0);
        } else {
            auto addresses = resolve_addresses(packet.peer.host,
                                               packet.peer.port,
                                               SOCK_DGRAM, false);
            if (!addresses) return finish(std::move(completion),
                                          std::unexpected(addresses.error()));
            for (addrinfo* address = addresses->value;
                 address != nullptr;
                 address = address->ai_next) {
                sent = ::sendto(*descriptor, packet.bytes.data(),
                                packet.bytes.size(), 0,
                                address->ai_addr,
                                static_cast<socklen_t>(address->ai_addrlen));
                if (sent >= 0) break;
            }
        }
        if (sent < 0) {
            return finish<usize>(std::move(completion),
                                 io_failure("UDP send failed"));
        }
        return finish(std::move(completion), static_cast<usize>(sent));
    }

    Result<OperationId> receive_datagram(
        NativeHandle handle,
        usize maximum_bytes,
        i32 timeout_ms,
        Completion<DatagramPacket> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        auto ready = wait_fd(*descriptor, POLLIN, timeout_ms);
        if (!ready) return finish(std::move(completion),
                                  std::unexpected(ready.error()));
        DatagramPacket packet;
        packet.bytes.resize(std::min(maximum_bytes, static_cast<usize>(65'507)));
        sockaddr_storage peer {};
        socklen_t peer_length = static_cast<socklen_t>(sizeof(peer));
        const ssize_t count = ::recvfrom(
            *descriptor, packet.bytes.data(), packet.bytes.size(), 0,
            reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (count < 0) {
            return finish<DatagramPacket>(
                std::move(completion),
                io_failure("UDP receive failed"));
        }
        packet.bytes.resize(static_cast<usize>(count));
        auto endpoint = endpoint_from_sockaddr(
            reinterpret_cast<const sockaddr*>(&peer), peer_length);
        if (!endpoint) return finish(std::move(completion),
                                     std::unexpected(endpoint.error()));
        packet.peer = std::move(*endpoint);
        return finish(std::move(completion), std::move(packet));
    }

    Result<OperationId> perform_http(
        HttpRequest request,
        Completion<HttpResponse> completion) override {
        auto response = perform_plain_http(std::move(request));
        if (!response) return finish(std::move(completion),
                                     std::unexpected(response.error()));
        return finish(std::move(completion), std::move(*response));
    }

    Result<OperationId> set_socket_option(
        NativeHandle handle,
        SocketOption option,
        i32 value,
        Completion<bool> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        int level = SOL_SOCKET;
        int name = 0;
        int integer = value;
        linger linger_value {};
        const void* pointer = &integer;
        socklen_t length = static_cast<socklen_t>(sizeof(integer));
        switch (option) {
        case SocketOption::delay:
            level = IPPROTO_TCP;
            name = TCP_NODELAY;
            integer = value == 0 ? 0 : 1;
            break;
        case SocketOption::linger:
            name = SO_LINGER;
            linger_value.l_onoff = value > 0 ? 1 : 0;
            linger_value.l_linger = std::max(value, 0);
            pointer = &linger_value;
            length = static_cast<socklen_t>(sizeof(linger_value));
            break;
        case SocketOption::keep_alive:
            name = SO_KEEPALIVE;
            integer = value == 0 ? 0 : 1;
            break;
        case SocketOption::receive_buffer:
            if (value <= 0) return finish<bool>(
                std::move(completion),
                fail(ErrorCode::invalid_argument,
                     "receive buffer must be positive"));
            name = SO_RCVBUF;
            break;
        case SocketOption::send_buffer:
            if (value <= 0) return finish<bool>(
                std::move(completion),
                fail(ErrorCode::invalid_argument,
                     "send buffer must be positive"));
            name = SO_SNDBUF;
            break;
        }
        if (::setsockopt(*descriptor, level, name, pointer, length) != 0) {
            return finish<bool>(std::move(completion),
                                io_failure("setsockopt failed"));
        }
        return finish(std::move(completion), true);
    }

    Result<OperationId> get_socket_option(
        NativeHandle handle,
        SocketOption option,
        Completion<i32> completion) override {
        auto descriptor = lookup(handle);
        if (!descriptor) return finish(std::move(completion),
                                       std::unexpected(descriptor.error()));
        int level = SOL_SOCKET;
        int name = 0;
        int integer = 0;
        linger linger_value {};
        void* pointer = &integer;
        socklen_t length = static_cast<socklen_t>(sizeof(integer));
        switch (option) {
        case SocketOption::delay:
            level = IPPROTO_TCP;
            name = TCP_NODELAY;
            break;
        case SocketOption::linger:
            name = SO_LINGER;
            pointer = &linger_value;
            length = static_cast<socklen_t>(sizeof(linger_value));
            break;
        case SocketOption::keep_alive:
            name = SO_KEEPALIVE;
            break;
        case SocketOption::receive_buffer:
            name = SO_RCVBUF;
            break;
        case SocketOption::send_buffer:
            name = SO_SNDBUF;
            break;
        }
        if (::getsockopt(*descriptor, level, name, pointer, &length) != 0) {
            return finish<i32>(std::move(completion),
                               io_failure("getsockopt failed"));
        }
        if (option == SocketOption::linger) {
            integer = linger_value.l_onoff == 0 ? 0
                                                : linger_value.l_linger;
        }
        return finish(std::move(completion), static_cast<i32>(integer));
    }

    Status close(NativeHandle handle) override {
        int descriptor = -1;
        {
            std::scoped_lock lock(mutex_);
            const auto found = handles_.find(handle.value);
            if (found == handles_.end()) return {};
            descriptor = found->second;
            handles_.erase(found);
        }
        if (descriptor >= 0) {
            (void)::shutdown(descriptor, SHUT_RDWR);
            ::close(descriptor);
        }
        return {};
    }

    Status cancel(OperationId) override {
        return {};
    }

private:
    template <typename T, typename U>
    [[nodiscard]] Result<OperationId> finish(Completion<T> completion,
                                             U&& value) {
        Result<T> result(std::forward<U>(value));
        const OperationId operation {next_operation_.fetch_add(1U)};
        if (completion) completion(std::move(result));
        return operation;
    }

    [[nodiscard]] Result<NativeConnection> store_connection(
        int descriptor,
        bool has_peer) {
        const NativeHandle handle {next_handle_.fetch_add(1U)};
        {
            std::scoped_lock lock(mutex_);
            handles_.insert_or_assign(handle.value, descriptor);
        }
        auto local = socket_endpoint(descriptor, false);
        if (!local) {
            (void)close(handle);
            return std::unexpected(local.error());
        }
        Endpoint remote;
        if (has_peer) {
            auto peer = socket_endpoint(descriptor, true);
            if (!peer) {
                (void)close(handle);
                return std::unexpected(peer.error());
            }
            remote = std::move(*peer);
        }
        return NativeConnection {
            .handle = handle,
            .local = std::move(*local),
            .remote = std::move(remote),
        };
    }

    [[nodiscard]] Result<int> lookup(NativeHandle handle) const {
        if (!handle.valid()) {
            return fail(ErrorCode::invalid_argument,
                        "native network handle is invalid");
        }
        std::scoped_lock lock(mutex_);
        const auto found = handles_.find(handle.value);
        if (found == handles_.end()) {
            return fail(ErrorCode::invalid_state,
                        "native network handle is closed");
        }
        return found->second;
    }

    mutable std::mutex mutex_;
    std::unordered_map<u64, int> handles_;
    std::atomic<u64> next_handle_ {1U};
    std::atomic<u64> next_operation_ {1U};
};

} // namespace

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return std::make_shared<PosixNetworkAdapter>();
}

} // namespace phoneme::network
