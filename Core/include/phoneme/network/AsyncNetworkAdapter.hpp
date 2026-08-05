#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/network/Url.hpp"

namespace phoneme::network {

enum class ConnectionMode : i32 {
    read = 1,
    write = 2,
    read_write = 3,
};

enum class SocketOption : i32 {
    delay = 0,
    linger = 1,
    keep_alive = 2,
    receive_buffer = 3,
    send_buffer = 4,
};

struct OperationId final {
    u64 value {0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(OperationId, OperationId) noexcept = default;
};

struct NativeHandle final {
    u64 value {0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(NativeHandle, NativeHandle) noexcept = default;
};

struct Endpoint final {
    std::string host;
    u16 port {0};
};

struct NativeConnection final {
    NativeHandle handle;
    Endpoint local;
    Endpoint remote;
};

struct DatagramPacket final {
    Endpoint peer;
    std::vector<u8> bytes;
};

using Header = std::pair<std::string, std::string>;

struct HttpRequest final {
    Url url;
    std::string method {"GET"};
    std::vector<Header> headers;
    std::vector<u8> body;
    i32 timeout_ms {30'000};
    u8 redirect_limit {5};
};

struct SecurityMetadata final {
    std::string protocol_name;
    std::string protocol_version;
    std::string cipher_suite;
    std::string certificate_subject;
    std::string certificate_issuer;
    std::string certificate_serial;
    i64 certificate_not_before {0};
    i64 certificate_not_after {0};
};

struct HttpResponse final {
    Url final_url;
    i32 status_code {-1};
    std::string reason;
    std::vector<Header> headers;
    std::vector<u8> body;
    std::optional<SecurityMetadata> security;
};

template <typename T>
using Completion = std::function<void(Result<T>)>;

class AsyncNetworkAdapter {
public:
    virtual ~AsyncNetworkAdapter() = default;

    [[nodiscard]] virtual Result<OperationId> open_stream(
        const Url& url,
        i32 timeout_ms,
        Completion<NativeConnection> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> open_server(
        const Url& url,
        i32 timeout_ms,
        Completion<NativeConnection> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> accept(
        NativeHandle server,
        i32 timeout_ms,
        Completion<NativeConnection> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> open_datagram(
        const Url& url,
        i32 timeout_ms,
        Completion<NativeConnection> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> read(
        NativeHandle handle,
        usize maximum_bytes,
        i32 timeout_ms,
        Completion<std::vector<u8>> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> write(
        NativeHandle handle,
        std::vector<u8> bytes,
        i32 timeout_ms,
        Completion<usize> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> available(
        NativeHandle handle,
        Completion<usize> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> send_datagram(
        NativeHandle handle,
        DatagramPacket packet,
        i32 timeout_ms,
        Completion<usize> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> receive_datagram(
        NativeHandle handle,
        usize maximum_bytes,
        i32 timeout_ms,
        Completion<DatagramPacket> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> perform_http(
        HttpRequest request,
        Completion<HttpResponse> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> set_socket_option(
        NativeHandle handle,
        SocketOption option,
        i32 value,
        Completion<bool> completion) = 0;
    [[nodiscard]] virtual Result<OperationId> get_socket_option(
        NativeHandle handle,
        SocketOption option,
        Completion<i32> completion) = 0;

    [[nodiscard]] virtual Status shutdown_output(NativeHandle handle) = 0;
    [[nodiscard]] virtual Status close(NativeHandle handle) = 0;
    [[nodiscard]] virtual Status cancel(OperationId operation) = 0;

    // Test-only diagnostics have a neutral default so fake adapters and
    // production callers remain decoupled from POSIX pool internals.
    [[nodiscard]] virtual usize worker_count_for_tests() const noexcept {
        return 0U;
    }
};

[[nodiscard]] std::shared_ptr<AsyncNetworkAdapter>
make_posix_network_adapter();

namespace detail {

// Exposes only bounded-pool diagnostics to the host regression suite. Runtime
// code must not depend on the concrete POSIX adapter implementation.
[[nodiscard]] usize posix_network_worker_count_for_tests(
    const std::shared_ptr<AsyncNetworkAdapter>& adapter) noexcept;

} // namespace detail

} // namespace phoneme::network
