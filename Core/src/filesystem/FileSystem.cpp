#include "phoneme/filesystem/FileSystem.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace phoneme::filesystem {
namespace {

[[nodiscard]] std::unexpected<Error> io_failure(std::string operation) {
    operation.append(": ");
    operation.append(std::strerror(errno));
    return fail(ErrorCode::io_error, std::move(operation));
}

[[nodiscard]] bool is_hex(char value) noexcept {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] u8 hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<u8>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<u8>(10 + value - 'a');
    }
    return static_cast<u8>(10 + value - 'A');
}

[[nodiscard]] Result<std::string> percent_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (usize index = 0; index < value.size(); ++index) {
        const char current = value[index];
        if (current != '%') {
            decoded.push_back(current);
            continue;
        }
        if (index + 2U >= value.size() || !is_hex(value[index + 1U]) ||
            !is_hex(value[index + 2U])) {
            return fail(ErrorCode::invalid_argument,
                        "file URL contains invalid percent encoding");
        }
        const u8 byte = static_cast<u8>(
            static_cast<u8>(hex_value(value[index + 1U]) << 4U) |
            hex_value(value[index + 2U]));
        if (byte == 0U) {
            return fail(ErrorCode::invalid_argument,
                        "path contains an encoded NUL byte");
        }
        decoded.push_back(static_cast<char>(byte));
        index += 2U;
    }
    return decoded;
}

[[nodiscard]] Result<std::string> normalize_components(
    std::string_view source,
    bool allow_empty,
    bool allow_parent) {
    if (source.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "path contains a NUL byte");
    }

    std::string path(source);
    for (char& character : path) {
        if (character == '\\') character = '/';
    }

    std::vector<std::string> components;
    usize start = 0;
    while (start <= path.size()) {
        const usize separator = path.find('/', start);
        const usize end = separator == std::string::npos
                              ? path.size()
                              : separator;
        const std::string_view component(path.data() + start, end - start);
        if (!component.empty() && component != ".") {
            if (component == "..") {
                if (!allow_parent || components.empty()) {
                    return fail(ErrorCode::invalid_argument,
                                "path attempts to escape its root");
                }
                components.pop_back();
            } else {
                components.emplace_back(component);
            }
        }
        if (separator == std::string::npos) break;
        start = separator + 1U;
    }

    std::string normalized;
    for (const std::string& component : components) {
        if (!normalized.empty()) normalized.push_back('/');
        normalized.append(component);
    }
    if (normalized.empty() && !allow_empty) {
        return fail(ErrorCode::invalid_argument,
                    "path resolves to the sandbox root");
    }
    return normalized;
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& child,
                                  const std::filesystem::path& root) {
    auto child_iterator = child.begin();
    for (auto root_iterator = root.begin(); root_iterator != root.end();
         ++root_iterator, ++child_iterator) {
        if (child_iterator == child.end() || *child_iterator != *root_iterator) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<std::string> canonical_directory(
    std::string path,
    std::string_view label) {
    if (path.empty()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(label) + " must not be empty");
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot create " + std::string(label) + ": " +
                        error.message());
    }
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot resolve " + std::string(label) + ": " +
                        error.message());
    }
    return canonical.string();
}

[[nodiscard]] i64 file_time_seconds(
    const std::filesystem::file_time_type& time) noexcept {
    const auto system_time = std::chrono::time_point_cast<std::chrono::seconds>(
        time - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    return system_time.time_since_epoch().count();
}

} // namespace

Result<std::string> normalize_virtual_path(std::string_view path,
                                           bool allow_empty) {
    return normalize_components(path, allow_empty, false);
}

Result<std::string> normalize_resource_path(std::string_view path) {
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1U);
    }
    return normalize_components(path, false, true);
}

Result<std::string> path_from_file_url(std::string_view url) {
    constexpr std::string_view scheme = "file:";
    if (!url.starts_with(scheme)) {
        return fail(ErrorCode::invalid_argument,
                    "connection URL does not use the file scheme");
    }
    url.remove_prefix(scheme.size());
    if (url.starts_with("//")) {
        url.remove_prefix(2U);
        if (!url.empty() && url.front() != '/') {
            const usize slash = url.find('/');
            const std::string_view authority = url.substr(0, slash);
            if (!authority.empty() && authority != "localhost") {
                return fail(ErrorCode::invalid_argument,
                            "file URL authorities are not supported");
            }
            url = slash == std::string_view::npos
                      ? std::string_view {}
                      : url.substr(slash);
        }
    }
    auto decoded = percent_decode(url);
    if (!decoded) return std::unexpected(decoded.error());
    return normalize_virtual_path(*decoded, false);
}

