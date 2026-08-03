#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "phoneme/network/ConnectionRegistry.hpp"

namespace {

using namespace std::chrono_literals;
using phoneme::ErrorCode;
using phoneme::Result;
using phoneme::Status;
using phoneme::fail;
using phoneme::i32;
using phoneme::i64;
using phoneme::u8;
using phoneme::u16;
using phoneme::u64;
using phoneme::usize;
using namespace phoneme::network;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

class DeferredNetworkAdapter final : public AsyncNetworkAdapter {
public:
    Result<OperationId> open_stream(
        const Url& url,
        i32,
        Completion<NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, false));
    }

    Result<OperationId> open_server(
        const Url& url,
        i32,
        Completion<NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, true));
    }

    Result<OperationId> accept(
        NativeHandle,
        i32,
        Completion<NativeConnection> completion) override {
        std::scoped_lock lock(mutex_);
        const OperationId operation {next_operation_++};
        pending_accept_ = PendingAccept {
            .operation = operation,
            .completion = std::move(completion),
        };
        condition_.notify_all();
        return operation;
    }

    Result<OperationId> open_datagram(
        const Url& url,
        i32,
        Completion<NativeConnection> completion) override {
        return complete(std::move(completion), create_connection(url, false));
    }

    Result<OperationId> read(
        NativeHandle,
        usize,
        i32,
        Completion<std::vector<u8>> completion) override {
        std::scoped_lock lock(mutex_);
        const OperationId operation {next_operation_++};
        pending_read_ = PendingRead {
            .operation = operation,
            .completion = std::move(completion),
        };
        condition_.notify_all();
        return operation;
    }

    Result<OperationId> write(
        NativeHandle handle,
        std::vector<u8> bytes,
        i32,
        Completion<usize> completion) override {
        if (!contains(handle)) {
            return complete<usize>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete(std::move(completion), bytes.size());
    }

    Result<OperationId> available(
        NativeHandle handle,
        Completion<usize> completion) override {
        if (!contains(handle)) {
            return complete<usize>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete(std::move(completion), static_cast<usize>(0));
    }

    Result<OperationId> send_datagram(
        NativeHandle handle,
        DatagramPacket packet,
        i32,
        Completion<usize> completion) override {
        if (!contains(handle)) {
            return complete<usize>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete(std::move(completion), packet.bytes.size());
    }

    Result<OperationId> receive_datagram(
        NativeHandle handle,
        usize,
        i32,
        Completion<DatagramPacket> completion) override {
        if (!contains(handle)) {
            return complete<DatagramPacket>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete<DatagramPacket>(
            std::move(completion),
            fail(ErrorCode::io_error, "test datagram queue is empty"));
    }

    Result<OperationId> perform_http(
        HttpRequest request,
        Completion<HttpResponse> completion) override {
        std::scoped_lock lock(mutex_);
        const OperationId operation {next_operation_++};
        pending_http_ = PendingHttp {
            .operation = operation,
            .request = std::move(request),
            .completion = std::move(completion),
        };
        condition_.notify_all();
        return operation;
    }

    Result<OperationId> set_socket_option(
        NativeHandle handle,
        SocketOption,
        i32,
        Completion<bool> completion) override {
        if (!contains(handle)) {
            return complete<bool>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete(std::move(completion), true);
    }

    Result<OperationId> get_socket_option(
        NativeHandle handle,
        SocketOption,
        Completion<i32> completion) override {
        if (!contains(handle)) {
            return complete<i32>(
                std::move(completion),
                fail(ErrorCode::invalid_state, "test handle is closed"));
        }
        return complete(std::move(completion), 0);
    }

    Status shutdown_output(NativeHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (!handles_.contains(handle.value)) {
            return fail(ErrorCode::invalid_state,
                        "test handle is closed");
        }
        ++shutdown_output_count_;
        return {};
    }

    Status close(NativeHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (handles_.erase(handle.value) != 0U) ++close_count_;
        return {};
    }

    Status cancel(OperationId operation) override {
        std::scoped_lock lock(mutex_);
        ++cancel_count_;
        cancelled_[operation.value] = true;
        condition_.notify_all();
        return {};
    }

    bool wait_for_http() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] {
            return pending_http_.has_value();
        });
    }

    bool wait_for_accept() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] {
            return pending_accept_.has_value();
        });
    }

    bool wait_for_read() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] {
            return pending_read_.has_value();
        });
    }

    OperationId pending_http_operation() const {
        std::scoped_lock lock(mutex_);
        return pending_http_.has_value()
            ? pending_http_->operation
            : OperationId {};
    }

    OperationId pending_accept_operation() const {
        std::scoped_lock lock(mutex_);
        return pending_accept_.has_value()
            ? pending_accept_->operation
            : OperationId {};
    }

    OperationId pending_read_operation() const {
        std::scoped_lock lock(mutex_);
        return pending_read_.has_value()
            ? pending_read_->operation
            : OperationId {};
    }

    void complete_http_late(OperationId operation,
                            std::string content_length = "1") {
        Completion<HttpResponse> completion;
        HttpRequest request;
        {
            std::scoped_lock lock(mutex_);
            require(pending_http_.has_value() &&
                        pending_http_->operation == operation,
                    "find deferred HTTP operation");
            completion = std::move(pending_http_->completion);
            request = std::move(pending_http_->request);
            pending_http_.reset();
        }
        HttpResponse response;
        response.final_url = request.url;
        response.status_code = 200;
        response.reason = "OK";
        response.headers = {
            {"Content-Length", std::move(content_length)},
        };
        response.body = {static_cast<u8>('X')};
        completion(std::move(response));
    }

    void complete_accept_late(OperationId operation) {
        Completion<NativeConnection> completion;
        {
            std::scoped_lock lock(mutex_);
            require(pending_accept_.has_value() &&
                        pending_accept_->operation == operation,
                    "find deferred accept operation");
            completion = std::move(pending_accept_->completion);
            pending_accept_.reset();
        }
        auto url = Url::parse("socket://client.test:7001");
        require(url.has_value(), "parse accepted test URL");
        completion(create_connection(*url, false));
    }

    void complete_read_late(OperationId operation) {
        Completion<std::vector<u8>> completion;
        {
            std::scoped_lock lock(mutex_);
            require(pending_read_.has_value() &&
                        pending_read_->operation == operation,
                    "find deferred read operation");
            completion = std::move(pending_read_->completion);
            pending_read_.reset();
        }
        completion(std::vector<u8> {1U, 2U, 3U});
    }

    usize cancel_count() const {
        std::scoped_lock lock(mutex_);
        return cancel_count_;
    }

    usize close_count() const {
        std::scoped_lock lock(mutex_);
        return close_count_;
    }

    usize shutdown_output_count() const {
        std::scoped_lock lock(mutex_);
        return shutdown_output_count_;
    }

    usize open_handle_count() const {
        std::scoped_lock lock(mutex_);
        return handles_.size();
    }

