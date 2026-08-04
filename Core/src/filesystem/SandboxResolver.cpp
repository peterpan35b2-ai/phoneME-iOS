#include "phoneme/filesystem/SandboxResolver.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace phoneme::filesystem {
namespace {

class UniqueFd final {
public:
    explicit UniqueFd(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.descriptor_, -1));
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return descriptor_; }
    [[nodiscard]] bool valid() const noexcept { return descriptor_ >= 0; }

    [[nodiscard]] int release() noexcept {
        return std::exchange(descriptor_, -1);
    }

    void reset(int replacement = -1) noexcept {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
        }
        descriptor_ = replacement;
    }

private:
    int descriptor_ {-1};
};

struct ParentHandle final {
    UniqueFd descriptor;
    std::string leaf;
};

[[nodiscard]] std::unexpected<Error> errno_failure(
    std::string operation,
    int error_number = errno,
    bool path_security_context = false) {
    operation.append(": ");
    operation.append(std::strerror(error_number));
    if (path_security_context &&
        (error_number == ELOOP || error_number == EMLINK)) {
        return fail(ErrorCode::invalid_argument, std::move(operation));
    }
    return fail(ErrorCode::io_error, std::move(operation));
}

[[nodiscard]] Status validate_normalized_path(std::string_view path,
                                              bool allow_empty) {
    if (path.empty()) {
        if (allow_empty) return {};
        return fail(ErrorCode::invalid_argument,
                    "sandbox path must name an entry");
    }
    if (path.front() == '/' || path.front() == '\\' ||
        path.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "sandbox path is not root-relative");
    }
    usize start = 0;
    while (start <= path.size()) {
        const usize slash = path.find('/', start);
        const usize end = slash == std::string_view::npos ? path.size()
                                                          : slash;
        const std::string_view component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return fail(ErrorCode::invalid_argument,
                        "sandbox path is not normalized");
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1U;
    }
    return {};
}

