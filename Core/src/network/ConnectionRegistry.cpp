#include "phoneme/network/ConnectionRegistry.hpp"

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace phoneme::network {
namespace {

constexpr i32 kTimeoutEnabledMilliseconds = 30'000;
constexpr i32 kTimeoutDisabledSafetyMilliseconds = 120'000;
constexpr usize kMaximumBufferedHttpBody = 16U * 1024U * 1024U;
constexpr usize kMaximumDatagramLength = 65'507U;

[[nodiscard]] bool readable(ConnectionMode mode) noexcept {
    return mode == ConnectionMode::read ||
           mode == ConnectionMode::read_write;
}

[[nodiscard]] bool writable(ConnectionMode mode) noexcept {
    return mode == ConnectionMode::write ||
           mode == ConnectionMode::read_write;
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

[[nodiscard]] bool valid_http_token(std::string_view value) noexcept {
    if (value.empty()) return false;
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte <= 32U || byte >= 127U || character == ':' ||
            character == '\r' || character == '\n') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_header_value(std::string_view value) noexcept {
    return value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

template <typename T>
struct WaitState final {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed {false};
    std::optional<Result<T>> result;
};

template <typename T, typename Starter>
[[nodiscard]] Result<T> await_operation(
    const std::shared_ptr<AsyncNetworkAdapter>& adapter,
    i32 timeout_ms,
    Starter&& starter) {
    if (!adapter) {
        return fail(ErrorCode::not_configured,
                    "network adapter is not configured");
    }
    auto state = std::make_shared<WaitState<T>>();
    auto started = starter(
        [state](Result<T> result) {
            std::scoped_lock lock(state->mutex);
            if (state->completed) return;
            state->result.emplace(std::move(result));
            state->completed = true;
            state->condition.notify_all();
        });
    if (!started) return std::unexpected(started.error());

    std::unique_lock lock(state->mutex);
    const bool completed = timeout_ms <= 0
        ? (state->condition.wait(lock, [&state] { return state->completed; }),
           true)
        : state->condition.wait_for(
              lock, std::chrono::milliseconds(timeout_ms),
              [&state] { return state->completed; });
    if (!completed) {
        state->completed = true;
        lock.unlock();
        (void)adapter->cancel(*started);
        return fail(ErrorCode::io_error,
                    "network adapter operation timed out");
    }
    if (!state->result.has_value()) {
        return fail(ErrorCode::internal_error,
                    "network adapter completed without a result");
    }
    return std::move(*state->result);
}

[[nodiscard]] std::optional<std::string> find_header(
    const std::vector<Header>& headers,
    std::string_view name) {
    const std::string normalized = lowercase(name);
    for (auto iterator = headers.rbegin(); iterator != headers.rend();
         ++iterator) {
        if (lowercase(iterator->first) == normalized) {
            return iterator->second;
        }
    }
    return std::nullopt;
}

void set_header(std::vector<Header>& headers,
                std::string name,
                std::string value) {
    const std::string normalized = lowercase(name);
    for (auto& [existing_name, existing_value] : headers) {
        if (lowercase(existing_name) == normalized) {
            existing_name = std::move(name);
            existing_value = std::move(value);
            return;
        }
    }
    headers.emplace_back(std::move(name), std::move(value));
}

} // namespace

ConnectionRegistry::ConnectionRegistry()
    : ConnectionRegistry(make_posix_network_adapter()) {}

ConnectionRegistry::ConnectionRegistry(
    std::shared_ptr<AsyncNetworkAdapter> adapter)
    : adapter_(std::move(adapter)) {}

ConnectionRegistry::~ConnectionRegistry() { close_all(); }

Status ConnectionRegistry::set_adapter(
    std::shared_ptr<AsyncNetworkAdapter> adapter) {
    if (!adapter) {
        return fail(ErrorCode::invalid_argument,
                    "network adapter is null");
    }
    std::scoped_lock lock(mutex_);
    if (!entries_.empty()) {
        return fail(ErrorCode::invalid_state,
                    "network adapter cannot change with open connections");
    }
    adapter_ = std::move(adapter);
    return {};
}

void ConnectionRegistry::set_owner(i32 owner) noexcept {
    std::scoped_lock lock(mutex_);
    owner_ = std::max(owner, 0);
}

i32 ConnectionRegistry::owner() const noexcept {
    std::scoped_lock lock(mutex_);
    return owner_;
}

ConnectionToken ConnectionRegistry::allocate_token_unlocked() {
    if (next_id_ <= 0 || next_id_ == std::numeric_limits<i32>::max()) {
        next_id_ = 1;
    }
    if (next_generation_ <= 0 ||
        next_generation_ == std::numeric_limits<i32>::max()) {
        next_generation_ = 1;
    }
    while (entries_.contains(next_id_)) {
        ++next_id_;
        if (next_id_ <= 0 ||
            next_id_ == std::numeric_limits<i32>::max()) {
            next_id_ = 1;
        }
    }
    return ConnectionToken {
        .id = next_id_++,
        .generation = next_generation_++,
    };
}

Result<ConnectionRegistry::Entry*> ConnectionRegistry::mutable_entry(
    ConnectionToken token) {
    if (!token.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "connection token is invalid");
    }
    const auto found = entries_.find(token.id);
    if (found == entries_.end() || found->second.token != token) {
        return fail(ErrorCode::invalid_state,
                    "connection is closed or belongs to another generation");
    }
    return &found->second;
}

Result<const ConnectionRegistry::Entry*> ConnectionRegistry::entry(
    ConnectionToken token) const {
    if (!token.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "connection token is invalid");
    }
    const auto found = entries_.find(token.id);
    if (found == entries_.end() || found->second.token != token) {
        return fail(ErrorCode::invalid_state,
                    "connection is closed or belongs to another generation");
    }
    return &found->second;
}

Result<NativeConnection> ConnectionRegistry::open_native(
    const Entry& entry_value) {
    const auto adapter = adapter_;
    switch (entry_value.kind) {
    case ConnectionKind::stream:
        return await_operation<NativeConnection>(
            adapter, entry_value.timeout_ms,
            [&entry_value, &adapter](Completion<NativeConnection> completion) {
                return adapter->open_stream(entry_value.parsed_url,
                                            entry_value.timeout_ms,
                                            std::move(completion));
            });
    case ConnectionKind::server:
        return await_operation<NativeConnection>(
            adapter, entry_value.timeout_ms,
            [&entry_value, &adapter](Completion<NativeConnection> completion) {
                return adapter->open_server(entry_value.parsed_url,
                                            entry_value.timeout_ms,
                                            std::move(completion));
            });
    case ConnectionKind::datagram:
        return await_operation<NativeConnection>(
            adapter, entry_value.timeout_ms,
            [&entry_value, &adapter](Completion<NativeConnection> completion) {
                return adapter->open_datagram(entry_value.parsed_url,
                                              entry_value.timeout_ms,
                                              std::move(completion));
            });
    case ConnectionKind::http:
    case ConnectionKind::https:
        return fail(ErrorCode::invalid_state,
                    "HTTP connections are opened lazily");
    }
    return fail(ErrorCode::internal_error,
                "unknown network connection kind");
}

Result<OpenedConnection> ConnectionRegistry::open(
    std::string_view url_text,
    ConnectionMode mode,
    bool timeouts) {
    if (mode != ConnectionMode::read &&
        mode != ConnectionMode::write &&
        mode != ConnectionMode::read_write) {
        return fail(ErrorCode::invalid_argument,
                    "GCF connection mode is invalid");
    }
    auto parsed = Url::parse(url_text);
    if (!parsed) return std::unexpected(parsed.error());

    Entry created;
    {
        std::scoped_lock lock(mutex_);
        created.token = allocate_token_unlocked();
        created.parsed_url = *parsed;
        created.mode = mode;
        created.timeouts = timeouts;
        created.timeout_ms = timeouts
            ? kTimeoutEnabledMilliseconds
            : kTimeoutDisabledSafetyMilliseconds;
        switch (parsed->scheme) {
        case Scheme::socket:
            created.kind = parsed->server_endpoint
                ? ConnectionKind::server
                : ConnectionKind::stream;
            break;
        case Scheme::server_socket:
            created.kind = ConnectionKind::server;
            break;
        case Scheme::datagram:
            created.kind = ConnectionKind::datagram;
            break;
        case Scheme::http:
            created.kind = ConnectionKind::http;
            break;
        case Scheme::https:
            created.kind = ConnectionKind::https;
            break;
        }
        // Reserve the token before native I/O so concurrent opens cannot
        // allocate the same id/generation while this operation is pending.
        entries_.insert_or_assign(created.token.id, created);
    }

    if (created.kind != ConnectionKind::http &&
        created.kind != ConnectionKind::https) {
        auto native = open_native(created);
        if (!native) {
            std::scoped_lock lock(mutex_);
            const auto found = entries_.find(created.token.id);
            if (found != entries_.end() &&
                found->second.token == created.token) {
                entries_.erase(found);
            }
            return std::unexpected(native.error());
        }

        std::optional<Error> completion_error;
        {
            std::scoped_lock lock(mutex_);
            auto found = mutable_entry(created.token);
            if (!found) {
                completion_error = found.error();
            } else if (!(*found)->connection_open) {
                completion_error = Error::make(
                    ErrorCode::invalid_state,
                    "connection closed while opening");
            } else {
                (*found)->native = std::move(*native);
            }
        }
        if (completion_error.has_value()) {
            (void)adapter_->close(native->handle);
            return std::unexpected(std::move(*completion_error));
        }
    }

    return OpenedConnection {
        .token = created.token,
        .kind = created.kind,
    };
}

Result<OpenedConnection> ConnectionRegistry::accept(
    ConnectionToken server) {
    NativeHandle handle;
    i32 timeout_ms = 0;
    bool timeouts = false;
    {
        std::scoped_lock lock(mutex_);
        auto found = entry(server);
        if (!found) return std::unexpected(found.error());
        if ((*found)->kind != ConnectionKind::server ||
            !(*found)->native.has_value()) {
            return fail(ErrorCode::invalid_state,
                        "connection is not an open server socket");
        }
        handle = (*found)->native->handle;
        timeout_ms = (*found)->timeout_ms;
        timeouts = (*found)->timeouts;
    }
    const auto adapter = adapter_;
    auto accepted = await_operation<NativeConnection>(
        adapter, timeout_ms,
        [&adapter, handle, timeout_ms](
            Completion<NativeConnection> completion) {
            return adapter->accept(handle, timeout_ms,
                                   std::move(completion));
        });
    if (!accepted) return std::unexpected(accepted.error());

    Entry created;
    {
        std::scoped_lock lock(mutex_);
        auto still_open = entry(server);
        if (!still_open) {
            (void)adapter_->close(accepted->handle);
            return std::unexpected(still_open.error());
        }
        created.token = allocate_token_unlocked();
        created.kind = ConnectionKind::stream;
        created.mode = ConnectionMode::read_write;
        created.timeouts = timeouts;
        created.timeout_ms = timeout_ms;
        created.native = *accepted;
        std::string remote_host = accepted->remote.host;
        if (remote_host.find(':') != std::string::npos) {
            remote_host = "[" + remote_host + "]";
        }
        auto parsed = Url::parse(
            "socket://" + remote_host + ":" +
            std::to_string(accepted->remote.port));
        if (parsed) created.parsed_url = std::move(*parsed);
        entries_.insert_or_assign(created.token.id, created);
    }
    return OpenedConnection {
        .token = created.token,
        .kind = created.kind,
    };
}

Status ConnectionRegistry::reconnect(ConnectionToken token) {
    Entry snapshot;
    std::optional<NativeHandle> old_handle;
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return std::unexpected(found.error());
        if (!(*found)->connection_open) {
            return fail(ErrorCode::invalid_state,
                        "closed connection cannot reconnect");
        }
        snapshot = **found;
        if ((*found)->native.has_value()) {
            old_handle = (*found)->native->handle;
            (*found)->native.reset();
        }
        (*found)->response.reset();
        (*found)->response_cursor = 0;
        (*found)->request_started = false;
    }
    if (old_handle.has_value()) (void)adapter_->close(*old_handle);
    if (snapshot.kind == ConnectionKind::http ||
        snapshot.kind == ConnectionKind::https) {
        return {};
    }
    auto reopened = open_native(snapshot);
    if (!reopened) return std::unexpected(reopened.error());
    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) {
        (void)adapter_->close(reopened->handle);
        return std::unexpected(found.error());
    }
    (*found)->native = std::move(*reopened);
    return {};
}