private:
    struct PendingHttp final {
        OperationId operation;
        HttpRequest request;
        Completion<HttpResponse> completion;
    };

    struct PendingAccept final {
        OperationId operation;
        Completion<NativeConnection> completion;
    };

    struct PendingRead final {
        OperationId operation;
        Completion<std::vector<u8>> completion;
    };

    template <typename T, typename U>
    Result<OperationId> complete(Completion<T> completion, U&& value) {
        OperationId operation;
        {
            std::scoped_lock lock(mutex_);
            operation = OperationId {next_operation_++};
        }
        Result<T> result(std::forward<U>(value));
        if (completion) completion(std::move(result));
        return operation;
    }

    NativeConnection create_connection(const Url& url, bool server) {
        std::scoped_lock lock(mutex_);
        const NativeHandle handle {next_handle_++};
        handles_.insert_or_assign(handle.value, true);
        return NativeConnection {
            .handle = handle,
            .local = Endpoint {
                .host = "127.0.0.1",
                .port = static_cast<u16>(server ? url.effective_port() : 5000),
            },
            .remote = server
                ? Endpoint {}
                : Endpoint {
                      .host = url.host,
                      .port = url.effective_port(),
                  },
        };
    }

    bool contains(NativeHandle handle) const {
        std::scoped_lock lock(mutex_);
        return handles_.contains(handle.value);
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<u64, bool> handles_;
    std::unordered_map<u64, bool> cancelled_;
    std::optional<PendingHttp> pending_http_;
    std::optional<PendingAccept> pending_accept_;
    std::optional<PendingRead> pending_read_;
    u64 next_operation_ {1};
    u64 next_handle_ {1};
    usize cancel_count_ {0};
    usize close_count_ {0};
    usize shutdown_output_count_ {0};
};

