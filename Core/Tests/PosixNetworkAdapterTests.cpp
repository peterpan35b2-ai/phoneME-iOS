#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/network/HttpParser.hpp"

namespace {

using BridgeCompletion = void (*)(int32_t, void*);

struct PendingBridgeRequest final {
    BridgeCompletion completion {nullptr};
    void* context {nullptr};
};

std::mutex g_bridge_mutex;
std::unordered_map<int32_t, PendingBridgeRequest> g_pending;
std::atomic<int32_t> g_next_handle {1};
std::atomic<int> g_cancel_count {0};
std::atomic<int> g_close_count {0};
std::atomic<int32_t> g_redirect_limit {-1};
std::atomic_bool g_next_request_certificate_error {false};
std::atomic<int32_t> g_certificate_error_handle {0};
std::mutex g_collection_mutex;
std::condition_variable g_collection_condition;
bool g_block_collection {false};
bool g_collection_entered {false};

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void complete_bridge_request(int32_t handle) {
    PendingBridgeRequest request;
    {
        std::scoped_lock lock(g_bridge_mutex);
        const auto found = g_pending.find(handle);
        require(found != g_pending.end(), "find pending Apple HTTP request");
        request = found->second;
        g_pending.erase(found);
    }
    require(request.completion != nullptr, "Apple HTTP request has callback");
    request.completion(handle, request.context);
}

void block_bridge_collection() {
    std::scoped_lock lock(g_collection_mutex);
    g_collection_entered = false;
    g_block_collection = true;
}

void wait_for_bridge_collection() {
    std::unique_lock lock(g_collection_mutex);
    const bool entered = g_collection_condition.wait_for(
        lock, 2s, [] { return g_collection_entered; });
    require(entered, "Apple HTTP callback enters result collection");
}

void release_bridge_collection() {
    {
        std::scoped_lock lock(g_collection_mutex);
        g_block_collection = false;
    }
    g_collection_condition.notify_all();
}

template <typename T>
class AsyncResult final {
public:
    void complete(phoneme::Result<T> result) {
        {
            std::scoped_lock lock(mutex_);
            result_.emplace(std::move(result));
        }
        condition_.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout = 2s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] {
            return result_.has_value();
        });
    }

    std::optional<phoneme::Result<T>> take() {
        std::scoped_lock lock(mutex_);
        return std::move(result_);
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<phoneme::Result<T>> result_;
};

std::string bridge_string(int32_t handle, int32_t field) {
    if (field == 10 &&
        handle == g_certificate_error_handle.load(std::memory_order_acquire)) {
        return "The certificate for this server is invalid";
    }
    switch (field) {
    case 1: return "OK";
    case 2: return "Content-Type: text/plain\r\n";
    case 3: return "https://example.test/final";
    case 4: return "TLS";
    case 5: return "1.3";
    case 6: return "TLS_AES_128_GCM_SHA256";
    case 7: return "CN=example.test";
    case 8: return "Test Issuer";
    case 9: return "01";
    case 10: return {};
    default: return {};
    }
}

} // namespace

extern "C" {

int32_t phoneme_ios_https_execute_async(
    const char*,
    const char*,
    const char*,
    const uint8_t*,
    int32_t,
    int32_t,
    int32_t redirect_limit,
    int64_t,
    BridgeCompletion completion,
    void* context) {
    g_redirect_limit.store(redirect_limit, std::memory_order_release);
    const int32_t handle = g_next_handle.fetch_add(1);
    if (g_next_request_certificate_error.exchange(
            false, std::memory_order_acq_rel)) {
        g_certificate_error_handle.store(handle, std::memory_order_release);
    }
    std::scoped_lock lock(g_bridge_mutex);
    g_pending.insert_or_assign(handle,
                               PendingBridgeRequest {completion, context});
    return handle;
}

int32_t phoneme_ios_https_get_status_code(int32_t) {
    std::unique_lock lock(g_collection_mutex);
    if (g_block_collection) {
        g_collection_entered = true;
        g_collection_condition.notify_all();
        g_collection_condition.wait(
            lock, [] { return !g_block_collection; });
    }
    return 200;
}

int32_t phoneme_ios_https_copy_string(int32_t handle,
                                      int32_t field,
                                      char* destination,
                                      int32_t capacity) {
    const std::string value = bridge_string(handle, field);
    const auto length = static_cast<int32_t>(value.size());
    if (destination != nullptr) {
        if (capacity <= length) return -1;
        if (length > 0) std::memcpy(destination, value.data(), value.size());
        destination[length] = '\0';
    }
    return length;
}

int32_t phoneme_ios_https_copy_body(int32_t,
                                    uint8_t* destination,
                                    int32_t capacity) {
    constexpr char kBody[] = "OK";
    constexpr int32_t kLength = 2;
    if (destination != nullptr) {
        if (capacity < kLength) return -1;
        std::memcpy(destination, kBody, kLength);
    }
    return kLength;
}

int64_t phoneme_ios_https_get_long(int32_t handle, int32_t field) {
    if (field == 1) return 1'700'000'000LL;
    if (field == 2) return 1'900'000'000LL;
    if (field == 3 &&
        handle == g_certificate_error_handle.load(std::memory_order_acquire)) {
        return -1202;
    }
    return 0;
}

void phoneme_ios_https_cancel(int32_t) {
    g_cancel_count.fetch_add(1, std::memory_order_relaxed);
}

void phoneme_ios_https_close(int32_t) {
    g_close_count.fetch_add(1, std::memory_order_relaxed);
}

void phoneme_ios_https_clear_session(int64_t) {}

} // extern "C"

