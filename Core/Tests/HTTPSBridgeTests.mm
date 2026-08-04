#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

extern "C" {
using PhoneMEHTTPSCompletion = void (*)(int32_t, void*);

int32_t phoneme_ios_https_execute_async(
    const char* url,
    const char* method,
    const char* headers,
    const uint8_t* body,
    int32_t body_length,
    int32_t timeout_ms,
    int32_t redirect_limit,
    int64_t cookie_session_id,
    PhoneMEHTTPSCompletion completion,
    void* context);
int32_t phoneme_ios_https_get_status_code(int32_t handle);
int32_t phoneme_ios_https_copy_string(
    int32_t handle, int32_t field, char* destination, int32_t capacity);
int32_t phoneme_ios_https_copy_body(
    int32_t handle, uint8_t* destination, int32_t capacity);
void phoneme_ios_https_close(int32_t handle);
void phoneme_ios_https_clear_session(int64_t cookie_session_id);
void phoneme_ios_https_reset(void);
void phoneme_ios_https_set_test_response_limit(int32_t bytes);
double phoneme_ios_https_get_test_timeout_interval(void);
int32_t phoneme_ios_https_get_test_waits_for_connectivity(void);
}

namespace {
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

class Completion final {
public:
    static void callback(int32_t handle, void* opaque) {
        auto* self = static_cast<Completion*>(opaque);
        {
            std::scoped_lock lock(self->mutex_);
            self->handle_ = handle;
        }
        self->condition_.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] {
            return handle_.has_value();
        });
    }

    int32_t handle() const {
        std::scoped_lock lock(mutex_);
        return handle_.value_or(0);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<int32_t> handle_;
};

struct Server final {
    int descriptor {-1};
    uint16_t port {0};

    Server() {
        descriptor = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(descriptor >= 0, "create HTTP fixture server socket");
        int enabled = 1;
        (void)::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,
                           &enabled, static_cast<socklen_t>(sizeof(enabled)));
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        require(::bind(descriptor, reinterpret_cast<sockaddr*>(&address),
                       static_cast<socklen_t>(sizeof(address))) == 0,
                "bind HTTP fixture server socket");
        require(::listen(descriptor, 1) == 0,
                "listen on HTTP fixture server socket");
        socklen_t length = static_cast<socklen_t>(sizeof(address));
        require(::getsockname(descriptor, reinterpret_cast<sockaddr*>(&address),
                              &length) == 0,
                "query HTTP fixture server port");
        port = ntohs(address.sin_port);
    }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    ~Server() {
        if (descriptor >= 0) ::close(descriptor);
    }
};

void send_all(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::send(
            descriptor, bytes.data() + offset, bytes.size() - offset, 0);
        require(written > 0, "send HTTP fixture response");
        offset += static_cast<std::size_t>(written);
    }
}

std::string read_request_headers(int descriptor) {
    std::string request;
    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t count = ::recv(descriptor, buffer, sizeof(buffer), 0);
        require(count > 0, "read HTTP fixture request");
        request.append(buffer, static_cast<std::size_t>(count));
        require(request.size() <= 64U * 1024U,
                "HTTP fixture request header remains bounded");
    }
    return request;
}

std::string copy_string(int32_t handle, int32_t field) {
    const int32_t length =
        phoneme_ios_https_copy_string(handle, field, nullptr, 0);
    if (length < 0) return {};
    std::string storage(static_cast<std::size_t>(length) + 1U, '\0');
    require(phoneme_ios_https_copy_string(
                handle, field, storage.data(), length + 1) == length,
            "copy HTTPS bridge string");
    storage.resize(static_cast<std::size_t>(length));
    return storage;
}