Status ConnectionRegistry::close(ConnectionToken token) {
    std::optional<NativeHandle> native;
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return {};
        (*found)->connection_open = false;
        if ((*found)->input_streams != 0U ||
            (*found)->output_streams != 0U) {
            return {};
        }
        if ((*found)->native.has_value()) native = (*found)->native->handle;
        entries_.erase(token.id);
    }
    if (native.has_value()) return adapter_->close(*native);
    return {};
}

void ConnectionRegistry::close_all() noexcept {
    std::vector<NativeHandle> handles;
    {
        std::scoped_lock lock(mutex_);
        handles.reserve(entries_.size());
        for (const auto& [unused, value] : entries_) {
            (void)unused;
            if (value.native.has_value()) {
                handles.push_back(value.native->handle);
            }
        }
        entries_.clear();
    }
    if (adapter_) {
        for (const NativeHandle handle : handles) {
            (void)adapter_->close(handle);
        }
    }
}

Result<ConnectionKind> ConnectionRegistry::kind(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return (*found)->kind;
}

Result<Url> ConnectionRegistry::url(ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->response.has_value()) return (*found)->response->final_url;
    return (*found)->parsed_url;
}

Result<Endpoint> ConnectionRegistry::local_endpoint(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if (!(*found)->native.has_value()) {
        return fail(ErrorCode::invalid_state,
                    "connection has no native local endpoint");
    }
    return (*found)->native->local;
}

