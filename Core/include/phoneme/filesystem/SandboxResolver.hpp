#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>

#include "phoneme/base/Error.hpp"

namespace phoneme::filesystem {

struct SandboxDirectoryEntry final {
    std::string name;
    bool directory {false};
    bool hidden {false};
};

struct SandboxSpace final {
    u64 available {0};
    u64 total {0};
    u64 used {0};
};

enum class SandboxFaultPoint : u8 {
    atomic_file_sync,
    atomic_backup_sync,
    atomic_install_sync,
    atomic_rollback_sync,
};

using SandboxFaultInjector = std::function<Status(SandboxFaultPoint)>;

struct SandboxTemporaryFile final {
    int descriptor {-1};
    std::string relative_path;
    std::string host_path;
};

class SandboxResolver final {
public:
    SandboxResolver() = default;
    ~SandboxResolver();

    SandboxResolver(const SandboxResolver&) = delete;
    SandboxResolver& operator=(const SandboxResolver&) = delete;

    [[nodiscard]] Status configure(std::string canonical_root);
    [[nodiscard]] bool configured() const noexcept;
    void set_fault_injector(SandboxFaultInjector injector);
    void clear_fault_injector();
    [[nodiscard]] Result<std::string> root_path() const;

    [[nodiscard]] Result<int> open_file(std::string_view normalized_path,
                                        int flags,
                                        mode_t mode = S_IRUSR | S_IWUSR) const;
    [[nodiscard]] Result<int> open_directory(
        std::string_view normalized_path,
        bool allow_root = true) const;
    [[nodiscard]] Result<std::optional<struct stat>> stat(
        std::string_view normalized_path,
        bool allow_root = true) const;

    [[nodiscard]] Status create_file(std::string_view normalized_path,
                                     mode_t mode = S_IRUSR | S_IWUSR) const;
    [[nodiscard]] Status create_directory(
        std::string_view normalized_path,
        mode_t mode = S_IRWXU) const;
    [[nodiscard]] Status remove(std::string_view normalized_path,
                                bool recursive) const;
    [[nodiscard]] Status rename(std::string_view normalized_from,
                                std::string_view normalized_to) const;
    [[nodiscard]] Status truncate(std::string_view normalized_path,
                                  u64 length) const;
    [[nodiscard]] Status set_readable(std::string_view normalized_path,
                                      bool readable) const;
    [[nodiscard]] Status set_writable(std::string_view normalized_path,
                                      bool writable) const;

    [[nodiscard]] Result<std::vector<SandboxDirectoryEntry>> list(
        std::string_view normalized_directory) const;
    [[nodiscard]] Result<u64> directory_size(
        std::string_view normalized_directory,
        bool include_subdirectories) const;
    [[nodiscard]] Result<SandboxSpace> space() const;

    [[nodiscard]] Result<SandboxTemporaryFile> create_temporary(
        std::string_view safe_prefix) const;
    [[nodiscard]] Status atomic_write(
        std::string_view normalized_path,
        std::span<const u8> contents) const;

    [[nodiscard]] Result<std::string> lexical_host_path(
        std::string_view normalized_path,
        bool allow_root = true) const;

private:
    [[nodiscard]] Result<int> duplicate_root() const;
    [[nodiscard]] Status inject_fault(SandboxFaultPoint point) const;

    mutable std::mutex mutex_;
    std::string root_path_;
    int root_descriptor_ {-1};
    SandboxFaultInjector fault_injector_;
};

} // namespace phoneme::filesystem