namespace {

std::vector<phoneme::u8> http_bytes(std::string_view text) {
    return std::vector<phoneme::u8>(text.begin(), text.end());
}

void test_http_parser_hardening() {
    auto url = phoneme::network::Url::parse("http://example.test/data");
    require(url.has_value(), "parse HTTP parser test URL");

    auto interim = http_bytes(
        "HTTP/1.1 100 Continue\r\nX-Interim: yes\r\n\r\n"
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    auto final_response =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", interim);
    require(final_response.has_value() &&
                final_response->status_code == 200 &&
                final_response->body == http_bytes("OK"),
            "HTTP parser skips interim 100 response");

    auto headerless_wire = http_bytes(
        "HTTP/1.1 100 Continue\r\n\r\n"
        "HTTP/1.1 204 No Content\r\n\r\n");
    auto headerless =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", headerless_wire);
    require(headerless.has_value() &&
                headerless->status_code == 204 &&
                headerless->headers.empty() && headerless->body.empty(),
            "HTTP parser accepts headerless interim and final responses");

    auto head_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nBODY");
    auto head_response =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "HEAD", head_wire);
    require(head_response.has_value() && head_response->body.empty(),
            "HEAD response never exposes a body");

    auto no_content_wire = http_bytes(
        "HTTP/1.1 204 No Content\r\nContent-Length: 4\r\n\r\nBODY");
    auto no_content =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", no_content_wire);
    require(no_content.has_value() && no_content->body.empty(),
            "204 response never exposes a body");

    auto chunked_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n0\r\nX-Trailer: yes\r\n\r\n");
    auto chunked = phoneme::network::detail::parse_http_response_bytes(
        *url, "GET", chunked_wire);
    require(chunked.has_value() &&
                chunked->body == http_bytes("Wiki"),
            "chunked response validates trailers and decodes body");

    auto repeated_transfer_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n0\r\n\r\n");
    auto repeated_transfer =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", repeated_transfer_wire);
    require(repeated_transfer.has_value() &&
                repeated_transfer->body == http_bytes("Wiki"),
            "repeated transfer codings preserve final chunked framing");

    auto connection_close_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\n"
        "Content-Length: 2\r\n\r\nBODY");
    auto connection_close =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", connection_close_wire);
    require(connection_close.has_value() &&
                connection_close->body == http_bytes("BODY"),
            "Transfer-Encoding overrides conflicting Content-Length framing");

    auto invalid_transfer_order_wire = http_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked, gzip\r\n\r\n");
    auto invalid_transfer_order =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", invalid_transfer_order_wire);
    require(!invalid_transfer_order.has_value(),
            "chunked transfer coding must be final");

    auto parameterized_chunked_wire = http_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked;foo=bar\r\n\r\n0\r\n\r\n");
    auto parameterized_chunked =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", parameterized_chunked_wire);
    require(!parameterized_chunked.has_value(),
            "chunked transfer coding parameters are rejected");

    auto truncated_chunked_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "0\r\n");
    auto truncated_chunked =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", truncated_chunked_wire);
    require(!truncated_chunked.has_value(),
            "chunked response requires final trailer terminator");

    auto malformed_length_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nContent-Length: 2x\r\n\r\nOK");
    auto malformed_length =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", malformed_length_wire);
    require(!malformed_length.has_value(),
            "Content-Length must be fully numeric");

    auto conflicting_length_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
        "Content-Length: 3\r\n\r\nOK!");
    auto conflicting_length =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", conflicting_length_wire);
    require(!conflicting_length.has_value(),
            "conflicting Content-Length headers are rejected");

    auto malformed_header_wire = http_bytes(
        "HTTP/1.1 200 OK\r\nBad Header: value\r\n\r\n");
    auto malformed_header =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", malformed_header_wire);
    require(!malformed_header.has_value(),
            "response header names reject whitespace");

    auto invalid_status_wire = http_bytes(
        "HTTP/1.1 700 Invalid\r\nContent-Length: 0\r\n\r\n");
    auto invalid_status =
        phoneme::network::detail::parse_http_response_bytes(
            *url, "GET", invalid_status_wire);
    require(!invalid_status.has_value(),
            "HTTP status code outside standard range is rejected");
}

