#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "phoneme/network/AsyncNetworkAdapter.hpp"

namespace phoneme::tests {

class FakeNetworkAdapter final : public network::AsyncNetworkAdapter {
public:
    [[nodiscard]] Result<network::OperationId> open_stream(
        const network::Url& url,
        i32,
        network::Completion<network::NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, false));
    }

    [[nodiscard]] Result<network::OperationId> open_server(
        const network::Url& url,
        i32,
        network::Completion<network::NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, true));
    }

    [[nodiscard]] Result<network::OperationId> accept(
        network::NativeHandle,
        i32,
        network::Completion<network::NativeConnection> completion) override {
        network::Url accepted;
        accepted.scheme = network::Scheme::socket;
        accepted.scheme_name = "socket";
        accepted.host = "client.test";
        accepted.port = 7001;
        return complete(std::move(completion),
                        create_connection(accepted, false));
    }

    [[nodiscard]] Result<network::OperationId> open_datagram(
        const network::Url& url,
        i32,
        network::Completion<network::NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, false));
    }

    [[nodiscard]] Result<network::OperationId> read(
        network::NativeHandle handle,
        usize maximum_bytes,
        i32,
        network::Completion<std::vector<u8>> completion) override {
        std::vector<u8> result;
        {
            std::scoped_lock lock(mutex_);
            auto found = handles_.find(handle.value);
            if (found == handles_.end()) {
                return complete<std::vector<u8>>(
                    std::move(completion),
                    fail(ErrorCode::invalid_state,
                         "fake network handle is closed"));
            }
            const usize count = std::min(maximum_bytes,
                                         found->second.read_buffer.size());
            result.insert(result.end(), found->second.read_buffer.begin(),
                          found->second.read_buffer.begin() +
                              static_cast<isize>(count));
            found->second.read_buffer.erase(
                found->second.read_buffer.begin(),
                found->second.read_buffer.begin() +
                    static_cast<isize>(count));
        }
        return complete(std::move(completion), std::move(result));
    }

    [[nodiscard]] Result<network::OperationId> write(
        network::NativeHandle handle,
        std::vector<u8> bytes,
        i32,
        network::Completion<usize> completion) override {
        const usize count = bytes.size();
        {
            std::scoped_lock lock(mutex_);
            auto found = handles_.find(handle.value);
            if (found == handles_.end()) {
                return complete<usize>(
                    std::move(completion),
                    fail(ErrorCode::invalid_state,
                         "fake network handle is closed"));
            }
            found->second.written.insert(found->second.written.end(),
                                         bytes.begin(), bytes.end());
            found->second.read_buffer.insert(found->second.read_buffer.end(),
                                             bytes.begin(), bytes.end());
        }
        return complete(std::move(completion), count);
    }

    [[nodiscard]] Result<network::OperationId> available(
        network::NativeHandle handle,
        network::Completion<usize> completion) override {
        std::scoped_lock lock(mutex_);
        const auto found = handles_.find(handle.value);
        if (found == handles_.end()) {
            return complete<usize>(
                std::move(completion),
                fail(ErrorCode::invalid_state,
                     "fake network handle is closed"));
        }
        return complete(std::move(completion),
                        found->second.read_buffer.size());
    }

    [[nodiscard]] Result<network::OperationId> send_datagram(
        network::NativeHandle handle,
        network::DatagramPacket packet,
        i32,
        network::Completion<usize> completion) override {
        const usize count = packet.bytes.size();
        {
            std::scoped_lock lock(mutex_);
            if (!handles_.contains(handle.value)) {
                return complete<usize>(
                    std::move(completion),
                    fail(ErrorCode::invalid_state,
                         "fake datagram handle is closed"));
            }
            last_datagram_ = std::move(packet);
        }
        return complete(std::move(completion), count);
    }

    [[nodiscard]] Result<network::OperationId> receive_datagram(
        network::NativeHandle handle,
        usize maximum_bytes,
        i32,
        network::Completion<network::DatagramPacket> completion) override {
        network::DatagramPacket packet;
        {
            std::scoped_lock lock(mutex_);
            if (!handles_.contains(handle.value)) {
                return complete<network::DatagramPacket>(
                    std::move(completion),
                    fail(ErrorCode::invalid_state,
                         "fake datagram handle is closed"));
            }
            if (!last_datagram_.has_value()) {
                return complete<network::DatagramPacket>(
                    std::move(completion),
                    fail(ErrorCode::io_error,
                         "fake datagram queue is empty"));
            }
            packet = *last_datagram_;
            packet.bytes.resize(std::min(maximum_bytes,
                                         packet.bytes.size()));
        }
        return complete(std::move(completion), std::move(packet));
    }

    [[nodiscard]] Result<network::OperationId> perform_http(
        network::HttpRequest request,
        network::Completion<network::HttpResponse> completion) override {
        network::HttpResponse response;
        response.final_url = request.url;
        response.status_code = 200;
        response.reason = "OK";
        response.headers = {
            {"Content-Length", "1"},
            {"Content-Type", "application/octet-stream"},
            {"X-Number", "7"},
            {"Date", "Sun, 06 Nov 1994 08:49:37 GMT"},
        };
        response.body = {
            static_cast<u8>(request.url.scheme == network::Scheme::https
                                ? 'S' : 'B'),
        };
        if (request.url.scheme == network::Scheme::https) {
            response.security = network::SecurityMetadata {
                .protocol_name = "TLS",
                .protocol_version = "TLSv1.3",
                .cipher_suite = "TLS_AES_128_GCM_SHA256",
                .certificate_subject = "CN=secure.test",
                .certificate_issuer = "CN=Fixture CA",
                .certificate_serial = "01AB",
                .certificate_not_before = 1'700'000'000'000LL,
                .certificate_not_after = 1'900'000'000'000LL,
            };
        }
        {
            std::scoped_lock lock(mutex_);
            last_http_request_ = request;
        }
        return complete(std::move(completion), std::move(response));
    }

    [[nodiscard]] Result<network::OperationId> set_socket_option(
        network::NativeHandle handle,
        network::SocketOption option,
        i32 value,
        network::Completion<bool> completion) override {
        std::scoped_lock lock(mutex_);
        auto found = handles_.find(handle.value);
        if (found == handles_.end()) {
            return complete<bool>(
                std::move(completion),
                fail(ErrorCode::invalid_state,
                     "fake network handle is closed"));
        }
        found->second.options.insert_or_assign(static_cast<i32>(option), value);
        return complete(std::move(completion), true);
    }

    [[nodiscard]] Result<network::OperationId> get_socket_option(
        network::NativeHandle handle,
        network::SocketOption option,
        network::Completion<i32> completion) override {
        std::scoped_lock lock(mutex_);
        const auto found = handles_.find(handle.value);
        if (found == handles_.end()) {
            return complete<i32>(
                std::move(completion),
                fail(ErrorCode::invalid_state,
                     "fake network handle is closed"));
        }
        const auto option_value = found->second.options.find(
            static_cast<i32>(option));
        return complete(std::move(completion),
                        option_value == found->second.options.end()
                            ? 0 : option_value->second);
    }

    [[nodiscard]] Status close(network::NativeHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (handles_.erase(handle.value) != 0U) ++close_count_;
        return {};
    }

    [[nodiscard]] Status cancel(network::OperationId) override {
        std::scoped_lock lock(mutex_);
        ++cancel_count_;
        return {};
    }

    [[nodiscard]] usize open_handle_count() const {
        std::scoped_lock lock(mutex_);
        return handles_.size();
    }

    [[nodiscard]] usize close_count() const {
        std::scoped_lock lock(mutex_);
        return close_count_;
    }

    [[nodiscard]] usize cancel_count() const {
        std::scoped_lock lock(mutex_);
        return cancel_count_;
    }

    [[nodiscard]] std::optional<network::HttpRequest>
    last_http_request() const {
        std::scoped_lock lock(mutex_);
        return last_http_request_;
    }