Result<Endpoint> ConnectionRegistry::remote_endpoint(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind == ConnectionKind::http ||
        (*found)->kind == ConnectionKind::https) {
        return Endpoint {
            .host = (*found)->parsed_url.host,
            .port = (*found)->parsed_url.effective_port(),
        };
    }
    if (!(*found)->native.has_value()) {
        return fail(ErrorCode::invalid_state,
                    "connection has no native remote endpoint");
    }
    return (*found)->native->remote;
}

Status ConnectionRegistry::open_input(ConnectionToken token) {
    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) return std::unexpected(found.error());
    if (!(*found)->connection_open || !readable((*found)->mode)) {
        return fail(ErrorCode::invalid_state,
                    "connection is not open for input");
    }
    ++(*found)->input_streams;
    return {};
}

Status ConnectionRegistry::open_output(ConnectionToken token) {
    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) return std::unexpected(found.error());
    if (!(*found)->connection_open || !writable((*found)->mode)) {
        return fail(ErrorCode::invalid_state,
                    "connection is not open for output");
    }
    if ((*found)->request_started) {
        return fail(ErrorCode::invalid_state,
                    "HTTP request output cannot open after request starts");
    }
    ++(*found)->output_streams;
    return {};
}

Status ConnectionRegistry::release_if_unused(ConnectionToken token) {
    std::optional<NativeHandle> native;
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return {};
        if ((*found)->connection_open || (*found)->input_streams != 0U ||
            (*found)->output_streams != 0U) {
            return {};
        }
        if ((*found)->native.has_value()) native = (*found)->native->handle;
        entries_.erase(token.id);
    }
    if (native.has_value()) return adapter_->close(*native);
    return {};
}