FileSystem::~FileSystem() {
    close_all();
    std::string temporary_root;
    std::string sandbox_root;
    {
        std::scoped_lock lock(mutex_);
        temporary_root = temporary_root_;
        sandbox_root = sandbox_root_;
    }
    if (!temporary_root.empty() && temporary_root != sandbox_root) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary_root, ignored);
    }
}

Status FileSystem::configure(std::string sandbox_root,
                             std::string temporary_root) {
    auto sandbox = canonical_directory(std::move(sandbox_root),
                                       "filesystem sandbox");
    if (!sandbox) return std::unexpected(sandbox.error());
    auto temporary = canonical_directory(std::move(temporary_root),
                                         "temporary directory");
    if (!temporary) return std::unexpected(temporary.error());

    std::scoped_lock lock(mutex_);
    for (auto& [handle, state] : handles_) {
        static_cast<void>(handle);
        if (state.descriptor >= 0) {
            static_cast<void>(::close(state.descriptor));
        }
    }
    handles_.clear();
    next_handle_ = 1;
    sandbox_root_ = std::move(*sandbox);
    temporary_root_ = std::move(*temporary);
    configured_ = true;
    return {};
}

bool FileSystem::configured() const noexcept {
    std::scoped_lock lock(mutex_);
    return configured_;
}

Result<std::string> FileSystem::resolve(std::string_view virtual_path,
                                        bool allow_empty) const {
    std::string sandbox_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "filesystem sandbox has not been configured");
        }
        sandbox_root = sandbox_root_;
    }
    auto normalized = normalize_virtual_path(virtual_path, allow_empty);
    if (!normalized) return std::unexpected(normalized.error());

    std::error_code error;
    const std::filesystem::path root(sandbox_root);
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root / *normalized, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot resolve sandbox path: " + error.message());
    }
    if (!path_is_within(candidate, root)) {
        return fail(ErrorCode::invalid_argument,
                    "resolved path escapes the application sandbox");
    }
    return candidate.string();
}

Result<std::string> FileSystem::host_path(
    std::string_view virtual_path) const {
    return resolve(virtual_path, true);
}

Result<i32> FileSystem::adopt_descriptor(int descriptor,
                                         bool readable,
                                         bool writable) {
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "cannot adopt an invalid file descriptor");
    }
    std::scoped_lock lock(mutex_);
    for (usize attempts = 0;
         attempts < static_cast<usize>(std::numeric_limits<i32>::max());
         ++attempts) {
        const i32 candidate = next_handle_;
        next_handle_ = next_handle_ == std::numeric_limits<i32>::max()
                           ? 1
                           : next_handle_ + 1;
        if (candidate <= 0 || handles_.contains(candidate)) continue;
        handles_.emplace(candidate, Handle {
            .descriptor = descriptor,
            .readable = readable,
            .writable = writable,
        });
        return candidate;
    }
    return fail(ErrorCode::overflow, "file handle table is exhausted");
}

Result<i32> FileSystem::open(std::string_view virtual_path,
                             OpenMode mode,
                             bool create,
                             bool truncate_file) {
    auto path = resolve(virtual_path);
    if (!path) return std::unexpected(path.error());

    int flags = O_CLOEXEC;
    bool readable = false;
    bool writable = false;
    switch (mode) {
    case OpenMode::read:
        flags |= O_RDONLY;
        readable = true;
        break;
    case OpenMode::write:
        flags |= O_WRONLY;
        writable = true;
        break;
    case OpenMode::read_write:
        flags |= O_RDWR;
        readable = true;
        writable = true;
        break;
    case OpenMode::append:
        flags |= O_WRONLY | O_APPEND;
        writable = true;
        break;
    }
    if (create) flags |= O_CREAT;
    if (truncate_file) flags |= O_TRUNC;

    const int descriptor = ::open(path->c_str(), flags, S_IRUSR | S_IWUSR);
    if (descriptor < 0) return io_failure("cannot open sandbox file");
    auto handle = adopt_descriptor(descriptor, readable, writable);
    if (!handle) {
        static_cast<void>(::close(descriptor));
        return std::unexpected(handle.error());
    }
    return *handle;
}

