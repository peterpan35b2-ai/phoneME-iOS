#include "phoneme/filesystem/FileSystem.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace phoneme::filesystem {
namespace {

class UniqueFd final {
public:
    explicit UniqueFd(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}
    ~UniqueFd() {
        if (descriptor_ >= 0) static_cast<void>(::close(descriptor_));
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0) static_cast<void>(::close(descriptor_));
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    int descriptor_ {-1};
};

[[nodiscard]] std::unexpected<Error> io_failure(
    std::string operation,
    int error_number = errno) {
    operation.append(": ");
    operation.append(std::strerror(error_number));
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
            if (current == '\0') {
                return fail(ErrorCode::invalid_argument,
                            "path contains a NUL byte");
            }
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
    bool allow_parent,
    bool reject_absolute) {
    if (source.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "path contains a NUL byte");
    }
    if (reject_absolute && !source.empty() &&
        (source.front() == '/' || source.front() == '\\')) {
        return fail(ErrorCode::invalid_argument,
                    "absolute paths are not allowed");
    }
    if (reject_absolute && source.size() >= 2U &&
        std::isalpha(static_cast<unsigned char>(source[0])) != 0 &&
        source[1] == ':') {
        return fail(ErrorCode::invalid_argument,
                    "drive-qualified paths are not allowed");
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
    auto canonical = std::filesystem::canonical(path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "cannot resolve " + std::string(label) + ": " +
                        error.message());
    }
    return canonical.string();
}

[[nodiscard]] bool is_path_prefix(const std::filesystem::path& prefix,
                                  const std::filesystem::path& path) {
    auto prefix_iterator = prefix.begin();
    auto path_iterator = path.begin();
    while (prefix_iterator != prefix.end()) {
        if (path_iterator == path.end() ||
            *prefix_iterator != *path_iterator) {
            return false;
        }
        ++prefix_iterator;
        ++path_iterator;
    }
    return true;
}

[[nodiscard]] bool paths_overlap(const std::string& first,
                                 const std::string& second) {
    const std::filesystem::path first_path(first);
    const std::filesystem::path second_path(second);
    return is_path_prefix(first_path, second_path) ||
           is_path_prefix(second_path, first_path);
}

[[nodiscard]] i64 modified_seconds(const struct stat& info) noexcept {
#if defined(__APPLE__)
    return static_cast<i64>(info.st_mtimespec.tv_sec);
#else
    return static_cast<i64>(info.st_mtim.tv_sec);
#endif
}

[[nodiscard]] std::string leaf_name(std::string_view normalized_path) {
    const usize slash = normalized_path.rfind('/');
    return std::string(normalized_path.substr(
        slash == std::string_view::npos ? 0U : slash + 1U));
}

} // namespace

Result<std::string> normalize_virtual_path(std::string_view path,
                                           bool allow_empty) {
    return normalize_components(path, allow_empty, false, true);
}

Result<std::string> normalize_resource_path(std::string_view path) {
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1U);
    }
    return normalize_components(path, false, true, false);
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
    if (url.find('?') != std::string_view::npos ||
        url.find('#') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "file URL query and fragment components are not supported");
    }

    auto decoded = percent_decode(url);
    if (!decoded) return std::unexpected(decoded.error());
    std::string_view path(*decoded);
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1U);
    }
    return normalize_virtual_path(path, true);
}

FileSystem::~FileSystem() {
    close_all();
    cleanup_temporary_root();
}

void FileSystem::cleanup_temporary_root() noexcept {
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
    close_all();
    cleanup_temporary_root();
    {
        std::scoped_lock lock(mutex_);
        sandbox_root_.clear();
        temporary_root_.clear();
        configured_ = false;
    }

    auto sandbox = canonical_directory(std::move(sandbox_root),
                                       "filesystem sandbox");
    if (!sandbox) return std::unexpected(sandbox.error());
    auto temporary = canonical_directory(std::move(temporary_root),
                                         "temporary directory");
    if (!temporary) return std::unexpected(temporary.error());
    if (paths_overlap(*sandbox, *temporary)) {
        return fail(ErrorCode::invalid_argument,
                    "persistent and temporary roots must not overlap");
    }

    auto configured_sandbox = sandbox_.configure(*sandbox);
    if (!configured_sandbox) return configured_sandbox;
    auto configured_temporary = temporary_.configure(*temporary);
    if (!configured_temporary) return configured_temporary;

    std::scoped_lock lock(mutex_);
    sandbox_root_ = std::move(*sandbox);
    temporary_root_ = std::move(*temporary);
    next_handle_ = 1;
    configured_ = true;
    mutation_generation_.store(0, std::memory_order_relaxed);
    return {};
}

bool FileSystem::configured() const noexcept {
    std::scoped_lock lock(mutex_);
    return configured_;
}

Result<i32> FileSystem::adopt_descriptor(int descriptor,
                                         bool readable,
                                         bool writable) {
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "cannot adopt an invalid file descriptor");
    }
    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::not_configured,
                    "filesystem sandbox has not been configured");
    }
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