Status ConnectionRegistry::close_input(ConnectionToken token) {
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return {};
        if ((*found)->input_streams != 0U) --(*found)->input_streams;
    }
    return release_if_unused(token);
}

Status ConnectionRegistry::close_output(ConnectionToken token) {
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return {};
        if ((*found)->output_streams != 0U) --(*found)->output_streams;
    }
    return release_if_unused(token);
}

Status ConnectionRegistry::ensure_http_response(ConnectionToken token) {
    HttpRequest request;
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return std::unexpected(found.error());
        if ((*found)->kind != ConnectionKind::http &&
            (*found)->kind != ConnectionKind::https) {
            return fail(ErrorCode::invalid_state,
                        "connection is not HTTP(S)");
        }
        if ((*found)->response.has_value()) return {};
        if (!(*found)->connection_open && (*found)->input_streams == 0U) {
            return fail(ErrorCode::invalid_state,
                        "HTTP connection is closed");
        }
        if ((*found)->request_started) {
            return fail(ErrorCode::invalid_state,
                        "HTTP request is already in progress");
        }
        (*found)->request_started = true;
        request.url = (*found)->parsed_url;
        request.method = (*found)->http_method;
        request.headers = (*found)->request_headers;
        request.body = (*found)->request_body;
        request.timeout_ms = (*found)->timeout_ms;
    }

    const auto adapter = adapter_;
    auto response = await_operation<HttpResponse>(
        adapter, request.timeout_ms,
        [&adapter, request = std::move(request)](
            Completion<HttpResponse> completion) mutable {
            return adapter->perform_http(std::move(request),
                                         std::move(completion));
        });
    if (!response) {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (found) (*found)->request_started = false;
        return std::unexpected(response.error());
    }

    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) return std::unexpected(found.error());
    (*found)->response = std::move(*response);
    (*found)->response_cursor = 0;
    return {};
}