void test_plain_http_uses_one_absolute_deadline(
    const std::shared_ptr<phoneme::network::AsyncNetworkAdapter>& adapter) {
    using phoneme::network::HttpRequest;
    using phoneme::network::NativeConnection;

    auto server_url = phoneme::network::Url::parse("socket://:0");
    require(server_url.has_value(), "parse drip-feed HTTP server URL");
    AsyncResult<NativeConnection> server_wait;
    auto server_operation = adapter->open_server(
        *server_url, 5'000,
        [&server_wait](phoneme::Result<NativeConnection> result) {
            server_wait.complete(std::move(result));
        });
    require(server_operation.has_value() && server_wait.wait_for(),
            "open drip-feed HTTP server");
    auto server_result = server_wait.take();
    require(server_result.has_value() && server_result->has_value(),
            "drip-feed HTTP server opens successfully");
    NativeConnection server = std::move(**server_result);

    std::string connect_host = server.local.host;
    if (connect_host.empty() || connect_host == "0.0.0.0") {
        connect_host = "127.0.0.1";
    } else if (connect_host == "::") {
        connect_host = "::1";
    }
    if (connect_host.find(':') != std::string::npos &&
        connect_host.front() != '[') {
        connect_host = "[" + connect_host + "]";
    }
    auto client_url = phoneme::network::Url::parse(
        "http://" + connect_host + ":" +
        std::to_string(server.local.port) + "/slow");
    require(client_url.has_value(), "parse drip-feed HTTP client URL");

    AsyncResult<NativeConnection> accepted_wait;
    auto accept_operation = adapter->accept(
        server.handle, 5'000,
        [&accepted_wait](phoneme::Result<NativeConnection> result) {
            accepted_wait.complete(std::move(result));
        });
    require(accept_operation.has_value(), "start drip-feed HTTP accept");

    std::optional<phoneme::Result<phoneme::network::HttpResponse>> client_result;
    std::chrono::milliseconds elapsed {0};
    std::thread client([&] {
        const auto started = std::chrono::steady_clock::now();
        client_result.emplace(
            phoneme::network::detail::perform_plain_http_request(
                HttpRequest {
                    .url = *client_url,
                    .method = "GET",
                    .timeout_ms = 250,
                    .redirect_limit = 0,
                }));
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
    });

    require(accepted_wait.wait_for(), "accept drip-feed HTTP client");
    auto accepted_result = accepted_wait.take();
    require(accepted_result.has_value() && accepted_result->has_value(),
            "drip-feed HTTP connection is accepted");
    NativeConnection accepted = std::move(**accepted_result);

    AsyncResult<std::vector<phoneme::u8>> request_wait;
    auto request_read = adapter->read(
        accepted.handle, 4'096U, 2'000,
        [&request_wait](phoneme::Result<std::vector<phoneme::u8>> result) {
            request_wait.complete(std::move(result));
        });
    require(request_read.has_value() && request_wait.wait_for(),
            "drip-feed server receives HTTP request");
    auto request_result = request_wait.take();
    require(request_result.has_value() && request_result->has_value() &&
                !(*request_result)->empty(),
            "drip-feed HTTP request contains bytes");

    const std::string first = "HTTP/1.1 200 OK\r\n\r\nA";
    AsyncResult<phoneme::usize> first_write_wait;
    auto first_write = adapter->write(
        accepted.handle,
        std::vector<phoneme::u8>(first.begin(), first.end()),
        2'000,
        [&first_write_wait](phoneme::Result<phoneme::usize> result) {
            first_write_wait.complete(std::move(result));
        });
    require(first_write.has_value() && first_write_wait.wait_for(),
            "send first drip-feed response chunk");

    std::this_thread::sleep_for(100ms);
    AsyncResult<phoneme::usize> second_write_wait;
    auto second_write = adapter->write(
        accepted.handle, std::vector<phoneme::u8> {'B'}, 2'000,
        [&second_write_wait](phoneme::Result<phoneme::usize> result) {
            second_write_wait.complete(std::move(result));
        });
    require(second_write.has_value() && second_write_wait.wait_for(),
            "send second drip-feed response chunk");

    client.join();
    require(client_result.has_value() && !client_result->has_value() &&
                client_result->error().code == phoneme::ErrorCode::io_error,
            "unframed HTTP response requires close before deadline");
    require(elapsed >= 180ms && elapsed < 320ms,
            "plain HTTP timeout is absolute and not reset per chunk");

    require(adapter->close(accepted.handle).has_value(),
            "close drip-feed accepted connection");

    AsyncResult<NativeConnection> framed_accept_wait;
    auto framed_accept = adapter->accept(
        server.handle, 5'000,
        [&framed_accept_wait](phoneme::Result<NativeConnection> result) {
            framed_accept_wait.complete(std::move(result));
        });
    require(framed_accept.has_value(),
            "start Content-Length HTTP accept");

    std::optional<phoneme::Result<phoneme::network::HttpResponse>>
        framed_client_result;
    std::chrono::milliseconds framed_elapsed {0};
    std::thread framed_client([&] {
        const auto started = std::chrono::steady_clock::now();
        framed_client_result.emplace(
            phoneme::network::detail::perform_plain_http_request(
                HttpRequest {
                    .url = *client_url,
                    .method = "POST",
                    .headers = {
                        {"Host", "evil.test"},
                        {"Connection", "keep-alive"},
                        {"Content-Length", "999"},
                        {"Transfer-Encoding", "chunked"},
                        {"X-Test", "kept"},
                    },
                    .body = {'X', 'Y'},
                    .timeout_ms = 1'000,
                    .redirect_limit = 0,
                }));
        framed_elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
    });

    require(framed_accept_wait.wait_for(),
            "accept Content-Length HTTP client");
    auto framed_accepted_result = framed_accept_wait.take();
    require(framed_accepted_result.has_value() &&
                framed_accepted_result->has_value(),
            "Content-Length HTTP connection is accepted");
    NativeConnection framed_accepted = std::move(**framed_accepted_result);

    AsyncResult<std::vector<phoneme::u8>> framed_request_wait;
    auto framed_request = adapter->read(
        framed_accepted.handle, 4'096U, 2'000,
        [&framed_request_wait](
            phoneme::Result<std::vector<phoneme::u8>> result) {
            framed_request_wait.complete(std::move(result));
        });
    require(framed_request.has_value() && framed_request_wait.wait_for(),
            "Content-Length server receives HTTP request");
    auto framed_request_result = framed_request_wait.take();
    require(framed_request_result.has_value() &&
                framed_request_result->has_value(),
            "serialized POST request is readable");
    const std::string serialized_request(
        (*framed_request_result)->begin(),
        (*framed_request_result)->end());
    require(serialized_request.find("POST /slow HTTP/1.1\r\n") == 0U &&
                serialized_request.find(
                    "Host: " + client_url->authority() + "\r\n") !=
                    std::string::npos &&
                serialized_request.find("Connection: close\r\n") !=
                    std::string::npos &&
                serialized_request.find("Content-Length: 2\r\n") !=
                    std::string::npos &&
                serialized_request.find("X-Test: kept\r\n") !=
                    std::string::npos &&
                serialized_request.find("evil.test") == std::string::npos &&
                serialized_request.find("Content-Length: 999") ==
                    std::string::npos &&
                serialized_request.find("Transfer-Encoding") ==
                    std::string::npos,
            "platform owns HTTP Host connection and body framing headers");

    const std::string framed_response =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    AsyncResult<phoneme::usize> framed_write_wait;
    auto framed_write = adapter->write(
        framed_accepted.handle,
        std::vector<phoneme::u8>(framed_response.begin(),
                                 framed_response.end()),
        2'000,
        [&framed_write_wait](phoneme::Result<phoneme::usize> result) {
            framed_write_wait.complete(std::move(result));
        });
    require(framed_write.has_value() && framed_write_wait.wait_for(),
            "send complete Content-Length response without FIN");

    framed_client.join();
    require(framed_client_result.has_value() &&
                framed_client_result->has_value() &&
                (*framed_client_result)->body ==
                    std::vector<phoneme::u8>({'O', 'K'}),
            "Content-Length response completes before peer closes");
    require(framed_elapsed < 500ms,
            "Content-Length response does not wait for timeout or FIN");

    require(adapter->close(framed_accepted.handle).has_value(),
            "close Content-Length accepted connection");
    require(adapter->close(server.handle).has_value(),
            "close drip-feed HTTP server");
}