void require_cancelled(const std::optional<Result<i32>>& result,
                       const char* message) {
    require(result.has_value() && !result->has_value(), message);
    require(result->error().code == ErrorCode::io_error,
            "cancelled network operation maps to I/O error");
    require(result->error().message.find("cancelled") != std::string::npos,
            "cancelled network operation has diagnostic message");
}

void test_close_all_cancels_http_and_drops_late_callback() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("https://example.test/data",
                                ConnectionMode::read, true);
    require(opened.has_value(), "open lazy HTTPS connection");
    require(registry.open_input(opened->token).has_value(),
            "open HTTPS input stream");

    std::optional<Result<i32>> response;
    std::thread worker([&] {
        response.emplace(registry.http_response_code(opened->token));
    });
    require(adapter->wait_for_http(), "HTTP operation becomes pending");
    const OperationId operation = adapter->pending_http_operation();
    require(operation.valid(), "pending HTTP operation has an ID");

    registry.close_all();
    worker.join();
    require_cancelled(response, "close_all cancels pending HTTP wait");
    require(adapter->cancel_count() == 1U,
            "close_all forwards HTTP cancellation to adapter");

    adapter->complete_http_late(operation);
    require_cancelled(response,
                      "late HTTP callback cannot replace cancellation result");
}

void test_pending_completion_does_not_own_adapter() {
    std::weak_ptr<DeferredNetworkAdapter> weak_adapter;
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    weak_adapter = adapter;
    {
        ConnectionRegistry registry(adapter);
        auto opened = registry.open("socket://example.test:7099",
                                    ConnectionMode::read_write, true);
        require(opened.has_value(),
                "open adapter-lifetime stream");
        require(registry.open_input(opened->token).has_value(),
                "open adapter-lifetime input stream");

        std::optional<Result<std::vector<u8>>> read_result;
        std::thread worker([&] {
            read_result.emplace(registry.read(opened->token, 8U));
        });
        require(adapter->wait_for_read(),
                "adapter-lifetime read becomes pending");
        registry.close_all();
        worker.join();
        require(read_result.has_value() && !read_result->has_value(),
                "adapter-lifetime read is cancelled");
    }

    // The deferred adapter intentionally retains the pending completion. That
    // completion must not retain the adapter in return: a strong capture forms
    // a cycle and may make the final release happen on the adapter's own worker
    // thread in the POSIX implementation, where destruction would self-join.
    adapter.reset();
    require(weak_adapter.expired(),
            "pending network completion does not retain its adapter");
}

void test_registry_rejects_malformed_content_length() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("http://example.test/length",
                                ConnectionMode::read, true);
    require(opened.has_value(), "open HTTP length test connection");
    require(registry.open_input(opened->token).has_value(),
            "open HTTP length input stream");

    std::optional<Result<i64>> length_result;
    std::thread worker([&] {
        length_result.emplace(
            registry.http_content_length(opened->token));
    });
    require(adapter->wait_for_http(),
            "HTTP length request becomes pending");
    adapter->complete_http_late(
        adapter->pending_http_operation(), "1x");
    worker.join();
    require(length_result.has_value() && length_result->has_value() &&
                **length_result == -1,
            "malformed Content-Length maps to unknown length");

    registry.close_all();
}

void test_close_cancels_accept_and_closes_late_handle() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto server = registry.open("socket://:12345",
                                ConnectionMode::read_write, true);
    require(server.has_value(), "open deferred-test server socket");
    require(adapter->open_handle_count() == 1U,
            "server socket owns one native handle");

    std::optional<Result<OpenedConnection>> accepted;
    std::thread worker([&] {
        accepted.emplace(registry.accept(server->token));
    });
    require(adapter->wait_for_accept(), "accept operation becomes pending");
    const OperationId operation = adapter->pending_accept_operation();

    require(registry.close(server->token).has_value(),
            "close server while accept is pending");
    worker.join();
    require(accepted.has_value() && !accepted->has_value(),
            "pending accept returns cancellation");
    require(accepted->error().code == ErrorCode::io_error,
            "cancelled accept maps to I/O error");
    require(adapter->cancel_count() == 1U,
            "server close forwards accept cancellation");
    require(adapter->close_count() == 1U,
            "server close releases listening handle");

    adapter->complete_accept_late(operation);
    require(adapter->close_count() == 2U,
            "late accepted handle is closed instead of leaked");
    require(adapter->open_handle_count() == 0U,
            "no native handles remain after late accept callback");
}