Result<std::vector<u8>> ConnectionRegistry::read(
    ConnectionToken token,
    usize maximum_bytes) {
    if (maximum_bytes == 0U) return std::vector<u8> {};
    ConnectionKind connection_kind;
    NativeHandle handle;
    i32 timeout_ms = 0;
    {
        std::scoped_lock lock(mutex_);
        auto found = entry(token);
        if (!found) return std::unexpected(found.error());
        connection_kind = (*found)->kind;
        timeout_ms = (*found)->timeout_ms;
        if (connection_kind != ConnectionKind::http &&
            connection_kind != ConnectionKind::https) {
            if (!(*found)->native.has_value()) {
                return fail(ErrorCode::invalid_state,
                            "network connection has no native handle");
            }
            handle = (*found)->native->handle;
        }
    }
    if (connection_kind == ConnectionKind::http ||
        connection_kind == ConnectionKind::https) {
        auto prepared = ensure_http_response(token);
        if (!prepared) return std::unexpected(prepared.error());
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return std::unexpected(found.error());
        const auto& body = (*found)->response->body;
        if ((*found)->response_cursor >= body.size()) {
            return std::vector<u8> {};
        }
        const usize count = std::min(maximum_bytes,
                                     body.size() -
                                         (*found)->response_cursor);
        std::vector<u8> result(
            body.begin() + static_cast<isize>((*found)->response_cursor),
            body.begin() + static_cast<isize>(
                (*found)->response_cursor + count));
        (*found)->response_cursor += count;
        return result;
    }

    const auto adapter = adapter_;
    return await_operation<std::vector<u8>>(
        adapter, timeout_ms,
        [&adapter, handle, maximum_bytes, timeout_ms](
            Completion<std::vector<u8>> completion) {
            return adapter->read(handle, maximum_bytes, timeout_ms,
                                 std::move(completion));
        });
}

Status ConnectionRegistry::write(ConnectionToken token,
                                 std::span<const u8> bytes) {
    if (bytes.empty()) return {};
    ConnectionKind connection_kind;
    NativeHandle handle;
    i32 timeout_ms = 0;
    {
        std::scoped_lock lock(mutex_);
        auto found = mutable_entry(token);
        if (!found) return std::unexpected(found.error());
        connection_kind = (*found)->kind;
        timeout_ms = (*found)->timeout_ms;
        if (connection_kind == ConnectionKind::http ||
            connection_kind == ConnectionKind::https) {
            if ((*found)->request_started) {
                return fail(ErrorCode::invalid_state,
                            "HTTP request body is already committed");
            }
            if (bytes.size() > kMaximumBufferedHttpBody -
                                   (*found)->request_body.size()) {
                return fail(ErrorCode::overflow,
                            "HTTP request body exceeds maximum size");
            }
            (*found)->request_body.insert((*found)->request_body.end(),
                                          bytes.begin(), bytes.end());
            return {};
        }
        if (!(*found)->native.has_value()) {
            return fail(ErrorCode::invalid_state,
                        "network connection has no native handle");
        }
        handle = (*found)->native->handle;
    }

    const auto adapter = adapter_;
    std::vector<u8> owned(bytes.begin(), bytes.end());
    auto written = await_operation<usize>(
        adapter, timeout_ms,
        [&adapter, handle, timeout_ms, owned = std::move(owned)](
            Completion<usize> completion) mutable {
            return adapter->write(handle, std::move(owned), timeout_ms,
                                  std::move(completion));
        });
    if (!written) return std::unexpected(written.error());
    if (*written != bytes.size()) {
        return fail(ErrorCode::io_error,
                    "network adapter performed a partial write");
    }
    return {};
}