void test_cross_authority_redirect_strips_credentials(
    const std::shared_ptr<phoneme::network::AsyncNetworkAdapter>& adapter) {
    using phoneme::network::HttpRequest;
    using phoneme::network::NativeConnection;

    const auto open_server = [&]() {
        auto url = phoneme::network::Url::parse("socket://:0");
        require(url.has_value(), "parse redirect server URL");
        AsyncResult<NativeConnection> wait;
        auto operation = adapter->open_server(
            *url, 5'000,
            [&wait](phoneme::Result<NativeConnection> result) {
                wait.complete(std::move(result));
            });
        require(operation.has_value() && wait.wait_for(),
                "open redirect test server");
        auto result = wait.take();
        require(result.has_value() && result->has_value(),
                "redirect test server opens successfully");
        return std::move(**result);
    };
    const auto http_url = [](const NativeConnection& server,
                             std::string_view path) {
        std::string host = server.local.host;
        if (host.empty() || host == "0.0.0.0") host = "127.0.0.1";
        else if (host == "::") host = "::1";
        if (host.find(':') != std::string::npos && host.front() != '[') {
            host = "[" + host + "]";
        }
        return phoneme::network::Url::parse(
            "http://" + host + ":" +
            std::to_string(server.local.port) + std::string(path));
    };
    const auto read_request = [&](NativeConnection connection,
                                  const char* description) {
        AsyncResult<std::vector<phoneme::u8>> wait;
        auto operation = adapter->read(
            connection.handle, 8'192U, 2'000,
            [&wait](phoneme::Result<std::vector<phoneme::u8>> result) {
                wait.complete(std::move(result));
            });
        require(operation.has_value() && wait.wait_for(), description);
        auto result = wait.take();
        require(result.has_value() && result->has_value(), description);
        return std::string((*result)->begin(), (*result)->end());
    };
    const auto write_response = [&](NativeConnection connection,
                                    std::string_view response,
                                    const char* description) {
        AsyncResult<phoneme::usize> wait;
        auto operation = adapter->write(
            connection.handle,
            std::vector<phoneme::u8>(response.begin(), response.end()),
            2'000,
            [&wait](phoneme::Result<phoneme::usize> result) {
                wait.complete(std::move(result));
            });
        require(operation.has_value() && wait.wait_for(), description);
        auto result = wait.take();
        require(result.has_value() && result->has_value() &&
                    **result == response.size(),
                description);
    };

    NativeConnection first_server = open_server();
    NativeConnection second_server = open_server();
    auto first_url = http_url(first_server, "/start");
    auto second_url = http_url(second_server, "/next");
    require(first_url.has_value() && second_url.has_value(),
            "build cross-authority redirect URLs");

    AsyncResult<NativeConnection> first_accept_wait;
    auto first_accept = adapter->accept(
        first_server.handle, 5'000,
        [&first_accept_wait](phoneme::Result<NativeConnection> result) {
            first_accept_wait.complete(std::move(result));
        });
    AsyncResult<NativeConnection> second_accept_wait;
    auto second_accept = adapter->accept(
        second_server.handle, 5'000,
        [&second_accept_wait](phoneme::Result<NativeConnection> result) {
            second_accept_wait.complete(std::move(result));
        });
    require(first_accept.has_value() && second_accept.has_value(),
            "start both redirect accepts");

    std::optional<phoneme::Result<phoneme::network::HttpResponse>> client_result;
    std::thread client([&] {
        client_result.emplace(
            phoneme::network::detail::perform_plain_http_request(
                HttpRequest {
                    .url = *first_url,
                    .method = "GET",
                    .headers = {
                        {"Authorization", "Bearer secret"},
                        {"Proxy-Authorization", "Basic proxy-secret"},
                        {"Cookie", "session=secret"},
                        {"X-Keep", "yes"},
                    },
                    .timeout_ms = 2'000,
                    .redirect_limit = 2,
                }));
    });

    require(first_accept_wait.wait_for(), "accept initial redirect request");
    auto first_connection_result = first_accept_wait.take();
    require(first_connection_result.has_value() &&
                first_connection_result->has_value(),
            "initial redirect connection succeeds");
    NativeConnection first_connection = std::move(**first_connection_result);
    const std::string initial_request = read_request(
        first_connection, "read initial redirect request");
    require(initial_request.find("GET /start HTTP/1.1\r\n") == 0U &&
                initial_request.find("Authorization: Bearer secret\r\n") !=
                    std::string::npos &&
                initial_request.find(
                    "Proxy-Authorization: Basic proxy-secret\r\n") !=
                    std::string::npos &&
                initial_request.find("Cookie: session=secret\r\n") !=
                    std::string::npos,
            "initial authority receives explicitly supplied credentials");
    const std::string redirect_response =
        "HTTP/1.1 302 Found\r\nLocation: " + second_url->to_string() +
        "\r\nContent-Length: 0\r\n\r\n";
    write_response(first_connection, redirect_response,
                   "send cross-authority redirect response");

    require(second_accept_wait.wait_for(),
            "accept redirected request on second authority");
    auto second_connection_result = second_accept_wait.take();
    require(second_connection_result.has_value() &&
                second_connection_result->has_value(),
            "redirected connection succeeds");
    NativeConnection second_connection = std::move(**second_connection_result);
    const std::string redirected_request = read_request(
        second_connection, "read redirected request");
    require(redirected_request.find("GET /next HTTP/1.1\r\n") == 0U &&
                redirected_request.find("X-Keep: yes\r\n") !=
                    std::string::npos &&
                redirected_request.find("Authorization:") ==
                    std::string::npos &&
                redirected_request.find("Proxy-Authorization:") ==
                    std::string::npos &&
                redirected_request.find("Cookie:") == std::string::npos,
            "cross-authority redirect strips sensitive credentials only");
    write_response(second_connection,
                   "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK",
                   "send final redirected response");

    client.join();
    require(client_result.has_value() && client_result->has_value() &&
                (*client_result)->body ==
                    std::vector<phoneme::u8>({'O', 'K'}) &&
                (*client_result)->final_url.to_string() ==
                    second_url->to_string(),
            "cross-authority redirect completes with final URL and body");

    require(adapter->close(first_connection.handle).has_value(),
            "close initial redirect connection");
    require(adapter->close(second_connection.handle).has_value(),
            "close redirected connection");
    require(adapter->close(first_server.handle).has_value(),
            "close initial redirect server");
    require(adapter->close(second_server.handle).has_value(),
            "close final redirect server");
}