void test_streaming_small_response() {
    Server server;
    std::jthread worker([&] {
        sockaddr_in peer {};
        socklen_t length = static_cast<socklen_t>(sizeof(peer));
        const int client = ::accept(
            server.descriptor, reinterpret_cast<sockaddr*>(&peer), &length);
        require(client >= 0, "accept small HTTP fixture client");
        read_request_headers(client);
        send_all(client,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: 2\r\n"
                 "Connection: keep-alive\r\n\r\nOK");
        std::this_thread::sleep_for(1s);
        ::close(client);
    });

    Completion completion;
    const std::string url =
        "http://127.0.0.1:" + std::to_string(server.port) + "/small";
    const auto started = std::chrono::steady_clock::now();
    const int32_t handle = phoneme_ios_https_execute_async(
        url.c_str(), "GET", "", nullptr, 0, 0, 0, 1,
        &Completion::callback, &completion);
    require(handle > 0, "start streaming HTTP bridge request");
    require(phoneme_ios_https_get_test_timeout_interval() > 86'400.0,
            "disabled MIDP timeout is not replaced by URLSession 60-second default");
    require(phoneme_ios_https_get_test_waits_for_connectivity() == 1,
            "disabled MIDP timeout waits through transient connectivity loss");
    require(completion.wait_for(700ms),
            "Content-Length response completes before peer FIN");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(elapsed < 700ms,
            "streaming bridge does not wait for keep-alive connection close");
    require(completion.handle() == handle,
            "streaming bridge returns the original handle");
    require(phoneme_ios_https_get_status_code(handle) == 200,
            "streaming bridge preserves HTTP status");
    require(copy_string(handle, 10).empty(),
            "streaming bridge has no error for small response");
    const int32_t body_length =
        phoneme_ios_https_copy_body(handle, nullptr, 0);
    require(body_length == 2, "streaming bridge reports small body length");
    uint8_t body[2] {};
    require(phoneme_ios_https_copy_body(handle, body, 2) == 2 &&
                body[0] == 'O' && body[1] == 'K',
            "streaming bridge preserves small response body");
    phoneme_ios_https_close(handle);
}

void test_cookie_sessions_persist_and_remain_isolated() {
    constexpr int64_t kFirstSession = 101;
    constexpr int64_t kSecondSession = 202;
    Server server;
    std::atomic_bool same_session_cookie {false};
    std::atomic_bool cross_session_cookie {false};
    std::jthread worker([&] {
        for (int request_index = 0; request_index < 3; ++request_index) {
            sockaddr_in peer {};
            socklen_t length = static_cast<socklen_t>(sizeof(peer));
            const int client = ::accept(
                server.descriptor, reinterpret_cast<sockaddr*>(&peer), &length);
            require(client >= 0, "accept cookie HTTP fixture client");
            const std::string request = read_request_headers(client);
            const bool contains_cookie = request.find(
                "Cookie: phoneme_session=stable") != std::string::npos;
            if (request_index == 1) {
                same_session_cookie.store(contains_cookie,
                                          std::memory_order_release);
            } else if (request_index == 2) {
                cross_session_cookie.store(contains_cookie,
                                           std::memory_order_release);
            }
            const char *set_cookie = request_index == 0
                ? "Set-Cookie: phoneme_session=stable; Path=/; HttpOnly\r\n"
                : "";
            const std::string response =
                std::string("HTTP/1.1 200 OK\r\n") + set_cookie +
                "Content-Length: 2\r\nConnection: close\r\n\r\nOK";
            send_all(client, response);
            ::close(client);
        }
    });

    const std::string base_url =
        "http://127.0.0.1:" + std::to_string(server.port);
    const auto request = [&](std::string_view path, int64_t session_id) {
        Completion completion;
        const std::string url = base_url + std::string(path);
        const int32_t handle = phoneme_ios_https_execute_async(
            url.c_str(), "GET", "", nullptr, 0, 5'000, 0, session_id,
            &Completion::callback, &completion);
        require(handle > 0, "start cookie-session HTTP request");
        require(completion.wait_for(2s),
                "cookie-session HTTP request completes");
        require(phoneme_ios_https_get_status_code(handle) == 200,
                "cookie-session HTTP request succeeds");
        phoneme_ios_https_close(handle);
    };

    request("/cookie-set", kFirstSession);
    request("/cookie-check", kFirstSession);
    request("/cookie-isolation", kSecondSession);
    require(same_session_cookie.load(std::memory_order_acquire),
            "HTTP cookie persists within one emulator network session");
    require(!cross_session_cookie.load(std::memory_order_acquire),
            "HTTP cookie is isolated between emulator network sessions");
    phoneme_ios_https_clear_session(kFirstSession);
    phoneme_ios_https_clear_session(kSecondSession);
}