Result<int> FileSystem::duplicate_descriptor(i32 handle,
                                             bool require_read,
                                             bool require_write) const {
    std::scoped_lock lock(mutex_);
    const auto iterator = handles_.find(handle);
    if (iterator == handles_.end()) {
        return fail(ErrorCode::invalid_state, "file handle is closed");
    }
    if (require_read && !iterator->second.readable) {
        return fail(ErrorCode::invalid_state,
                    "file handle is not readable");
    }
    if (require_write && !iterator->second.writable) {
        return fail(ErrorCode::invalid_state,
                    "file handle is not writable");
    }
    const int duplicate = ::dup(iterator->second.descriptor);
    if (duplicate < 0) {
        return io_failure("cannot duplicate file handle");
    }
    static_cast<void>(::fcntl(duplicate, F_SETFD, FD_CLOEXEC));
    return duplicate;
}

Result<i32> FileSystem::open(std::string_view virtual_path,
                             OpenMode mode,
                             bool create,
                             bool truncate_file) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());

    int flags = 0;
    bool readable = false;
    bool writable = false;
    switch (mode) {
    case OpenMode::read:
        flags = O_RDONLY;
        readable = true;
        break;
    case OpenMode::write:
        flags = O_WRONLY;
        writable = true;
        break;
    case OpenMode::read_write:
        flags = O_RDWR;
        readable = true;
        writable = true;
        break;
    case OpenMode::append:
        flags = O_WRONLY | O_APPEND;
        writable = true;
        break;
    }
    if (create) flags |= O_CREAT;
    if (truncate_file) flags |= O_TRUNC;

    auto descriptor = sandbox_.open_file(*path, flags,
                                         S_IRUSR | S_IWUSR);
    if (!descriptor) return std::unexpected(descriptor.error());
    auto handle = adopt_descriptor(*descriptor, readable, writable);
    if (!handle) {
        static_cast<void>(::close(*descriptor));
        return std::unexpected(handle.error());
    }
    if (create || truncate_file) {
        mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    }
    return *handle;
}

Result<usize> FileSystem::read(i32 handle, std::span<u8> destination) {
    auto descriptor = duplicate_descriptor(handle, true, false);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);
    for (;;) {
        const ssize_t count = ::read(lease.get(), destination.data(),
                                     destination.size());
        if (count >= 0) return static_cast<usize>(count);
        if (errno != EINTR) return io_failure("cannot read sandbox file");
    }
}

Result<usize> FileSystem::write(i32 handle,
                                std::span<const u8> source) {
    auto descriptor = duplicate_descriptor(handle, false, true);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);

    usize total = 0;
    while (total < source.size()) {
        const ssize_t count = ::write(lease.get(), source.data() + total,
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
    if (total > 0) {
        mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    }
    return total;
}

Result<i64> FileSystem::seek(i32 handle,
                             i64 offset,
                             SeekOrigin origin) {
    if (offset > static_cast<i64>(std::numeric_limits<off_t>::max()) ||
        offset < static_cast<i64>(std::numeric_limits<off_t>::min())) {
        return fail(ErrorCode::out_of_range,
                    "file seek offset is outside platform range");
    }
    auto descriptor = duplicate_descriptor(handle, false, false);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);

    int whence = SEEK_SET;
    switch (origin) {
    case SeekOrigin::begin: whence = SEEK_SET; break;
    case SeekOrigin::current: whence = SEEK_CUR; break;
    case SeekOrigin::end: whence = SEEK_END; break;
    }
    const off_t result = ::lseek(lease.get(), static_cast<off_t>(offset),
                                  whence);
    if (result < 0) return io_failure("cannot seek sandbox file");
    return static_cast<i64>(result);
}

Result<i64> FileSystem::position(i32 handle) {
    return seek(handle, 0, SeekOrigin::current);
}

Result<i64> FileSystem::size(i32 handle) {
    auto descriptor = duplicate_descriptor(handle, false, false);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);
    struct stat info {};
    if (::fstat(lease.get(), &info) != 0) {
        return io_failure("cannot stat sandbox file handle");
    }
    return static_cast<i64>(info.st_size);
}

Result<i64> FileSystem::available(i32 handle) {
    auto descriptor = duplicate_descriptor(handle, true, false);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);
    const off_t current = ::lseek(lease.get(), 0, SEEK_CUR);
    if (current < 0) return io_failure("cannot read sandbox file position");
    struct stat info {};
    if (::fstat(lease.get(), &info) != 0) {
        return io_failure("cannot stat sandbox file handle");
    }
    return info.st_size > current
               ? static_cast<i64>(info.st_size - current)
               : 0;
}

Status FileSystem::flush(i32 handle) {
    auto descriptor = duplicate_descriptor(handle, false, true);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd lease(*descriptor);
    if (::fsync(lease.get()) != 0) {
        return io_failure("cannot flush sandbox file");
    }
    return {};
}