[[nodiscard]] Result<UniqueFd> open_child_directory(
    int parent_descriptor,
    std::string_view component) {
    std::string name(component);
    struct stat before {};
    if (::fstatat(parent_descriptor, name.c_str(), &before,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        return errno_failure("cannot inspect sandbox directory component",
                             errno, true);
    }
    if (S_ISLNK(before.st_mode)) {
        return fail(ErrorCode::invalid_argument,
                    "sandbox path contains a symbolic link");
    }
    const int descriptor = ::openat(
        parent_descriptor, name.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        const int error_number = errno;
        if (error_number == ENOTDIR) {
            struct stat after {};
            if (::fstatat(parent_descriptor, name.c_str(), &after,
                          AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISLNK(after.st_mode)) {
                return fail(ErrorCode::invalid_argument,
                            "sandbox path contains a symbolic link");
            }
        }
        return errno_failure("cannot open sandbox directory component",
                             error_number, true);
    }
    return UniqueFd(descriptor);
}

[[nodiscard]] Result<UniqueFd> walk_directory(
    UniqueFd root,
    std::string_view normalized_directory) {
    if (normalized_directory.empty()) return root;

    usize start = 0;
    while (start < normalized_directory.size()) {
        const usize slash = normalized_directory.find('/', start);
        const usize end = slash == std::string_view::npos
                              ? normalized_directory.size()
                              : slash;
        auto child = open_child_directory(
            root.get(), normalized_directory.substr(start, end - start));
        if (!child) return std::unexpected(child.error());
        root = std::move(*child);
        if (slash == std::string_view::npos) break;
        start = slash + 1U;
    }
    return root;
}

[[nodiscard]] Result<ParentHandle> open_parent(
    UniqueFd root,
    std::string_view normalized_path) {
    const usize slash = normalized_path.rfind('/');
    const std::string_view parent = slash == std::string_view::npos
                                        ? std::string_view {}
                                        : normalized_path.substr(0, slash);
    const std::string_view leaf = slash == std::string_view::npos
                                      ? normalized_path
                                      : normalized_path.substr(slash + 1U);
    auto directory = walk_directory(std::move(root), parent);
    if (!directory) return std::unexpected(directory.error());
    return ParentHandle {
        .descriptor = std::move(*directory),
        .leaf = std::string(leaf),
    };
}

[[nodiscard]] Result<std::optional<ParentHandle>> open_parent_for_stat(
    UniqueFd root,
    std::string_view normalized_path) {
    const usize slash = normalized_path.rfind('/');
    const std::string_view parent = slash == std::string_view::npos
                                        ? std::string_view {}
                                        : normalized_path.substr(0, slash);
    const std::string_view leaf = slash == std::string_view::npos
                                      ? normalized_path
                                      : normalized_path.substr(slash + 1U);
    usize start = 0U;
    while (start < parent.size()) {
        const usize separator = parent.find('/', start);
        const usize end = separator == std::string_view::npos
                              ? parent.size()
                              : separator;
        const std::string component(parent.substr(start, end - start));
        struct stat info {};
        if (::fstatat(root.get(), component.c_str(), &info,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            const int error_number = errno;
            if (error_number == ENOENT || error_number == ENOTDIR) {
                return std::optional<ParentHandle> {};
            }
            return errno_failure(
                "cannot inspect sandbox directory component",
                error_number, true);
        }
        if (S_ISLNK(info.st_mode)) {
            return fail(ErrorCode::invalid_argument,
                        "sandbox path contains a symbolic link");
        }
        if (!S_ISDIR(info.st_mode)) {
            return std::optional<ParentHandle> {};
        }
        const int descriptor = ::openat(
            root.get(), component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0) {
            const int error_number = errno;
            if (error_number == ENOENT || error_number == ENOTDIR) {
                return std::optional<ParentHandle> {};
            }
            return errno_failure(
                "cannot open sandbox directory component",
                error_number, true);
        }
        root = UniqueFd(descriptor);
        if (separator == std::string_view::npos) break;
        start = separator + 1U;
    }
    return std::optional<ParentHandle>(ParentHandle {
        .descriptor = std::move(root),
        .leaf = std::string(leaf),
    });
}

[[nodiscard]] Result<std::optional<struct stat>> stat_at(
    int parent_descriptor,
    std::string_view leaf) {
    std::string name(leaf);
    struct stat info {};
    if (::fstatat(parent_descriptor, name.c_str(), &info,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(info.st_mode)) {
            return fail(ErrorCode::invalid_argument,
                        "sandbox entry is a symbolic link");
        }
        return std::optional<struct stat>(info);
    }
    if (errno == ENOENT) return std::optional<struct stat> {};
    return errno_failure("cannot inspect sandbox entry", errno, true);
}

[[nodiscard]] Status write_all(int descriptor,
                               std::span<const u8> contents) {
    usize offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = ::write(descriptor,
                                      contents.data() + offset,
                                      contents.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            return errno_failure("cannot write sandbox file");
        }
        if (count == 0) {
            return fail(ErrorCode::io_error,
                        "sandbox write made no progress");
        }
        offset += static_cast<usize>(count);
    }
    return {};
}

[[nodiscard]] u64 saturated_product(u64 left, u64 right) noexcept {
    if (left == 0 || right == 0) return 0;
    if (left > std::numeric_limits<u64>::max() / right) {
        return std::numeric_limits<u64>::max();
    }
    return left * right;
}

[[nodiscard]] Result<u64> directory_size_from_fd(
    int directory_descriptor,
    bool include_subdirectories) {
    const int duplicate = ::dup(directory_descriptor);
    if (duplicate < 0) {
        return errno_failure("cannot duplicate sandbox directory");
    }
    DIR* directory = ::fdopendir(duplicate);
    if (directory == nullptr) {
        const int error_number = errno;
        static_cast<void>(::close(duplicate));
        return errno_failure("cannot enumerate sandbox directory",
                             error_number);
    }

    u64 total = 0;
    errno = 0;
    while (dirent* entry = ::readdir(directory)) {
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;

        struct stat info {};
        if (::fstatat(directory_descriptor, entry->d_name, &info,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            const int error_number = errno;
            static_cast<void>(::closedir(directory));
            return errno_failure("cannot inspect listed sandbox entry",
                                 error_number, true);
        }
        if (S_ISLNK(info.st_mode)) continue;
        if (S_ISREG(info.st_mode)) {
            const u64 size = info.st_size < 0
                                 ? 0
                                 : static_cast<u64>(info.st_size);
            total = size > std::numeric_limits<u64>::max() - total
                        ? std::numeric_limits<u64>::max()
                        : total + size;
            continue;
        }
        if (include_subdirectories && S_ISDIR(info.st_mode)) {
            const int child = ::openat(
                directory_descriptor, entry->d_name,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0) {
                const int error_number = errno;
                static_cast<void>(::closedir(directory));
                return errno_failure("cannot open listed sandbox directory",
                                     error_number, true);
            }
            UniqueFd child_handle(child);
            auto child_size = directory_size_from_fd(child_handle.get(), true);
            if (!child_size) {
                static_cast<void>(::closedir(directory));
                return std::unexpected(child_size.error());
            }
            total = *child_size > std::numeric_limits<u64>::max() - total
                        ? std::numeric_limits<u64>::max()
                        : total + *child_size;
        }
    }
    const int read_error = errno;
    static_cast<void>(::closedir(directory));
    if (read_error != 0) {
        return errno_failure("cannot enumerate sandbox directory",
                             read_error);
    }
    return total;
}

[[nodiscard]] Status remove_directory_contents(int directory_descriptor) {
    const int duplicate = ::dup(directory_descriptor);
    if (duplicate < 0) {
        return errno_failure("cannot duplicate sandbox directory");
    }
    DIR* directory = ::fdopendir(duplicate);
    if (directory == nullptr) {
        const int error_number = errno;
        static_cast<void>(::close(duplicate));
        return errno_failure("cannot enumerate sandbox directory",
                             error_number);
    }

    errno = 0;
    while (dirent* entry = ::readdir(directory)) {
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;

        struct stat info {};
        if (::fstatat(directory_descriptor, entry->d_name, &info,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            const int error_number = errno;
            static_cast<void>(::closedir(directory));
            return errno_failure("cannot inspect sandbox removal entry",
                                 error_number, true);
        }
        if (S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) {
            const int child = ::openat(
                directory_descriptor, entry->d_name,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0) {
                const int error_number = errno;
                static_cast<void>(::closedir(directory));
                return errno_failure("cannot open sandbox removal directory",
                                     error_number, true);
            }
            UniqueFd child_handle(child);
            auto removed = remove_directory_contents(child_handle.get());
            if (!removed) {
                static_cast<void>(::closedir(directory));
                return removed;
            }
            if (::unlinkat(directory_descriptor, entry->d_name,
                           AT_REMOVEDIR) != 0) {
                const int error_number = errno;
                static_cast<void>(::closedir(directory));
                return errno_failure("cannot remove sandbox directory",
                                     error_number, true);
            }
        } else if (::unlinkat(directory_descriptor, entry->d_name, 0) != 0) {
            const int error_number = errno;
            static_cast<void>(::closedir(directory));
            return errno_failure("cannot remove sandbox entry",
                                 error_number, true);
        }
    }
    const int read_error = errno;
    static_cast<void>(::closedir(directory));
    if (read_error != 0) {
        return errno_failure("cannot enumerate sandbox directory",
                             read_error);
    }
    return {};
}

[[nodiscard]] std::string temporary_leaf(std::string_view prefix,
                                         u64 sequence) {
    std::string result(prefix);
    result.push_back('-');
    result.append(std::to_string(static_cast<u64>(::getpid())));
    result.push_back('-');
    result.append(std::to_string(sequence));
    result.append(".tmp");
    return result;
}

[[nodiscard]] Status update_permission(
    UniqueFd root,
    std::string_view normalized_path,
    mode_t permission,
    bool enabled) {
    auto parent = open_parent(std::move(root), normalized_path);
    if (!parent) return std::unexpected(parent.error());
    auto info = stat_at(parent->descriptor.get(), parent->leaf);
    if (!info) return std::unexpected(info.error());
    if (!info->has_value()) {
        return fail(ErrorCode::io_error,
                    "sandbox entry does not exist");
    }
    mode_t mode = info->value().st_mode;
    mode = enabled ? static_cast<mode_t>(mode | permission)
                   : static_cast<mode_t>(mode & ~permission);
    if (::fchmodat(parent->descriptor.get(), parent->leaf.c_str(), mode,
                   AT_SYMLINK_NOFOLLOW) != 0) {
        return errno_failure("cannot update sandbox permissions",
                             errno, true);
    }
    return {};
}

} // namespace

SandboxResolver::~SandboxResolver() {
    std::scoped_lock lock(mutex_);
    if (root_descriptor_ >= 0) {
        static_cast<void>(::close(root_descriptor_));
        root_descriptor_ = -1;
    }
}

Status SandboxResolver::configure(std::string canonical_root) {
    if (canonical_root.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "sandbox root must not be empty");
    }
    const int descriptor = ::open(canonical_root.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                      O_NOFOLLOW);
    if (descriptor < 0) {
        return errno_failure("cannot open sandbox root", errno, true);
    }

    std::scoped_lock lock(mutex_);
    const int previous = std::exchange(root_descriptor_, descriptor);
    root_path_ = std::move(canonical_root);
    if (previous >= 0) static_cast<void>(::close(previous));
    return {};
}

bool SandboxResolver::configured() const noexcept {
    std::scoped_lock lock(mutex_);
    return root_descriptor_ >= 0;
}

void SandboxResolver::set_fault_injector(SandboxFaultInjector injector) {
    std::scoped_lock lock(mutex_);
    fault_injector_ = std::move(injector);
}

void SandboxResolver::clear_fault_injector() {
    std::scoped_lock lock(mutex_);
    fault_injector_ = {};
}

Status SandboxResolver::inject_fault(SandboxFaultPoint point) const {
    SandboxFaultInjector injector;
    {
        std::scoped_lock lock(mutex_);
        injector = fault_injector_;
    }
    return injector ? injector(point) : Status {};
}

Result<std::string> SandboxResolver::root_path() const {
    std::scoped_lock lock(mutex_);
    if (root_descriptor_ < 0) {
        return fail(ErrorCode::not_configured,
                    "sandbox resolver has not been configured");
    }
    return root_path_;
}

Result<int> SandboxResolver::duplicate_root() const {
    std::scoped_lock lock(mutex_);
    if (root_descriptor_ < 0) {
        return fail(ErrorCode::not_configured,
                    "sandbox resolver has not been configured");
    }
    const int duplicate = ::dup(root_descriptor_);
    if (duplicate < 0) {
        return errno_failure("cannot duplicate sandbox root");
    }
    static_cast<void>(::fcntl(duplicate, F_SETFD, FD_CLOEXEC));
    return duplicate;
}

Result<int> SandboxResolver::open_file(std::string_view normalized_path,
                                       int flags,
                                       mode_t mode) const {
    auto valid = validate_normalized_path(normalized_path, false);
    if (!valid) return std::unexpected(valid.error());
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    auto parent = open_parent(UniqueFd(*root), normalized_path);
    if (!parent) return std::unexpected(parent.error());

    const int descriptor = ::openat(parent->descriptor.get(),
                                    parent->leaf.c_str(),
                                    flags | O_CLOEXEC | O_NOFOLLOW,
                                    mode);
    if (descriptor < 0) {
        const int error_number = errno;
        if (error_number == ENOTDIR) {
            struct stat info {};
            if (::fstatat(parent->descriptor.get(), parent->leaf.c_str(),
                          &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISLNK(info.st_mode)) {
                return fail(ErrorCode::invalid_argument,
                            "sandbox file is a symbolic link");
            }
        }
        return errno_failure("cannot open sandbox file", error_number, true);
    }
    struct stat info {};
    if (::fstat(descriptor, &info) != 0) {
        const int error_number = errno;
        static_cast<void>(::close(descriptor));
        return errno_failure("cannot inspect opened sandbox file",
                             error_number);
    }
    if (!S_ISREG(info.st_mode)) {
        static_cast<void>(::close(descriptor));
        return fail(ErrorCode::io_error,
                    "sandbox path does not name a regular file");
    }
    return descriptor;
}

Result<int> SandboxResolver::open_directory(
    std::string_view normalized_path,
    bool allow_root) const {
    auto valid = validate_normalized_path(normalized_path, allow_root);
    if (!valid) return std::unexpected(valid.error());
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    auto directory = walk_directory(UniqueFd(*root), normalized_path);
    if (!directory) return std::unexpected(directory.error());
    return directory->release();
}

Result<std::optional<struct stat>> SandboxResolver::stat(
    std::string_view normalized_path,
    bool allow_root) const {
    auto valid = validate_normalized_path(normalized_path, allow_root);
    if (!valid) return std::unexpected(valid.error());
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    UniqueFd root_handle(*root);
    if (normalized_path.empty()) {
        struct stat info {};
        if (::fstat(root_handle.get(), &info) != 0) {
            return errno_failure("cannot inspect sandbox root");
        }
        return std::optional<struct stat>(info);
    }
    auto parent = open_parent_for_stat(
        std::move(root_handle), normalized_path);
    if (!parent) return std::unexpected(parent.error());
    if (!parent->has_value()) return std::optional<struct stat> {};
    return stat_at((*parent)->descriptor.get(), (*parent)->leaf);
}

Status SandboxResolver::create_file(std::string_view normalized_path,
                                    mode_t mode) const {
    auto descriptor = open_file(normalized_path,
                                O_WRONLY | O_CREAT | O_EXCL,
                                mode);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd handle(*descriptor);
    return {};
}

Status SandboxResolver::create_directory(
    std::string_view normalized_path,
    mode_t mode) const {
    auto valid = validate_normalized_path(normalized_path, false);
    if (!valid) return valid;
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    auto parent = open_parent(UniqueFd(*root), normalized_path);
    if (!parent) return std::unexpected(parent.error());
    if (::mkdirat(parent->descriptor.get(), parent->leaf.c_str(), mode) != 0) {
        return errno_failure("cannot create sandbox directory", errno, true);
    }
    return {};
}

Status SandboxResolver::remove(std::string_view normalized_path,
                               bool recursive) const {
    auto valid = validate_normalized_path(normalized_path, false);
    if (!valid) return valid;
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    auto parent = open_parent(UniqueFd(*root), normalized_path);
    if (!parent) return std::unexpected(parent.error());
    auto info = stat_at(parent->descriptor.get(), parent->leaf);
    if (!info) return std::unexpected(info.error());
    if (!info->has_value()) {
        return fail(ErrorCode::io_error,
                    "sandbox path does not exist");
    }

    if (S_ISDIR(info->value().st_mode)) {
        if (recursive) {
            const int directory = ::openat(
                parent->descriptor.get(), parent->leaf.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (directory < 0) {
                return errno_failure("cannot open sandbox directory for removal",
                                     errno, true);
            }
            UniqueFd directory_handle(directory);
            auto emptied = remove_directory_contents(directory_handle.get());
            if (!emptied) return emptied;
        }
        if (::unlinkat(parent->descriptor.get(), parent->leaf.c_str(),
                       AT_REMOVEDIR) != 0) {
            return errno_failure("cannot remove sandbox directory",
                                 errno, true);
        }
        return {};
    }

    if (::unlinkat(parent->descriptor.get(), parent->leaf.c_str(), 0) != 0) {
        return errno_failure("cannot remove sandbox file", errno, true);
    }
    return {};
}

Status SandboxResolver::rename(std::string_view normalized_from,
                               std::string_view normalized_to) const {
    auto source_valid = validate_normalized_path(normalized_from, false);
    auto destination_valid = validate_normalized_path(normalized_to, false);
    if (!source_valid) return source_valid;
    if (!destination_valid) return destination_valid;

    auto source_root = duplicate_root();
    auto destination_root = duplicate_root();
    if (!source_root) return std::unexpected(source_root.error());
    if (!destination_root) return std::unexpected(destination_root.error());
    auto source = open_parent(UniqueFd(*source_root), normalized_from);
    auto destination = open_parent(UniqueFd(*destination_root), normalized_to);
    if (!source) return std::unexpected(source.error());
    if (!destination) return std::unexpected(destination.error());

    auto source_info = stat_at(source->descriptor.get(), source->leaf);
    if (!source_info) return std::unexpected(source_info.error());
    if (!source_info->has_value()) {
        return fail(ErrorCode::io_error,
                    "sandbox rename source does not exist");
    }
    auto destination_info = stat_at(destination->descriptor.get(),
                                    destination->leaf);
    if (!destination_info) return std::unexpected(destination_info.error());
    if (destination_info->has_value()) {
        return fail(ErrorCode::io_error,
                    "sandbox rename destination already exists");
    }

#if defined(__APPLE__)
    if (::renameatx_np(source->descriptor.get(), source->leaf.c_str(),
                       destination->descriptor.get(),
                       destination->leaf.c_str(), RENAME_EXCL) != 0) {
#else
    if (::renameat(source->descriptor.get(), source->leaf.c_str(),
                   destination->descriptor.get(),
                   destination->leaf.c_str()) != 0) {
#endif
        return errno_failure("cannot rename sandbox entry", errno, true);
    }
    return {};
}

Status SandboxResolver::truncate(std::string_view normalized_path,
                                 u64 length) const {
    if (length > static_cast<u64>(std::numeric_limits<off_t>::max())) {
        return fail(ErrorCode::out_of_range,
                    "truncate length is outside platform range");
    }
    auto descriptor = open_file(normalized_path, O_WRONLY);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd handle(*descriptor);
    if (::ftruncate(handle.get(), static_cast<off_t>(length)) != 0) {
        return errno_failure("cannot truncate sandbox file");
    }
    return {};
}

Status SandboxResolver::set_readable(
    std::string_view normalized_path,
    bool readable) const {
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    return update_permission(UniqueFd(*root), normalized_path,
                             S_IRUSR, readable);
}

Status SandboxResolver::set_writable(
    std::string_view normalized_path,
    bool writable) const {
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    return update_permission(UniqueFd(*root), normalized_path,
                             S_IWUSR, writable);
}

Result<std::vector<SandboxDirectoryEntry>> SandboxResolver::list(
    std::string_view normalized_directory) const {
    auto descriptor = open_directory(normalized_directory, true);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd handle(*descriptor);
    const int duplicate = ::dup(handle.get());
    if (duplicate < 0) {
        return errno_failure("cannot duplicate sandbox directory");
    }
    DIR* directory = ::fdopendir(duplicate);
    if (directory == nullptr) {
        const int error_number = errno;
        static_cast<void>(::close(duplicate));
        return errno_failure("cannot enumerate sandbox directory",
                             error_number);
    }

    std::vector<SandboxDirectoryEntry> entries;
    errno = 0;
    while (dirent* entry = ::readdir(directory)) {
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;
        struct stat info {};
        if (::fstatat(handle.get(), entry->d_name, &info,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            const int error_number = errno;
            static_cast<void>(::closedir(directory));
            return errno_failure("cannot inspect listed sandbox entry",
                                 error_number, true);
        }
        if (S_ISLNK(info.st_mode)) continue;
        entries.push_back(SandboxDirectoryEntry {
            .name = std::string(name),
            .directory = S_ISDIR(info.st_mode),
            .hidden = !name.empty() && name.front() == '.',
        });
    }
    const int read_error = errno;
    static_cast<void>(::closedir(directory));
    if (read_error != 0) {
        return errno_failure("cannot enumerate sandbox directory",
                             read_error);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& left, const auto& right) {
                  return left.name < right.name;
              });
    return entries;
}

Result<u64> SandboxResolver::directory_size(
    std::string_view normalized_directory,
    bool include_subdirectories) const {
    auto descriptor = open_directory(normalized_directory, true);
    if (!descriptor) return std::unexpected(descriptor.error());
    UniqueFd handle(*descriptor);
    return directory_size_from_fd(handle.get(), include_subdirectories);
}

Result<SandboxSpace> SandboxResolver::space() const {
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    UniqueFd handle(*root);
    struct statvfs info {};
    if (::fstatvfs(handle.get(), &info) != 0) {
        return errno_failure("cannot inspect sandbox filesystem capacity");
    }
    const u64 block_size = info.f_frsize == 0
                               ? static_cast<u64>(info.f_bsize)
                               : static_cast<u64>(info.f_frsize);
    const u64 total = saturated_product(static_cast<u64>(info.f_blocks),
                                        block_size);
    const u64 available = saturated_product(
        static_cast<u64>(info.f_bavail), block_size);
    const u64 free = saturated_product(static_cast<u64>(info.f_bfree),
                                       block_size);
    return SandboxSpace {
        .available = available,
        .total = total,
        .used = total >= free ? total - free : 0,
    };
}

Result<SandboxTemporaryFile> SandboxResolver::create_temporary(
    std::string_view safe_prefix) const {
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    UniqueFd root_handle(*root);
    auto host_root = root_path();
    if (!host_root) return std::unexpected(host_root.error());

    static std::atomic_uint64_t sequence {1};
    for (usize attempt = 0; attempt < 256U; ++attempt) {
        const std::string leaf = temporary_leaf(
            safe_prefix, sequence.fetch_add(1, std::memory_order_relaxed));
        const int descriptor = ::openat(
            root_handle.get(), leaf.c_str(),
            O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
        if (descriptor >= 0) {
            return SandboxTemporaryFile {
                .descriptor = descriptor,
                .relative_path = leaf,
                .host_path = *host_root + "/" + leaf,
            };
        }
        if (errno != EEXIST) {
            return errno_failure("cannot create sandbox temporary file",
                                 errno, true);
        }
    }
    return fail(ErrorCode::overflow,
                "cannot allocate a unique temporary file name");
}

Status SandboxResolver::atomic_write(
    std::string_view normalized_path,
    std::span<const u8> contents) const {
    auto valid = validate_normalized_path(normalized_path, false);
    if (!valid) return valid;
    auto root = duplicate_root();
    if (!root) return std::unexpected(root.error());
    auto parent = open_parent(UniqueFd(*root), normalized_path);
    if (!parent) return std::unexpected(parent.error());

    auto destination = stat_at(parent->descriptor.get(), parent->leaf);
    if (!destination) return std::unexpected(destination.error());
    if (destination->has_value() &&
        !S_ISREG(destination->value().st_mode)) {
        return fail(ErrorCode::io_error,
                    "atomic write destination is not a regular file");
    }
    const mode_t temporary_mode = destination->has_value()
                                      ? static_cast<mode_t>(
                                            destination->value().st_mode & 0777)
                                      : static_cast<mode_t>(S_IRUSR | S_IWUSR);

    static std::atomic_uint64_t sequence {1};
    std::string temporary;
    UniqueFd temporary_handle;
    for (usize attempt = 0; attempt < 256U; ++attempt) {
        temporary = temporary_leaf(
            ".phoneme-atomic",
            sequence.fetch_add(1, std::memory_order_relaxed));
        const int descriptor = ::openat(
            parent->descriptor.get(), temporary.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            temporary_mode);
        if (descriptor >= 0) {
            temporary_handle.reset(descriptor);
            break;
        }
        if (errno != EEXIST) {
            return errno_failure("cannot create atomic sandbox file",
                                 errno, true);
        }
    }
    if (!temporary_handle.valid()) {
        return fail(ErrorCode::overflow,
                    "cannot allocate an atomic sandbox file name");
    }

    const auto remove_temporary = [&] {
        static_cast<void>(::unlinkat(parent->descriptor.get(),
                                     temporary.c_str(), 0));
    };

    auto written = write_all(temporary_handle.get(), contents);
    if (!written) {
        remove_temporary();
        return written;
    }
    auto injected = inject_fault(SandboxFaultPoint::atomic_file_sync);
    if (!injected) {
        remove_temporary();
        return injected;
    }
    if (::fsync(temporary_handle.get()) != 0) {
        const int error_number = errno;
        remove_temporary();
        return errno_failure("cannot flush atomic sandbox file",
                             error_number);
    }
    temporary_handle.reset();

    std::string backup;
    bool has_backup = false;
    if (destination->has_value()) {
        for (usize attempt = 0; attempt < 256U; ++attempt) {
            backup = temporary_leaf(
                ".phoneme-backup",
                sequence.fetch_add(1, std::memory_order_relaxed));
            if (::linkat(parent->descriptor.get(), parent->leaf.c_str(),
                         parent->descriptor.get(), backup.c_str(), 0) == 0) {
                has_backup = true;
                break;
            }
            if (errno != EEXIST) {
                const int error_number = errno;
                remove_temporary();
                return errno_failure("cannot preserve atomic write backup",
                                     error_number, true);
            }
        }
        if (!has_backup) {
            remove_temporary();
            return fail(ErrorCode::overflow,
                        "cannot allocate an atomic write backup name");
        }

        injected = inject_fault(SandboxFaultPoint::atomic_backup_sync);
        if (!injected || ::fsync(parent->descriptor.get()) != 0) {
            const Error error = !injected
                ? injected.error()
                : Error::make(ErrorCode::io_error,
                              "cannot synchronize atomic write backup");
            static_cast<void>(::unlinkat(parent->descriptor.get(),
                                         backup.c_str(), 0));
            remove_temporary();
            return std::unexpected(error);
        }
    }

    if (::renameat(parent->descriptor.get(), temporary.c_str(),
                   parent->descriptor.get(), parent->leaf.c_str()) != 0) {
        const int error_number = errno;
        if (has_backup) {
            static_cast<void>(::unlinkat(parent->descriptor.get(),
                                         backup.c_str(), 0));
        }
        remove_temporary();
        return errno_failure("cannot install atomic sandbox file",
                             error_number, true);
    }

    const auto rollback = [&](const Error& original) -> Status {
        bool restored = false;
        if (has_backup) {
            restored = ::renameat(parent->descriptor.get(), backup.c_str(),
                                  parent->descriptor.get(),
                                  parent->leaf.c_str()) == 0;
        } else {
            restored = ::unlinkat(parent->descriptor.get(),
                                  parent->leaf.c_str(), 0) == 0 ||
                       errno == ENOENT;
        }
        auto rollback_injected = inject_fault(
            SandboxFaultPoint::atomic_rollback_sync);
        const bool synchronized = rollback_injected.has_value() &&
                                  ::fsync(parent->descriptor.get()) == 0;
        if (!restored || !synchronized) {
            return fail(ErrorCode::io_error,
                        "atomic write failed and rollback could not be made durable");
        }
        return std::unexpected(original);
    };

    injected = inject_fault(SandboxFaultPoint::atomic_install_sync);
    if (!injected) return rollback(injected.error());
    if (::fsync(parent->descriptor.get()) != 0) {
        return rollback(Error::make(
            ErrorCode::io_error,
            "cannot flush sandbox directory after atomic write"));
    }

    if (has_backup) {
        if (::unlinkat(parent->descriptor.get(), backup.c_str(), 0) == 0) {
            static_cast<void>(::fsync(parent->descriptor.get()));
        }
    }
    return {};
}

Result<std::string> SandboxResolver::lexical_host_path(
    std::string_view normalized_path,
    bool allow_root) const {
    auto valid = validate_normalized_path(normalized_path, allow_root);
    if (!valid) return std::unexpected(valid.error());
    auto root = root_path();
    if (!root) return std::unexpected(root.error());
    if (normalized_path.empty()) return *root;
    return *root + "/" + std::string(normalized_path);
}

} // namespace phoneme::filesystem