Result<usize> ConnectionRegistry::available(ConnectionToken token) {
    ConnectionKind connection_kind;
    NativeHandle handle;
    {
        std::scoped_lock lock(mutex_);
        auto found = entry(token);
        if (!found) return std::unexpected(found.error());
        connection_kind = (*found)->kind;
        if (connection_kind == ConnectionKind::http ||
            connection_kind == ConnectionKind::https) {
            if (!(*found)->response.has_value()) return 0U;
            return (*found)->response->body.size() -
                   (*found)->response_cursor;
        }
        if (!(*found)->native.has_value()) {
            return fail(ErrorCode::invalid_state,
                        "network connection has no native handle");
        }
        handle = (*found)->native->handle;
    }
    const auto adapter = adapter_;
    return await_operation<usize>(
        adapter, kTimeoutEnabledMilliseconds,
        [&adapter, handle](Completion<usize> completion) {
            return adapter->available(handle, std::move(completion));
        });
}

Status ConnectionRegistry::flush(ConnectionToken token) {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return {};
}

Result<NativeHandle> ConnectionRegistry::native_handle(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if (!(*found)->native.has_value()) {
        return fail(ErrorCode::invalid_state,
                    "connection has no native handle");
    }
    return (*found)->native->handle;
}

Status ConnectionRegistry::set_socket_option(ConnectionToken token,
                                             SocketOption option,
                                             i32 value) {
    auto handle = native_handle(token);
    if (!handle) return std::unexpected(handle.error());
    const auto adapter = adapter_;
    auto changed = await_operation<bool>(
        adapter, kTimeoutEnabledMilliseconds,
        [&adapter, handle = *handle, option, value](
            Completion<bool> completion) {
            return adapter->set_socket_option(handle, option, value,
                                              std::move(completion));
        });
    if (!changed) return std::unexpected(changed.error());
    return {};
}

Result<i32> ConnectionRegistry::socket_option(ConnectionToken token,
                                              SocketOption option) {
    auto handle = native_handle(token);
    if (!handle) return std::unexpected(handle.error());
    const auto adapter = adapter_;
    return await_operation<i32>(
        adapter, kTimeoutEnabledMilliseconds,
        [&adapter, handle = *handle, option](Completion<i32> completion) {
            return adapter->get_socket_option(handle, option,
                                              std::move(completion));
        });
}

Status ConnectionRegistry::send_datagram(ConnectionToken token,
                                         DatagramPacket packet) {
    NativeHandle handle;
    i32 timeout_ms = 0;
    {
        std::scoped_lock lock(mutex_);
        auto found = entry(token);
        if (!found) return std::unexpected(found.error());
        if ((*found)->kind != ConnectionKind::datagram ||
            !(*found)->native.has_value()) {
            return fail(ErrorCode::invalid_state,
                        "connection is not a datagram socket");
        }
        handle = (*found)->native->handle;
        timeout_ms = (*found)->timeout_ms;
        if (packet.peer.host.empty() &&
            !(*found)->native->remote.host.empty()) {
            packet.peer = (*found)->native->remote;
        }
    }
    if (packet.bytes.size() > kMaximumDatagramLength) {
        return fail(ErrorCode::out_of_range,
                    "UDP payload exceeds maximum datagram length");
    }
    const auto adapter = adapter_;
    auto sent = await_operation<usize>(
        adapter, timeout_ms,
        [&adapter, handle, packet = std::move(packet), timeout_ms](
            Completion<usize> completion) mutable {
            return adapter->send_datagram(handle, std::move(packet),
                                          timeout_ms,
                                          std::move(completion));
        });
    if (!sent) return std::unexpected(sent.error());
    return {};
}

Result<DatagramPacket> ConnectionRegistry::receive_datagram(
    ConnectionToken token,
    usize maximum_bytes) {
    NativeHandle handle;
    i32 timeout_ms = 0;
    {
        std::scoped_lock lock(mutex_);
        auto found = entry(token);
        if (!found) return std::unexpected(found.error());
        if ((*found)->kind != ConnectionKind::datagram ||
            !(*found)->native.has_value()) {
            return fail(ErrorCode::invalid_state,
                        "connection is not a datagram socket");
        }
        handle = (*found)->native->handle;
        timeout_ms = (*found)->timeout_ms;
    }
    const auto adapter = adapter_;
    return await_operation<DatagramPacket>(
        adapter, timeout_ms,
        [&adapter, handle, maximum_bytes, timeout_ms](
            Completion<DatagramPacket> completion) {
            return adapter->receive_datagram(
                handle, std::min(maximum_bytes, kMaximumDatagramLength),
                timeout_ms, std::move(completion));
        });
}