void test_posix_udp_roundtrip(
    const std::shared_ptr<phoneme::network::AsyncNetworkAdapter>& adapter) {
    using phoneme::network::DatagramPacket;
    using phoneme::network::NativeConnection;

    auto receiver_url = phoneme::network::Url::parse("datagram://:0");
    require(receiver_url.has_value(), "parse UDP receiver URL");
    AsyncResult<NativeConnection> receiver_wait;
    auto receiver_operation = adapter->open_datagram(
        *receiver_url, 5'000,
        [&receiver_wait](phoneme::Result<NativeConnection> result) {
            receiver_wait.complete(std::move(result));
        });
    require(receiver_operation.has_value() && receiver_wait.wait_for(),
            "open UDP receiver on worker pool");
    auto receiver_result = receiver_wait.take();
    require(receiver_result.has_value() && receiver_result->has_value(),
            "UDP receiver opens successfully");
    NativeConnection receiver = std::move(**receiver_result);

    std::string connect_host = receiver.local.host;
    if (connect_host.empty() || connect_host == "0.0.0.0") {
        connect_host = "127.0.0.1";
    } else if (connect_host == "::") {
        connect_host = "::1";
    }
    if (connect_host.find(':') != std::string::npos &&
        connect_host.front() != '[') {
        connect_host = "[" + connect_host + "]";
    }
    auto sender_url = phoneme::network::Url::parse(
        "datagram://" + connect_host + ":" +
        std::to_string(receiver.local.port));
    require(sender_url.has_value(), "parse UDP sender URL");

    AsyncResult<NativeConnection> sender_wait;
    auto sender_operation = adapter->open_datagram(
        *sender_url, 5'000,
        [&sender_wait](phoneme::Result<NativeConnection> result) {
            sender_wait.complete(std::move(result));
        });
    require(sender_operation.has_value() && sender_wait.wait_for(),
            "open connected UDP sender");
    auto sender_result = sender_wait.take();
    require(sender_result.has_value() && sender_result->has_value(),
            "UDP sender opens successfully");
    NativeConnection sender = std::move(**sender_result);

    AsyncResult<DatagramPacket> receive_wait;
    auto receive_operation = adapter->receive_datagram(
        receiver.handle, 3U, 5'000,
        [&receive_wait](phoneme::Result<DatagramPacket> result) {
            receive_wait.complete(std::move(result));
        });
    require(receive_operation.has_value(), "start UDP receive");

    std::vector<phoneme::u8> payload {1U, 2U, 3U, 4U, 5U};
    AsyncResult<phoneme::usize> send_wait;
    auto send_operation = adapter->send_datagram(
        sender.handle,
        DatagramPacket {.peer = {}, .bytes = payload},
        5'000,
        [&send_wait](phoneme::Result<phoneme::usize> result) {
            send_wait.complete(std::move(result));
        });
    require(send_operation.has_value() && send_wait.wait_for(),
            "send connected UDP datagram");
    auto send_result = send_wait.take();
    require(send_result.has_value() && send_result->has_value() &&
                **send_result == payload.size(),
            "UDP sender reports complete datagram length");

    require(receive_wait.wait_for(), "receive UDP datagram");
    auto received_result = receive_wait.take();
    require(received_result.has_value() && received_result->has_value(),
            "UDP receiver completes successfully");
    DatagramPacket received = std::move(**received_result);
    require(received.bytes == std::vector<phoneme::u8>({1U, 2U, 3U}),
            "UDP receive truncates to destination buffer without overflow");
    require(!received.peer.host.empty() &&
                received.peer.port == sender.local.port,
            "UDP receive preserves source address and port");

    std::vector<phoneme::u8> reply {9U, 8U, 7U};
    AsyncResult<phoneme::usize> reply_send_wait;
    auto reply_send = adapter->send_datagram(
        receiver.handle,
        DatagramPacket {.peer = received.peer, .bytes = reply},
        5'000,
        [&reply_send_wait](phoneme::Result<phoneme::usize> result) {
            reply_send_wait.complete(std::move(result));
        });
    require(reply_send.has_value() && reply_send_wait.wait_for(),
            "send UDP reply to captured source endpoint");

    AsyncResult<DatagramPacket> reply_receive_wait;
    auto reply_receive = adapter->receive_datagram(
        sender.handle, 16U, 5'000,
        [&reply_receive_wait](phoneme::Result<DatagramPacket> result) {
            reply_receive_wait.complete(std::move(result));
        });
    require(reply_receive.has_value() && reply_receive_wait.wait_for(),
            "connected UDP sender receives reply");
    auto reply_result = reply_receive_wait.take();
    require(reply_result.has_value() && reply_result->has_value() &&
                (**reply_result).bytes == reply &&
                (**reply_result).peer.port == receiver.local.port,
            "UDP reply preserves payload and receiver source port");

    require(adapter->close(sender.handle).has_value(),
            "close UDP sender handle");
    require(adapter->close(receiver.handle).has_value(),
            "close UDP receiver handle");
}

void test_dns_cancellation_releases_network_workers() {
    using phoneme::network::NativeConnection;
    using phoneme::network::OperationId;

    auto adapter = phoneme::network::make_posix_network_adapter();
    auto remote = phoneme::network::Url::parse("socket://localhost:9");
    require(remote.has_value(), "parse delayed DNS URL");
    phoneme::network::detail::set_address_resolution_delay_for_tests(800);

    std::atomic<int> callback_count {0};
    std::array<OperationId, 4> operations {};
    for (auto& operation : operations) {
        auto started = adapter->open_stream(
            *remote, 5'000,
            [&callback_count](phoneme::Result<NativeConnection>) {
                callback_count.fetch_add(1, std::memory_order_relaxed);
            });
        require(started.has_value() && started->valid(),
                "start delayed DNS operation");
        operation = *started;
    }
    std::this_thread::sleep_for(100ms);
    for (const OperationId operation : operations) {
        require(adapter->cancel(operation).has_value(),
                "cancel delayed DNS operation");
    }
    phoneme::network::detail::set_address_resolution_delay_for_tests(0);

    auto server_url = phoneme::network::Url::parse("socket://:0");
    require(server_url.has_value(), "parse post-cancel server URL");
    AsyncResult<NativeConnection> server_wait;
    const auto started = std::chrono::steady_clock::now();
    auto server_operation = adapter->open_server(
        *server_url, 2'000,
        [&server_wait](phoneme::Result<NativeConnection> result) {
            server_wait.complete(std::move(result));
        });
    require(server_operation.has_value() && server_wait.wait_for(),
            "network worker is available after DNS cancellation");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    auto server_result = server_wait.take();
    require(server_result.has_value() && server_result->has_value() &&
                elapsed < 400ms,
            "cancelled DNS waits release the bounded worker pool promptly");
    require(adapter->close((**server_result).handle).has_value(),
            "close post-cancel server handle");

    std::this_thread::sleep_for(850ms);
    require(callback_count.load(std::memory_order_relaxed) == 0,
            "cancelled DNS operations suppress late callbacks");
}

void test_blocking_listeners_do_not_starve_new_operations() {
    using phoneme::network::NativeConnection;
    using phoneme::network::OperationId;

    auto adapter = phoneme::network::make_posix_network_adapter();
    auto server_url = phoneme::network::Url::parse("socket://:0");
    require(server_url.has_value(), "parse listener-starvation server URL");

    AsyncResult<NativeConnection> server_wait;
    auto server_operation = adapter->open_server(
        *server_url, 2'000,
        [&server_wait](phoneme::Result<NativeConnection> result) {
            server_wait.complete(std::move(result));
        });
    require(server_operation.has_value() && server_wait.wait_for(),
            "listener-starvation server opens");
    auto server_result = server_wait.take();
    require(server_result.has_value() && server_result->has_value(),
            "listener-starvation server result succeeds");
    NativeConnection server = std::move(**server_result);

    std::atomic<int> callback_count {0};
    std::array<OperationId, 36> listeners {};
    for (auto& listener : listeners) {
        auto started = adapter->accept(
            server.handle, 0,
            [&callback_count](phoneme::Result<NativeConnection>) {
                callback_count.fetch_add(1, std::memory_order_relaxed);
            });
        require(started.has_value() && started->valid(),
                "start indefinitely blocking listener");
        listener = *started;
    }
    std::this_thread::sleep_for(150ms);

    AsyncResult<NativeConnection> probe_wait;
    const auto started_at = std::chrono::steady_clock::now();
    auto probe_operation = adapter->open_server(
        *server_url, 2'000,
        [&probe_wait](phoneme::Result<NativeConnection> result) {
            probe_wait.complete(std::move(result));
        });
    require(probe_operation.has_value() && probe_wait.wait_for(1s),
            "new network operation bypasses blocked listener burst");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);
    auto probe_result = probe_wait.take();
    require(probe_result.has_value() && probe_result->has_value() &&
                elapsed < 1s,
            "dynamic workers prevent online listener starvation");
    require(adapter->close((**probe_result).handle).has_value(),
            "close listener-starvation probe server");

    for (const OperationId listener : listeners) {
        require(adapter->cancel(listener).has_value(),
                "cancel indefinitely blocking listener");
    }
    require(adapter->close(server.handle).has_value(),
            "close listener-starvation server");
    std::this_thread::sleep_for(200ms);
    require(callback_count.load(std::memory_order_relaxed) == 0,
            "cancelled blocking listeners suppress callbacks");
}

