#pragma once

#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"

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
    [[nodiscard]] Result<std::vector<std::string>> list(
        std::string_view virtual_directory) const;

    [[nodiscard]] Result<std::string> host_path(
        std::string_view virtual_path) const;
private:
    struct Handle final {
        int descriptor {-1};
        bool readable {false};
        bool writable {false};
    };

    [[nodiscard]] Result<std::string> resolve(
        std::string_view virtual_path,
        bool allow_empty = false) const;
    [[nodiscard]] Result<i32> adopt_descriptor(int descriptor,
                                               bool readable,
                                               bool writable);

    mutable std::mutex mutex_;
    std::string sandbox_root_;
    std::string temporary_root_;
    std::unordered_map<i32, Handle> handles_;
    i32 next_handle_ {1};
    bool configured_ {false};
};

} // namespace phoneme::filesystem