void test_redirect_response_cookie_reaches_followup_request() {
    constexpr int64_t kSession = 303;
    Server server;
    std::atomic_bool redirect_cookie_seen {false};
    std::jthread worker([&] {
        sockaddr_in peer {};
        socklen_t length = static_cast<socklen_t>(sizeof(peer));
        int client = ::accept(
            server.descriptor, reinterpret_cast<sockaddr*>(&peer), &length);
        require(client >= 0, "accept redirect source HTTP client");
        (void)read_request_headers(client);
        const std::string location =
            "http://127.0.0.1:" + std::to_string(server.port) + "/landing";
        const std::string redirect =
            "HTTP/1.1 302 Found\r\nLocation: " + location +
            "\r\nSet-Cookie: redirect_token=ready; Path=/\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(client, redirect);
        ::close(client);

        peer = {};
        length = static_cast<socklen_t>(sizeof(peer));
        client = ::accept(
            server.descriptor, reinterpret_cast<sockaddr*>(&peer), &length);
        require(client >= 0, "accept redirected HTTP client");
        const std::string request = read_request_headers(client);
        redirect_cookie_seen.store(
            request.find("Cookie: redirect_token=ready") != std::string::npos,
            std::memory_order_release);
        send_all(client,
                 "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                 "Connection: close\r\n\r\nOK");
        ::close(client);
    });

    Completion completion;
    const std::string url =
        "http://127.0.0.1:" + std::to_string(server.port) + "/redirect";
    const int32_t handle = phoneme_ios_https_execute_async(
        url.c_str(), "GET", "", nullptr, 0, 5'000, 1, kSession,
        &Completion::callback, &completion);
    require(handle > 0, "start redirect-cookie HTTP request");
    require(completion.wait_for(2s),
            "redirect-cookie HTTP request completes");
    require(phoneme_ios_https_get_status_code(handle) == 200,
            "redirect-cookie HTTP request reaches final response");
    require(redirect_cookie_seen.load(std::memory_order_acquire),
            "redirect response cookie is sent to the follow-up request");
    phoneme_ios_https_close(handle);
    phoneme_ios_https_clear_session(kSession);
}

void test_declared_oversized_response_is_rejected() {
    Server server;
    std::jthread worker([&] {
        sockaddr_in peer {};
        socklen_t length = static_cast<socklen_t>(sizeof(peer));
        const int client = ::accept(
            server.descriptor, reinterpret_cast<sockaddr*>(&peer), &length);
        require(client >= 0, "accept oversized HTTP fixture client");
        read_request_headers(client);
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2048\r\n"
            "Connection: keep-alive\r\n\r\n";
        response.append(2048U, 'A');
        send_all(client, response);
        std::this_thread::sleep_for(500ms);
        ::close(client);
    });

    phoneme_ios_https_set_test_response_limit(1024);
    Completion completion;
    const std::string url =
        "http://127.0.0.1:" + std::to_string(server.port) + "/large";
    const int32_t handle = phoneme_ios_https_execute_async(
        url.c_str(), "GET", "", nullptr, 0, 5'000, 0, 1,
        &Completion::callback, &completion);
    require(handle > 0, "start oversized HTTP bridge request");
    require(completion.wait_for(2s),
            "oversized response is rejected from response headers");
    const std::string error = copy_string(handle, 10);
    require(error.find("1024 byte") != std::string::npos,
            "oversized response reports configured bounded error");
    require(phoneme_ios_https_copy_body(handle, nullptr, 0) == 0,
            "oversized response does not retain body bytes");
    phoneme_ios_https_close(handle);
    phoneme_ios_https_set_test_response_limit(0);
}

} // namespace

int main() {
    @autoreleasepool {
        phoneme_ios_https_reset();
        test_streaming_small_response();
        test_cookie_sessions_persist_and_remain_isolated();
        test_redirect_response_cookie_reaches_followup_request();
        test_declared_oversized_response_is_rejected();
        phoneme_ios_https_reset();
    }
    std::cout << "HTTPS bridge streaming tests passed\n";
    return 0;
}