void test_adapter_shutdown_cancels_pending_accept() {
    using phoneme::network::NativeConnection;

    auto adapter = phoneme::network::make_posix_network_adapter();
    auto server_url = phoneme::network::Url::parse("socket://:0");
    require(server_url.has_value(), "parse shutdown server URL");
    AsyncResult<NativeConnection> server_wait;
    auto server_operation = adapter->open_server(
        *server_url, 5'000,
        [&server_wait](phoneme::Result<NativeConnection> result) {
            server_wait.complete(std::move(result));
        });
    require(server_operation.has_value() && server_wait.wait_for(),
            "shutdown test server opens");
    auto server_result = server_wait.take();
    require(server_result.has_value() && server_result->has_value(),
            "shutdown test server result succeeds");
    NativeConnection server = std::move(**server_result);

    std::atomic_bool callback_called {false};
    auto accept_operation = adapter->accept(
        server.handle, 30'000,
        [&callback_called](phoneme::Result<NativeConnection>) {
            callback_called.store(true, std::memory_order_release);
        });
    require(accept_operation.has_value(),
            "shutdown test starts pending accept");
    std::this_thread::sleep_for(100ms);
    const auto started = std::chrono::steady_clock::now();
    adapter.reset();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(elapsed < 1s,
            "adapter shutdown cancels pending accept promptly");
    require(!callback_called.load(std::memory_order_acquire),
            "adapter shutdown suppresses pending callback");
}