Result<usize> FileSystem::read(i32 handle, std::span<u8> destination) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    if (!iterator->second.readable) {
        return fail(ErrorCode::invalid_state,
                    "file handle is not readable");
    }
    const ssize_t count = ::read(iterator->second.descriptor,
                                 destination.data(), destination.size());
    if (count < 0) return io_failure("cannot read sandbox file");
    return static_cast<usize>(count);
}

Result<usize> FileSystem::write(i32 handle,
                                std::span<const u8> source) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    if (!iterator->second.writable) {
        return fail(ErrorCode::invalid_state,
                    "file handle is not writable");
    }

    usize total = 0;
    while (total < source.size()) {
        const ssize_t count = ::write(iterator->second.descriptor,
                                      source.data() + total,
                                      source.size() - total);
        if (count < 0) {
            if (errno == EINTR) continue;
            return io_failure("cannot write sandbox file");
        }
        if (count == 0) {
            return fail(ErrorCode::io_error,
                        "sandbox file write made no progress");
        }
        total += static_cast<usize>(count);
    }
    return total;
}

Result<i64> FileSystem::seek(i32 handle,
                             i64 offset,
                             SeekOrigin origin) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    int whence = SEEK_SET;
    switch (origin) {
    case SeekOrigin::begin: whence = SEEK_SET; break;
    case SeekOrigin::current: whence = SEEK_CUR; break;
    case SeekOrigin::end: whence = SEEK_END; break;
    }
    if (offset > static_cast<i64>(std::numeric_limits<off_t>::max()) ||
        offset < static_cast<i64>(std::numeric_limits<off_t>::min())) {
        return fail(ErrorCode::out_of_range,
                    "file seek offset is outside platform range");
    }
    const off_t position = ::lseek(iterator->second.descriptor,
                                   static_cast<off_t>(offset), whence);
    if (position < 0) return io_failure("cannot seek sandbox file");
    return static_cast<i64>(position);
}

Result<i64> FileSystem::position(i32 handle) {
    return seek(handle, 0, SeekOrigin::current);
}

Result<i64> FileSystem::size(i32 handle) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    struct stat info {};
    if (::fstat(iterator->second.descriptor, &info) != 0) {
        return io_failure("cannot stat sandbox file handle");
    }
    return static_cast<i64>(info.st_size);
}

Result<i64> FileSystem::available(i32 handle) {
    auto current = position(handle);
    if (!current) return std::unexpected(current.error());
    auto end = size(handle);
    if (!end) return std::unexpected(end.error());
    return *end > *current ? *end - *current : 0;
}

Status FileSystem::flush(i32 handle) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    if (!iterator->second.writable) return {};
    if (::fsync(iterator->second.descriptor) != 0) {
        return io_failure("cannot flush sandbox file");
    }
    return {};
}

Status FileSystem::close(i32 handle) {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) return {};
    const int descriptor = iterator->second.descriptor;
    handles_.erase(iterator);
    if (descriptor >= 0 && ::close(descriptor) != 0) {
        return fail(ErrorCode::io_error,
                    "cannot close sandbox file descriptor");
    }
    return {};
}

void FileSystem::close_all() noexcept {
    std::scoped_lock lock(mutex_);
    for (auto& [handle, state] : handles_) {
        static_cast<void>(handle);
        if (state.descriptor >= 0) {
            static_cast<void>(::close(state.descriptor));
        }
    }
    handles_.clear();
}

Result<TemporaryFile> FileSystem::create_temporary(
    std::string_view prefix) {
    std::string temporary_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "filesystem sandbox has not been configured");
        }
        temporary_root = temporary_root_;
    }
    std::string safe_prefix;
    safe_prefix.reserve(std::min<usize>(prefix.size(), 32U));
    for (const char character : prefix) {
        if (safe_prefix.size() >= 32U) break;
        const unsigned char byte = static_cast<unsigned char>(character);
        safe_prefix.push_back(std::isalnum(byte) != 0 || character == '-' ||
                                      character == '_'
                                  ? character
                                  : '_');
    }
    if (safe_prefix.empty()) safe_prefix = "tmp";

    std::string pattern = temporary_root + "/" + safe_prefix + "-XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const int descriptor = ::mkstemp(writable.data());
    if (descriptor < 0) return io_failure("cannot create temporary file");
    static_cast<void>(::fcntl(descriptor, F_SETFD, FD_CLOEXEC));

    auto handle = adopt_descriptor(descriptor, true, true);
    if (!handle) {
        static_cast<void>(::close(descriptor));
        static_cast<void>(::unlink(writable.data()));
        return std::unexpected(handle.error());
    }
    return TemporaryFile {
        .handle = *handle,
        .host_path = writable.data(),
    };
}