void test_stream_limits_and_input_close_cancellation() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("socket://example.test:7100",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open stream for stream-limit test");
    require(registry.open_input(opened->token).has_value(),
            "open first input stream");
    auto duplicate_input = registry.open_input(opened->token);
    require(!duplicate_input.has_value() &&
                duplicate_input.error().code == ErrorCode::io_error,
            "second input stream is rejected like phoneME");
    require(registry.open_output(opened->token).has_value(),
            "open first output stream");
    auto duplicate_output = registry.open_output(opened->token);
    require(!duplicate_output.has_value() &&
                duplicate_output.error().code == ErrorCode::io_error,
            "second output stream is rejected like phoneME");

    std::optional<Result<std::vector<u8>>> read_result;
    std::thread worker([&] {
        read_result.emplace(registry.read(opened->token, 16U));
    });
    require(adapter->wait_for_read(), "input-close read becomes pending");
    const OperationId operation = adapter->pending_read_operation();

    require(registry.close_input(opened->token).has_value(),
            "close input stream while read is pending");
    worker.join();
    require(read_result.has_value() && !read_result->has_value(),
            "input close cancels pending read");
    require(read_result->error().code == ErrorCode::io_error,
            "input-close cancellation maps to I/O error");
    require(adapter->cancel_count() == 1U,
            "input close forwards read cancellation");
    require(adapter->open_handle_count() == 1U,
            "input close keeps parent socket open");
    auto reopened_input = registry.open_input(opened->token);
    require(!reopened_input.has_value() &&
                reopened_input.error().code == ErrorCode::io_error,
            "closed input stream cannot be reopened");

    adapter->complete_read_late(operation);
    require(read_result.has_value() && !read_result->has_value(),
            "late read callback cannot replace input-close result");

    require(registry.close(opened->token).has_value(),
            "close logical connection while output stream remains");
    require(adapter->open_handle_count() == 1U,
            "open output stream keeps native socket alive");
    require(registry.close_output(opened->token).has_value(),
            "close final output stream");
    require(adapter->open_handle_count() == 0U,
            "final stream close releases native socket");
}

void test_output_close_sends_fin_and_is_not_reopenable() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("socket://example.test:7150",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open stream for output half-close test");
    require(registry.open_output(opened->token).has_value(),
            "open output stream before half-close");

    require(registry.close_output(opened->token).has_value(),
            "close output stream sends FIN");
    require(adapter->shutdown_output_count() == 1U,
            "output close performs exactly one native half-close");
    require(adapter->open_handle_count() == 1U,
            "output half-close keeps input side and connection alive");

    auto reopened_output = registry.open_output(opened->token);
    require(!reopened_output.has_value() &&
                reopened_output.error().code == ErrorCode::io_error,
            "closed output stream cannot be reopened");
    require(registry.close_output(opened->token).has_value(),
            "repeated output close is idempotent");
    require(adapter->shutdown_output_count() == 1U,
            "repeated output close does not send a second FIN");

    require(registry.close(opened->token).has_value(),
            "close half-closed logical connection");
    require(adapter->open_handle_count() == 0U,
            "closing parent releases half-closed native socket");
}

void test_interrupt_cancels_pending_read() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    std::atomic_bool interrupted {false};
    registry.set_blocking_hooks(NetworkBlockingHooks {
        .poll_cancellation = [&interrupted]() -> std::optional<phoneme::Error> {
            if (!interrupted.exchange(false, std::memory_order_acq_rel)) {
                return std::nullopt;
            }
            return phoneme::Error::make_java(
                "java/io/InterruptedIOException",
                "test network operation was interrupted");
        },
    });

    auto opened = registry.open("socket://example.test:7200",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open stream for interrupt test");
    require(registry.open_input(opened->token).has_value(),
            "open interrupt-test input stream");

    std::optional<Result<std::vector<u8>>> read_result;
    std::thread worker([&] {
        read_result.emplace(registry.read(opened->token, 16U));
    });
    require(adapter->wait_for_read(), "interrupt-test read becomes pending");
    const OperationId operation = adapter->pending_read_operation();
    interrupted.store(true, std::memory_order_release);
    worker.join();

    require(read_result.has_value() && !read_result->has_value(),
            "thread interruption aborts pending read");
    require(read_result->error().code == ErrorCode::java_exception &&
                read_result->error().java_exception_class ==
                    "java/io/InterruptedIOException",
            "interrupted read preserves Java exception type");
    require(adapter->cancel_count() == 1U,
            "interrupted read cancels adapter operation");

    adapter->complete_read_late(operation);
    require(read_result.has_value() && !read_result->has_value(),
            "late callback cannot replace interrupted result");
    registry.close_all();
}