void test_posix_socket_workers(
    const std::shared_ptr<phoneme::network::AsyncNetworkAdapter>& adapter) {
    using phoneme::network::NativeConnection;
    using phoneme::network::OperationId;

    auto server_url = phoneme::network::Url::parse("socket://:0");
    require(server_url.has_value(), "parse loopback server URL");
    AsyncResult<NativeConnection> server_wait;
    auto server_operation = adapter->open_server(
        *server_url, 5'000,
        [&server_wait](phoneme::Result<NativeConnection> result) {
            server_wait.complete(std::move(result));
        });
    require(server_operation.has_value() && server_operation->valid(),
            "server open returns async operation ID");
    require(server_wait.wait_for(), "server open completes on worker pool");
    auto server_result = server_wait.take();
    require(server_result.has_value() && server_result->has_value(),
            "loopback server opens successfully");
    NativeConnection server = std::move(**server_result);

    std::atomic_bool cancelled_accept_callback {false};
    auto cancelled_accept = adapter->accept(
        server.handle, 5'000,
        [&cancelled_accept_callback](phoneme::Result<NativeConnection>) {
            cancelled_accept_callback.store(true, std::memory_order_release);
        });
    require(cancelled_accept.has_value() && cancelled_accept->valid(),
            "pending accept returns operation ID");
    std::this_thread::sleep_for(100ms);
    require(adapter->cancel(*cancelled_accept).has_value(),
            "cancel pending POSIX accept");
    std::this_thread::sleep_for(150ms);
    require(!cancelled_accept_callback.load(std::memory_order_acquire),
            "cancelled POSIX accept suppresses callback");

    std::string connect_host = server.local.host;
    if (connect_host.empty() || connect_host == "0.0.0.0") {
        connect_host = "127.0.0.1";
    } else if (connect_host == "::") {
        connect_host = "::1";
    }
    if (connect_host.find(':') != std::string::npos &&
        connect_host.front() != '[') {
        connect_host = "[" + connect_host + "]";
    }
    auto client_url = phoneme::network::Url::parse(
        "socket://" + connect_host + ":" +
        std::to_string(server.local.port));
    require(client_url.has_value(), "parse loopback client URL");

    AsyncResult<NativeConnection> accepted_wait;
    auto accepted_operation = adapter->accept(
        server.handle, 5'000,
        [&accepted_wait](phoneme::Result<NativeConnection> result) {
            accepted_wait.complete(std::move(result));
        });
    require(accepted_operation.has_value(), "start second accept");

    AsyncResult<NativeConnection> client_wait;
    auto client_operation = adapter->open_stream(
        *client_url, 5'000,
        [&client_wait](phoneme::Result<NativeConnection> result) {
            client_wait.complete(std::move(result));
        });
    require(client_operation.has_value(), "start loopback client connect");
    require(client_wait.wait_for(), "loopback client connects");
    require(accepted_wait.wait_for(), "second accept completes");
    auto client_result = client_wait.take();
    auto accepted_result = accepted_wait.take();
    require(client_result.has_value() && client_result->has_value(),
            "loopback client connection succeeds");
    require(accepted_result.has_value() && accepted_result->has_value(),
            "loopback accepted connection succeeds");
    NativeConnection client = std::move(**client_result);
    NativeConnection accepted = std::move(**accepted_result);

    const auto get_delay = [&](const char* description) {
        AsyncResult<phoneme::i32> wait;
        auto operation = adapter->get_socket_option(
            client.handle,
            phoneme::network::SocketOption::delay,
            [&wait](phoneme::Result<phoneme::i32> result) {
                wait.complete(std::move(result));
            });
        require(operation.has_value() && wait.wait_for(), description);
        auto result = wait.take();
        require(result.has_value() && result->has_value(), description);
        return **result;
    };
    const auto set_delay = [&](phoneme::i32 value, const char* description) {
        AsyncResult<bool> wait;
        auto operation = adapter->set_socket_option(
            client.handle,
            phoneme::network::SocketOption::delay,
            value,
            [&wait](phoneme::Result<bool> result) {
                wait.complete(std::move(result));
            });
        require(operation.has_value() && wait.wait_for(), description);
        auto result = wait.take();
        require(result.has_value() && result->has_value() && **result,
                description);
    };

    require(get_delay("read default Java ME DELAY option") == 0,
            "connected sockets disable small-write delay by default");
    set_delay(1, "enable Java ME small-write delay");
    require(get_delay("read enabled Java ME DELAY option") == 1,
            "DELAY=1 enables Nagle through inverse TCP_NODELAY mapping");
    set_delay(0, "disable Java ME small-write delay");
    require(get_delay("read disabled Java ME DELAY option") == 0,
            "DELAY=0 restores low-latency socket writes");

    constexpr phoneme::usize kLargePayloadSize = 64U * 1024U;
    std::vector<phoneme::u8> large_payload(kLargePayloadSize);
    for (phoneme::usize index = 0; index < large_payload.size(); ++index) {
        large_payload[index] = static_cast<phoneme::u8>(
            (index * 37U + 11U) & 0xFFU);
    }
    AsyncResult<phoneme::usize> large_write_wait;
    auto large_write = adapter->write(
        client.handle, large_payload, 5'000,
        [&large_write_wait](phoneme::Result<phoneme::usize> result) {
            large_write_wait.complete(std::move(result));
        });
    require(large_write.has_value() && large_write_wait.wait_for(),
            "queue large loopback socket payload");
    auto large_write_result = large_write_wait.take();
    require(large_write_result.has_value() &&
                large_write_result->has_value() &&
                **large_write_result == large_payload.size(),
            "large loopback socket payload is fully written");

    phoneme::usize queued_large_bytes = 0U;
    for (int attempt = 0; attempt < 100 &&
         queued_large_bytes < large_payload.size(); ++attempt) {
        AsyncResult<phoneme::usize> available_wait;
        auto available_operation = adapter->available(
            accepted.handle,
            [&available_wait](phoneme::Result<phoneme::usize> result) {
                available_wait.complete(std::move(result));
            });
        require(available_operation.has_value() && available_wait.wait_for(),
                "query queued large socket payload");
        auto available_result = available_wait.take();
        require(available_result.has_value() &&
                    available_result->has_value(),
                "large socket payload availability succeeds");
        queued_large_bytes = **available_result;
        if (queued_large_bytes < large_payload.size()) {
            std::this_thread::sleep_for(10ms);
        }
    }
    require(queued_large_bytes >= large_payload.size(),
            "large socket payload is queued before the read");

    AsyncResult<std::vector<phoneme::u8>> large_read_wait;
    auto large_read = adapter->read(
        accepted.handle, large_payload.size(), 5'000,
        [&large_read_wait](
            phoneme::Result<std::vector<phoneme::u8>> result) {
            large_read_wait.complete(std::move(result));
        });
    require(large_read.has_value() && large_read_wait.wait_for(),
            "read large socket payload in one Java-sized operation");
    auto large_read_result = large_read_wait.take();
    require(large_read_result.has_value() &&
                large_read_result->has_value() &&
                **large_read_result == large_payload,
            "socket reads are not capped at 16 KiB");

    const phoneme::usize workers_before_byte_handoffs =
        phoneme::network::detail::posix_network_worker_count_for_tests(
            adapter);
    for (int iteration = 0; iteration < 128; ++iteration) {
        AsyncResult<std::vector<phoneme::u8>> byte_read_wait;
        auto byte_read = adapter->read(
            accepted.handle, 1U, 5'000,
            [&byte_read_wait](
                phoneme::Result<std::vector<phoneme::u8>> result) {
                byte_read_wait.complete(std::move(result));
            });
        require(byte_read.has_value(),
                "queue sequential DataInputStream-style byte read");

        const phoneme::u8 expected = static_cast<phoneme::u8>(iteration);
        AsyncResult<phoneme::usize> byte_write_wait;
        auto byte_write = adapter->write(
            client.handle, std::vector<phoneme::u8> {expected}, 5'000,
            [&byte_write_wait](phoneme::Result<phoneme::usize> result) {
                byte_write_wait.complete(std::move(result));
            });
        require(byte_write.has_value() && byte_write_wait.wait_for(),
                "write sequential DataInputStream-style byte");
        require(byte_read_wait.wait_for(),
                "read sequential DataInputStream-style byte");
        auto byte_write_result = byte_write_wait.take();
        auto byte_read_result = byte_read_wait.take();
        require(byte_write_result.has_value() &&
                    byte_write_result->has_value() &&
                    **byte_write_result == 1U,
                "sequential byte write completes");
        require(byte_read_result.has_value() &&
                    byte_read_result->has_value() &&
                    (*byte_read_result)->size() == 1U &&
                    (*byte_read_result)->front() == expected,
                "sequential byte read preserves payload");
    }
    std::this_thread::sleep_for(20ms);
    const phoneme::usize workers_after_byte_handoffs =
        phoneme::network::detail::posix_network_worker_count_for_tests(
            adapter);
    require(workers_after_byte_handoffs <=
                workers_before_byte_handoffs + 2U,
            "sequential read callbacks reuse workers instead of growing the pool");

    AsyncResult<std::vector<phoneme::u8>> serialized_read_wait;
    auto serialized_read = adapter->read(
        accepted.handle, 16U, 5'000,
        [&serialized_read_wait](
            phoneme::Result<std::vector<phoneme::u8>> result) {
            serialized_read_wait.complete(std::move(result));
        });
    require(serialized_read.has_value() && serialized_read->valid(),
            "first same-direction read starts");

    std::atomic_bool cancelled_read_callback {false};
    auto cancelled_read = adapter->read(
        accepted.handle, 16U, 5'000,
        [&cancelled_read_callback](phoneme::Result<std::vector<phoneme::u8>>) {
            cancelled_read_callback.store(true, std::memory_order_release);
        });
    require(cancelled_read.has_value() && cancelled_read->valid(),
            "second same-direction read queues behind first");
    std::this_thread::sleep_for(100ms);
    require(adapter->cancel(*cancelled_read).has_value(),
            "cancel queued POSIX read");
    std::this_thread::sleep_for(150ms);
    require(!cancelled_read_callback.load(std::memory_order_acquire),
            "cancelled queued read suppresses callback");

    AsyncResult<phoneme::usize> write_wait;
    std::vector<phoneme::u8> payload {1U, 2U, 3U, 4U};
    auto write_operation = adapter->write(
        client.handle, payload, 5'000,
        [&write_wait](phoneme::Result<phoneme::usize> result) {
            write_wait.complete(std::move(result));
        });
    require(write_operation.has_value(), "start socket write with read pending");
    require(write_wait.wait_for(), "full-duplex write completes");
    auto write_result = write_wait.take();
    require(write_result.has_value() && write_result->has_value() &&
                **write_result == payload.size(),
            "write runs concurrently with serialized read");

    require(serialized_read_wait.wait_for(),
            "first serialized read receives payload");
    auto read_result = serialized_read_wait.take();
    require(read_result.has_value() && read_result->has_value() &&
                **read_result == payload,
            "cancelled queued read cannot steal bytes from first reader");

    require(adapter->shutdown_output(client.handle).has_value(),
            "half-close loopback client output");
    AsyncResult<std::vector<phoneme::u8>> eof_wait;
    auto eof_operation = adapter->read(
        accepted.handle, 16U, 5'000,
        [&eof_wait](phoneme::Result<std::vector<phoneme::u8>> result) {
            eof_wait.complete(std::move(result));
        });
    require(eof_operation.has_value() && eof_wait.wait_for(),
            "peer observes FIN after output half-close");
    auto eof_result = eof_wait.take();
    require(eof_result.has_value() && eof_result->has_value() &&
                (**eof_result).empty(),
            "output half-close maps peer read to EOF");

    AsyncResult<phoneme::usize> reverse_write_wait;
    std::vector<phoneme::u8> reverse_payload {9U, 8U};
    auto reverse_write = adapter->write(
        accepted.handle, reverse_payload, 5'000,
        [&reverse_write_wait](phoneme::Result<phoneme::usize> result) {
            reverse_write_wait.complete(std::move(result));
        });
    require(reverse_write.has_value() && reverse_write_wait.wait_for(),
            "peer can still write after receiving FIN");
    auto reverse_write_result = reverse_write_wait.take();
    require(reverse_write_result.has_value() &&
                reverse_write_result->has_value() &&
                **reverse_write_result == reverse_payload.size(),
            "reverse direction remains writable after half-close");

    AsyncResult<std::vector<phoneme::u8>> reverse_read_wait;
    auto reverse_read = adapter->read(
        client.handle, 16U, 5'000,
        [&reverse_read_wait](phoneme::Result<std::vector<phoneme::u8>> result) {
            reverse_read_wait.complete(std::move(result));
        });
    require(reverse_read.has_value() && reverse_read_wait.wait_for(),
            "half-closed client can still read reverse traffic");
    auto reverse_read_result = reverse_read_wait.take();
    require(reverse_read_result.has_value() &&
                reverse_read_result->has_value() &&
                **reverse_read_result == reverse_payload,
            "half-close preserves reverse-direction payload");

    require(adapter->close(client.handle).has_value(),
            "close loopback client handle");
    require(adapter->close(accepted.handle).has_value(),
            "close accepted loopback handle");
    require(adapter->close(server.handle).has_value(),
            "close loopback server handle");
}

} // namespace