private:
    struct HandleState final {
        std::vector<u8> read_buffer;
        std::vector<u8> written;
        std::unordered_map<i32, i32> options;
    };

    template <typename T, typename U>
    [[nodiscard]] Result<network::OperationId> complete(
        network::Completion<T> completion,
        U&& value) {
        Result<T> result(std::forward<U>(value));
        const network::OperationId operation {next_operation_++};
        if (completion) completion(std::move(result));
        return operation;
    }

    [[nodiscard]] network::NativeConnection create_connection(
        const network::Url& url,
        bool server) {
        std::scoped_lock lock(mutex_);
        const network::NativeHandle handle {next_handle_++};
        handles_.insert_or_assign(handle.value, HandleState {});
        return network::NativeConnection {
            .handle = handle,
            .local = network::Endpoint {
                .host = "127.0.0.1",
                .port = static_cast<u16>(server ? url.effective_port() : 5000),
            },
            .remote = server
                ? network::Endpoint {}
                : network::Endpoint {
                      .host = url.host,
                      .port = url.effective_port(),
                  },
        };
    }

    mutable std::mutex mutex_;
    std::unordered_map<u64, HandleState> handles_;
    std::optional<network::DatagramPacket> last_datagram_;
    std::optional<network::HttpRequest> last_http_request_;
    u64 next_handle_ {1};
    u64 next_operation_ {1};
    usize close_count_ {0};
    usize cancel_count_ {0};
};

} // namespace phoneme::tests