Result<usize> ConnectionRegistry::maximum_datagram_length(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind != ConnectionKind::datagram) {
        return fail(ErrorCode::invalid_state,
                    "connection is not a datagram socket");
    }
    return kMaximumDatagramLength;
}

Status ConnectionRegistry::set_http_method(ConnectionToken token,
                                           std::string method) {
    if (!valid_http_token(method)) {
        return fail(ErrorCode::invalid_argument,
                    "HTTP request method is invalid");
    }
    std::transform(method.begin(), method.end(), method.begin(),
                   [](unsigned char character) {
                       if (character >= 'a' && character <= 'z') {
                           return static_cast<char>(character - 'a' + 'A');
                       }
                       return static_cast<char>(character);
                   });
    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind != ConnectionKind::http &&
        (*found)->kind != ConnectionKind::https) {
        return fail(ErrorCode::invalid_state,
                    "connection is not HTTP(S)");
    }
    if ((*found)->request_started) {
        return fail(ErrorCode::invalid_state,
                    "HTTP request is already committed");
    }
    (*found)->http_method = std::move(method);
    return {};
}

Result<std::string> ConnectionRegistry::http_method(
    ConnectionToken token) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind != ConnectionKind::http &&
        (*found)->kind != ConnectionKind::https) {
        return fail(ErrorCode::invalid_state,
                    "connection is not HTTP(S)");
    }
    return (*found)->http_method;
}

Status ConnectionRegistry::set_http_header(ConnectionToken token,
                                           std::string name,
                                           std::string value) {
    if (!valid_http_token(name) || !valid_header_value(value)) {
        return fail(ErrorCode::invalid_argument,
                    "HTTP header is invalid");
    }
    std::scoped_lock lock(mutex_);
    auto found = mutable_entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind != ConnectionKind::http &&
        (*found)->kind != ConnectionKind::https) {
        return fail(ErrorCode::invalid_state,
                    "connection is not HTTP(S)");
    }
    if ((*found)->request_started) {
        return fail(ErrorCode::invalid_state,
                    "HTTP request is already committed");
    }
    set_header((*found)->request_headers, std::move(name), std::move(value));
    return {};
}

Result<std::optional<std::string>>
ConnectionRegistry::http_request_header(ConnectionToken token,
                                        std::string_view name) const {
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return find_header((*found)->request_headers, name);
}

Result<i32> ConnectionRegistry::http_response_code(
    ConnectionToken token) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return (*found)->response->status_code;
}

Result<std::string> ConnectionRegistry::http_response_message(
    ConnectionToken token) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return (*found)->response->reason;
}

Result<std::optional<std::string>>
ConnectionRegistry::http_response_header(ConnectionToken token,
                                         std::string_view name) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    return find_header((*found)->response->headers, name);
}

Result<std::optional<Header>> ConnectionRegistry::http_response_header(
    ConnectionToken token,
    usize index) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if (index >= (*found)->response->headers.size()) {
        return std::optional<Header> {};
    }
    return std::optional<Header>((*found)->response->headers[index]);
}

Result<i64> ConnectionRegistry::http_content_length(
    ConnectionToken token) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    auto header = find_header((*found)->response->headers, "Content-Length");
    if (!header.has_value()) {
        const usize size = (*found)->response->body.size();
        if (size > static_cast<usize>(std::numeric_limits<i64>::max())) {
            return fail(ErrorCode::overflow,
                        "HTTP body length exceeds Java long");
        }
        return static_cast<i64>(size);
    }
    i64 length = -1;
    const auto converted = std::from_chars(
        header->data(), header->data() + header->size(), length);
    if (converted.ec != std::errc {} || length < 0) return -1;
    return length;
}

Result<std::optional<SecurityMetadata>>
ConnectionRegistry::http_security_info(ConnectionToken token) {
    auto prepared = ensure_http_response(token);
    if (!prepared) return std::unexpected(prepared.error());
    std::scoped_lock lock(mutex_);
    auto found = entry(token);
    if (!found) return std::unexpected(found.error());
    if ((*found)->kind != ConnectionKind::https) {
        return fail(ErrorCode::invalid_state,
                    "security information requires HTTPS");
    }
    return (*found)->response->security;
}

} // namespace phoneme::network