int main() {
    auto adapter = phoneme::network::make_posix_network_adapter();
    auto url = phoneme::network::Url::parse("https://example.test/start");
    require(url.has_value(), "parse HTTPS test URL");

    phoneme::network::HttpRequest request {
        .url = *url,
        .method = "GET",
        .headers = {{"Accept", "text/plain"}},
        .timeout_ms = 5'000,
        .redirect_limit = 3,
    };

    bool completed = false;
    std::optional<phoneme::Result<phoneme::network::HttpResponse>> result;
    auto operation = adapter->perform_http(
        request,
        [&](phoneme::Result<phoneme::network::HttpResponse> response) {
            completed = true;
            result.emplace(std::move(response));
        });
    require(operation.has_value() && operation->valid(),
            "Apple HTTP request returns an operation ID");
    require(g_redirect_limit.load(std::memory_order_acquire) == 3,
            "Apple HTTP bridge receives Core redirect limit");
    require(!completed,
            "Apple HTTP request does not complete synchronously");

    complete_bridge_request(1);
    require(completed && result.has_value() && result->has_value(),
            "Apple HTTP callback completes successfully");
    require((*result)->status_code == 200 &&
                (*result)->body.size() == 2U &&
                (*result)->security.has_value() &&
                (*result)->security->protocol_version == "1.3",
            "Apple HTTP callback copies response and TLS metadata");

    bool late_completion = false;
    auto cancelled = adapter->perform_http(
        request,
        [&](phoneme::Result<phoneme::network::HttpResponse>) {
            late_completion = true;
        });
    require(cancelled.has_value(), "start cancellable Apple HTTP request");
    require(adapter->cancel(*cancelled).has_value(),
            "cancel Apple HTTP request");
    complete_bridge_request(2);
    require(!late_completion,
            "cancelled Apple HTTP request suppresses late completion");
    require(g_cancel_count.load(std::memory_order_relaxed) == 1 &&
                g_close_count.load(std::memory_order_relaxed) == 2,
            "Apple HTTP cancellation and result cleanup are balanced");

    std::atomic_bool raced_completion {false};
    auto raced = adapter->perform_http(
        request,
        [&raced_completion](phoneme::Result<phoneme::network::HttpResponse>) {
            raced_completion.store(true, std::memory_order_release);
        });
    require(raced.has_value(),
            "start Apple HTTP cancellation-race request");
    const int32_t raced_handle =
        g_next_handle.load(std::memory_order_acquire) - 1;
    block_bridge_collection();
    std::jthread raced_callback([raced_handle] {
        complete_bridge_request(raced_handle);
    });
    wait_for_bridge_collection();
    require(adapter->cancel(*raced).has_value(),
            "cancel Apple HTTP request while callback collects result");
    release_bridge_collection();
    raced_callback.join();
    require(!raced_completion.load(std::memory_order_acquire),
            "cancellation wins against an in-flight Apple HTTP callback");

    g_next_request_certificate_error.store(true, std::memory_order_release);
    std::optional<phoneme::Result<phoneme::network::HttpResponse>>
        certificate_error_result;
    auto certificate_error = adapter->perform_http(
        request,
        [&](phoneme::Result<phoneme::network::HttpResponse> response) {
            certificate_error_result.emplace(std::move(response));
        });
    require(certificate_error.has_value(),
            "start Apple HTTPS certificate-error request");
    complete_bridge_request(4);
    require(certificate_error_result.has_value() &&
                !certificate_error_result->has_value() &&
                certificate_error_result->error().code ==
                    phoneme::ErrorCode::java_exception &&
                certificate_error_result->error().java_exception_class ==
                    "java/lang/SecurityException",
            "Apple certificate trust error maps to SecurityException");
    require(g_close_count.load(std::memory_order_relaxed) == 4,
            "certificate-error result handle is released");

    test_http_parser_hardening();
    test_plain_http_uses_one_absolute_deadline(adapter);
    test_cross_authority_redirect_strips_credentials(adapter);
    test_posix_udp_roundtrip(adapter);
    test_posix_socket_workers(adapter);
    test_dns_cancellation_releases_network_workers();
    test_blocking_listeners_do_not_starve_new_operations();
    test_adapter_shutdown_cancels_pending_accept();

    std::cout << "Posix network adapter async tests passed\n";
    return 0;
}
