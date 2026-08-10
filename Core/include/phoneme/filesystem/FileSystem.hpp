#pragma once

#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/filesystem/SandboxResolver.hpp"

namespace phoneme::filesystem {

enum class OpenMode : u8 {
    read = 1,
    write = 2,
    read_write = 3,
    append = 4,
};

enum class SeekOrigin : u8 {
    begin,
    current,
    end,
};

struct FileInfo final {
    bool exists {false};
    bool directory {false};
    bool readable {false};
    bool writable {false};
    bool hidden {false};
    u64 size {0};
    i64 modified_seconds {0};
};

struct StorageInfo final {
    u64 available {0};
    u64 total {0};
    u64 used {0};
};

struct TemporaryFile final {
    i32 handle {0};
    std::string host_path;
};

[[nodiscard]] Result<std::string> normalize_virtual_path(
    std::string_view path,
    bool allow_empty = false);
[[nodiscard]] Result<std::string> normalize_resource_path(
    std::string_view path);
[[nodiscard]] Result<std::string> path_from_file_url(
    std::string_view url);

class FileSystem final {
public:
    FileSystem() = default;
    ~FileSystem();

    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    [[nodiscard]] Status configure(std::string sandbox_root,
                                   std::string temporary_root);
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] u64 mutation_generation() const noexcept {
        return mutation_generation_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] Result<i32> open(std::string_view virtual_path,
                                   OpenMode mode,
                                   bool create,
                                   bool truncate);
    [[nodiscard]] Result<usize> read(i32 handle, std::span<u8> destination);
    [[nodiscard]] Result<usize> write(i32 handle,
                                      std::span<const u8> source);
    [[nodiscard]] Result<i64> seek(i32 handle,
                                   i64 offset,
                                   SeekOrigin origin);
    [[nodiscard]] Result<i64> position(i32 handle);
    [[nodiscard]] Result<i64> size(i32 handle);
    [[nodiscard]] Result<i64> available(i32 handle);
    [[nodiscard]] Status flush(i32 handle);
    [[nodiscard]] Status close(i32 handle);
    void close_all() noexcept;

    [[nodiscard]] Result<TemporaryFile> create_temporary(
        std::string_view prefix);

    [[nodiscard]] Result<FileInfo> stat(std::string_view virtual_path) const;
    [[nodiscard]] Status create_file(std::string_view virtual_path);
    [[nodiscard]] Status create_directory(std::string_view virtual_path);
    [[nodiscard]] Status remove(std::string_view virtual_path,
                                bool recursive = false);
    [[nodiscard]] Status rename(std::string_view from,
                                std::string_view to);
    [[nodiscard]] Status truncate(std::string_view virtual_path,
                                  u64 length);
    [[nodiscard]] Status set_readable(std::string_view virtual_path,
                                      bool readable);
    [[nodiscard]] Status set_writable(std::string_view virtual_path,
                                      bool writable);
    [[nodiscard]] Result<std::vector<std::string>> list(
        std::string_view virtual_directory) const;
    [[nodiscard]] Result<u64> directory_size(
        std::string_view virtual_directory,
        bool include_subdirectories) const;
    [[nodiscard]] Result<StorageInfo> storage_info() const;
    [[nodiscard]] Result<u64> available_size() const;
    [[nodiscard]] Result<u64> total_size() const;
    [[nodiscard]] Result<u64> used_size() const;

    [[nodiscard]] Status atomic_write(
        std::string_view virtual_path,
        std::span<const u8> contents);

    [[nodiscard]] Result<std::string> host_path(
        std::string_view virtual_path) const;

private:
    struct Handle final {
        int descriptor {-1};
        bool readable {false};
        bool writable {false};
    };

    [[nodiscard]] Result<i32> adopt_descriptor(int descriptor,
                                               bool readable,
                                               bool writable);
    [[nodiscard]] Result<int> duplicate_descriptor(i32 handle,
                                                   bool require_read,
                                                   bool require_write) const;
    void cleanup_temporary_root() noexcept;

    mutable std::mutex mutex_;
    SandboxResolver sandbox_;
    SandboxResolver temporary_;
    std::string sandbox_root_;
    std::string temporary_root_;
    std::unordered_map<i32, Handle> handles_;
    i32 next_handle_ {1};
    bool configured_ {false};
    std::atomic<u64> mutation_generation_ {0};
};

} // namespace phoneme::filesystem