Result<FileInfo> FileSystem::stat(std::string_view virtual_path) const {
    auto path = resolve(virtual_path, true);
    if (!path) return std::unexpected(path.error());
    std::error_code error;
    const std::filesystem::path host(*path);
    const auto status = std::filesystem::status(host, error);
    if (error == std::errc::no_such_file_or_directory) return FileInfo {};
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot stat sandbox path: " + error.message());
    }
    const bool exists = std::filesystem::exists(status);
    const auto filename = host.filename().string();
    FileInfo result {
        .exists = exists,
        .directory = exists && std::filesystem::is_directory(status),
        .readable = exists && ::access(path->c_str(), R_OK) == 0,
        .writable = exists && ::access(path->c_str(), W_OK) == 0,
        .hidden = !filename.empty() && filename.front() == '.',
        .size = 0,
        .modified_seconds = 0,
    };
    if (exists && std::filesystem::is_regular_file(status)) {
        result.size = std::filesystem::file_size(host, error);
        if (error) {
            return fail(ErrorCode::io_error,
                        "cannot read sandbox file size: " + error.message());
        }
    }
    if (exists) {
        const auto modified = std::filesystem::last_write_time(host, error);
        if (!error) result.modified_seconds = file_time_seconds(modified);
    }
    return result;
}

Status FileSystem::create_file(std::string_view virtual_path) {
    auto path = resolve(virtual_path);
    if (!path) return std::unexpected(path.error());
    const int descriptor = ::open(path->c_str(), O_CREAT | O_EXCL | O_WRONLY |
                                                     O_CLOEXEC,
                                  S_IRUSR | S_IWUSR);
    if (descriptor < 0) return io_failure("cannot create sandbox file");
    if (::close(descriptor) != 0) {
        return io_failure("cannot close newly created sandbox file");
    }
    return {};
}

Status FileSystem::create_directory(std::string_view virtual_path) {
    auto path = resolve(virtual_path);
    if (!path) return std::unexpected(path.error());
    std::error_code error;
    const bool created = std::filesystem::create_directory(*path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot create sandbox directory: " + error.message());
    }
    if (!created) {
        return fail(ErrorCode::invalid_state,
                    "sandbox directory already exists");
    }
    return {};
}

Status FileSystem::remove(std::string_view virtual_path, bool recursive) {
    auto path = resolve(virtual_path);
    if (!path) return std::unexpected(path.error());
    std::error_code error;
    const auto removed = recursive
                             ? std::filesystem::remove_all(*path, error)
                             : static_cast<std::uintmax_t>(
                                   std::filesystem::remove(*path, error));
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot remove sandbox path: " + error.message());
    }
    if (removed == 0U) {
        return fail(ErrorCode::invalid_state,
                    "sandbox path does not exist");
    }
    return {};
}

Status FileSystem::rename(std::string_view from, std::string_view to) {
    auto source = resolve(from);
    auto destination = resolve(to);
    if (!source) return std::unexpected(source.error());
    if (!destination) return std::unexpected(destination.error());
    std::error_code error;
    std::filesystem::rename(*source, *destination, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot rename sandbox path: " + error.message());
    }
    return {};
}

Status FileSystem::truncate(std::string_view virtual_path, u64 length) {
    if (length > static_cast<u64>(std::numeric_limits<off_t>::max())) {
        return fail(ErrorCode::out_of_range,
                    "truncate length is outside platform range");
    }
    auto path = resolve(virtual_path);
    if (!path) return std::unexpected(path.error());
    if (::truncate(path->c_str(), static_cast<off_t>(length)) != 0) {
        return io_failure("cannot truncate sandbox file");
    }
    return {};
}

Result<std::vector<std::string>> FileSystem::list(
    std::string_view virtual_directory) const {
    auto path = resolve(virtual_directory, true);
    if (!path) return std::unexpected(path.error());
    std::error_code error;
    std::filesystem::directory_iterator iterator(*path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot list sandbox directory: " + error.message());
    }
    std::vector<std::string> names;
    for (const auto& entry : iterator) {
        std::string name = entry.path().filename().string();
        std::error_code type_error;
        if (entry.is_directory(type_error) && !type_error) name.push_back('/');
        names.push_back(std::move(name));
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace phoneme::filesystem