void test_url_validation_and_resolution() {
    require(!Url::parse("http://example.test/a b").has_value(),
            "URL rejects raw spaces");
    require(!Url::parse("http://example.test/\r\nX-Test: injected")
                 .has_value(),
            "URL rejects raw control characters");
    require(!Url::parse("http://example.test\\evil/path").has_value(),
            "URL rejects backslash in authority");
    require(!Url::parse("socket://[]:1234").has_value(),
            "URL rejects empty bracketed IPv6 host");

    auto base = Url::parse("https://example.test/a/b/c?q=1#old");
    require(base.has_value(), "parse redirect resolution base URL");
    auto fragment = Url::resolve(*base, "#new");
    require(fragment.has_value() && fragment->path == "/a/b/c" &&
                fragment->query == "q=1" && fragment->fragment == "new",
            "fragment-only redirect preserves base path and query");
    auto trailing = Url::resolve(*base, "../next/");
    require(trailing.has_value() && trailing->path == "/a/next/" &&
                trailing->query.empty() && trailing->fragment.empty(),
            "relative redirect preserves significant trailing slash");
    require(!Url::resolve(*base, "../bad%2").has_value(),
            "relative redirect rejects malformed percent encoding");
}

void test_datagram_boundaries() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("datagram://example.test:7300",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open datagram boundary fixture");
    auto maximum = registry.maximum_datagram_length(opened->token);
    require(maximum.has_value() && *maximum == 65'507U,
            "datagram maximum length matches UDP payload limit");

    DatagramPacket oversized {
        .peer = Endpoint {},
        .bytes = std::vector<u8>(65'508U, 0x5AU),
    };
    auto sent = registry.send_datagram(opened->token, std::move(oversized));
    require(!sent.has_value() &&
                sent.error().code == ErrorCode::out_of_range,
            "oversized UDP datagram is rejected before adapter send");
    require(registry.close(opened->token).has_value(),
            "close datagram boundary fixture");
}

void test_http_output_stream_freezes_request_metadata() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("http://example.test/freeze",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open HTTP metadata-freeze fixture");
    require(registry.set_http_method(opened->token, "POST").has_value(),
            "set HTTP method before output stream opens");
    require(registry.set_http_header(opened->token, "X-Test", "before")
                .has_value(),
            "set HTTP header before output stream opens");
    require(registry.open_output(opened->token).has_value(),
            "open HTTP output stream to freeze metadata");

    require(registry.set_http_method(opened->token, "HEAD").has_value(),
            "method mutation after output open is ignored like phoneME");
    auto method = registry.http_method(opened->token);
    require(method.has_value() && *method == "POST",
            "output-open HTTP method remains unchanged");
    require(registry.set_http_header(opened->token, "X-Test", "after")
                .has_value(),
            "header mutation after output open is ignored like phoneME");
    auto header = registry.http_request_header(opened->token, "X-Test");
    require(header.has_value() && header->has_value() &&
                **header == "before",
            "output-open HTTP header remains unchanged");

    require(registry.close_output(opened->token).has_value(),
            "close HTTP output stream without socket half-close");
    require(adapter->shutdown_output_count() == 0U,
            "HTTP output close does not call raw socket shutdown");
    require(registry.close(opened->token).has_value(),
            "close HTTP metadata-freeze fixture");
}

void test_http_reconnect_policy() {
    {
        auto adapter = std::make_shared<DeferredNetworkAdapter>();
        ConnectionRegistry registry(adapter);
        auto opened = registry.open("http://example.test/post",
                                    ConnectionMode::read_write, true);
        require(opened.has_value(), "open HTTP POST reconnect fixture");
        auto unsupported = registry.set_http_method(opened->token, "PATCH");
        require(!unsupported.has_value() &&
                    unsupported.error().code == ErrorCode::io_error,
                "unsupported MIDP HTTP method is rejected as IOException");
        require(registry.set_http_method(opened->token, "post").has_value(),
                "HTTP method normalization accepts POST");

        std::optional<Result<i32>> response;
        std::thread worker([&] {
            response.emplace(registry.http_response_code(opened->token));
        });
        require(adapter->wait_for_http(),
                "committed HTTP POST becomes pending");

        auto reconnected = registry.reconnect(opened->token);
        require(!reconnected.has_value() &&
                    reconnected.error().code == ErrorCode::invalid_state,
                "reconnect refuses to replay committed HTTP POST");
        require(adapter->cancel_count() == 0U,
                "rejected POST reconnect leaves in-flight request untouched");

        registry.close_all();
        worker.join();
        require(response.has_value() && !response->has_value(),
                "closing POST fixture cancels the pending request");
    }

    {
        auto adapter = std::make_shared<DeferredNetworkAdapter>();
        ConnectionRegistry registry(adapter);
        auto opened = registry.open("http://example.test/get",
                                    ConnectionMode::read, true);
        require(opened.has_value(), "open HTTP GET reconnect fixture");

        std::optional<Result<i32>> response;
        std::thread worker([&] {
            response.emplace(registry.http_response_code(opened->token));
        });
        require(adapter->wait_for_http(),
                "committed HTTP GET becomes pending");
        require(registry.reconnect(opened->token).has_value(),
                "reconnect may retry idempotent HTTP GET");
        worker.join();
        require(response.has_value() && !response->has_value(),
                "GET reconnect cancels the previous request instance");
        require(adapter->cancel_count() == 1U,
                "GET reconnect forwards cancellation before retry");
        registry.close_all();
    }
}

void test_reconnect_cancels_pending_read() {
    auto adapter = std::make_shared<DeferredNetworkAdapter>();
    ConnectionRegistry registry(adapter);
    auto opened = registry.open("socket://example.test:7000",
                                ConnectionMode::read_write, true);
    require(opened.has_value(), "open stream for reconnect test");
    require(registry.open_input(opened->token).has_value(),
            "open stream input before pending read");

    std::optional<Result<std::vector<u8>>> read_result;
    std::thread worker([&] {
        read_result.emplace(registry.read(opened->token, 16U));
    });
    require(adapter->wait_for_read(), "read operation becomes pending");
    const OperationId operation = adapter->pending_read_operation();

    require(registry.reconnect(opened->token).has_value(),
            "reconnect succeeds after cancelling old read");
    worker.join();
    require(read_result.has_value() && !read_result->has_value(),
            "reconnect cancels pending read");
    require(read_result->error().code == ErrorCode::io_error,
            "cancelled read maps to I/O error");
    require(adapter->cancel_count() == 1U,
            "reconnect forwards pending read cancellation");
    require(adapter->close_count() == 1U,
            "reconnect closes old native handle");
    require(adapter->open_handle_count() == 1U,
            "reconnect installs one replacement handle");

    adapter->complete_read_late(operation);
    require(read_result.has_value() && !read_result->has_value(),
            "late read callback cannot replace cancellation result");

    require(registry.close(opened->token).has_value(),
            "close reconnected logical connection");
    require(registry.close_input(opened->token).has_value(),
            "close final input stream");
    require(adapter->open_handle_count() == 0U,
            "replacement native handle is released");
}

} // namespace

int main() {
    test_close_all_cancels_http_and_drops_late_callback();
    test_pending_completion_does_not_own_adapter();
    test_registry_rejects_malformed_content_length();
    test_close_cancels_accept_and_closes_late_handle();
    test_stream_limits_and_input_close_cancellation();
    test_output_close_sends_fin_and_is_not_reopenable();
    test_interrupt_cancels_pending_read();
    test_url_validation_and_resolution();
    test_datagram_boundaries();
    test_http_output_stream_freezes_request_metadata();
    test_http_reconnect_policy();
    test_reconnect_cancels_pending_read();
    std::cout << "Network async cancellation tests passed\n";
    return 0;
}