Status FileSystem::close(i32 handle) {
    int descriptor = -1;
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = handles_.find(handle);
        if (iterator == handles_.end()) return {};
        descriptor = iterator->second.descriptor;
        handles_.erase(iterator);
    }
    if (descriptor >= 0 && ::close(descriptor) != 0) {
        return io_failure("cannot close sandbox file descriptor");
    }
    return {};
}

void FileSystem::close_all() noexcept {
    std::unordered_map<i32, Handle> handles;
    {
        std::scoped_lock lock(mutex_);
        handles.swap(handles_);
    }
    for (const auto& [handle, state] : handles) {
        static_cast<void>(handle);
        if (state.descriptor >= 0) {
            static_cast<void>(::close(state.descriptor));
        }
    }
}

Result<TemporaryFile> FileSystem::create_temporary(
    std::string_view prefix) {
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

    auto temporary = temporary_.create_temporary(safe_prefix);
    if (!temporary) return std::unexpected(temporary.error());
    auto handle = adopt_descriptor(temporary->descriptor, true, true);
    if (!handle) {
        static_cast<void>(::close(temporary->descriptor));
        static_cast<void>(temporary_.remove(temporary->relative_path, false));
        return std::unexpected(handle.error());
    }
    return TemporaryFile {
        .handle = *handle,
        .host_path = std::move(temporary->host_path),
    };
}

Result<FileInfo> FileSystem::stat(std::string_view virtual_path) const {
    auto path = normalize_virtual_path(virtual_path, true);
    if (!path) return std::unexpected(path.error());
    auto info = sandbox_.stat(*path, true);
    if (!info) return std::unexpected(info.error());
    if (!info->has_value()) return FileInfo {};

    const struct stat& status = info->value();
    const std::string name = leaf_name(*path);
    return FileInfo {
        .exists = true,
        .directory = S_ISDIR(status.st_mode),
        .readable = (status.st_mode & S_IRUSR) != 0,
        .writable = (status.st_mode & S_IWUSR) != 0,
        .hidden = !name.empty() && name.front() == '.',
        .size = S_ISREG(status.st_mode) && status.st_size > 0
                    ? static_cast<u64>(status.st_size)
                    : 0,
        .modified_seconds = modified_seconds(status),
    };
}

Status FileSystem::create_file(std::string_view virtual_path) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.create_file(*path);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::create_directory(std::string_view virtual_path) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.create_directory(*path);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::remove(std::string_view virtual_path, bool recursive) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.remove(*path, recursive);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::rename(std::string_view from, std::string_view to) {
    auto source = normalize_virtual_path(from);
    auto destination = normalize_virtual_path(to);
    if (!source) return std::unexpected(source.error());
    if (!destination) return std::unexpected(destination.error());
    auto result = sandbox_.rename(*source, *destination);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::truncate(std::string_view virtual_path, u64 length) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.truncate(*path, length);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::set_readable(std::string_view virtual_path,
                                bool readable) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.set_readable(*path, readable);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Status FileSystem::set_writable(std::string_view virtual_path,
                                bool writable) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.set_writable(*path, writable);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Result<std::vector<std::string>> FileSystem::list(
    std::string_view virtual_directory) const {
    auto path = normalize_virtual_path(virtual_directory, true);
    if (!path) return std::unexpected(path.error());
    auto entries = sandbox_.list(*path);
    if (!entries) return std::unexpected(entries.error());
    std::vector<std::string> names;
    names.reserve(entries->size());
    for (const SandboxDirectoryEntry& entry : *entries) {
        std::string name = entry.name;
        if (entry.directory) name.push_back('/');
        names.push_back(std::move(name));
    }
    return names;
}

Result<u64> FileSystem::directory_size(
    std::string_view virtual_directory,
    bool include_subdirectories) const {
    auto path = normalize_virtual_path(virtual_directory, true);
    if (!path) return std::unexpected(path.error());
    return sandbox_.directory_size(*path, include_subdirectories);
}

Result<StorageInfo> FileSystem::storage_info() const {
    auto info = sandbox_.space();
    if (!info) return std::unexpected(info.error());
    return StorageInfo {
        .available = info->available,
        .total = info->total,
        .used = info->used,
    };
}

Result<u64> FileSystem::available_size() const {
    auto info = storage_info();
    if (!info) return std::unexpected(info.error());
    return info->available;
}

Result<u64> FileSystem::total_size() const {
    auto info = storage_info();
    if (!info) return std::unexpected(info.error());
    return info->total;
}

Result<u64> FileSystem::used_size() const {
    auto info = storage_info();
    if (!info) return std::unexpected(info.error());
    return info->used;
}

Status FileSystem::atomic_write(std::string_view virtual_path,
                                std::span<const u8> contents) {
    auto path = normalize_virtual_path(virtual_path);
    if (!path) return std::unexpected(path.error());
    auto result = sandbox_.atomic_write(*path, contents);
    if (result) mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

Result<std::string> FileSystem::host_path(
    std::string_view virtual_path) const {
    auto path = normalize_virtual_path(virtual_path, true);
    if (!path) return std::unexpected(path.error());
    return sandbox_.lexical_host_path(*path, true);
}

} // namespace phoneme::filesystem
