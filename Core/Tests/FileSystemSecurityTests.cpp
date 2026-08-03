#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/filesystem/ResourceLoader.hpp"
#include "phoneme/filesystem/SandboxResolver.hpp"

namespace {

using phoneme::ErrorCode;
using phoneme::filesystem::FileSystem;
using phoneme::filesystem::OpenMode;
using phoneme::filesystem::ResourceLoader;
using phoneme::filesystem::SandboxFaultPoint;
using phoneme::filesystem::SandboxResolver;
using phoneme::filesystem::SeekOrigin;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string read_host_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void write_host_file(const std::filesystem::path& path,
                     std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(output.good(), "write hostile-host fixture file");
}

int open_descriptor_count() {
    int count = 0;
    const int limit = ::getdtablesize();
    for (int descriptor = 0; descriptor < limit; ++descriptor) {
        if (::fcntl(descriptor, F_GETFD) >= 0 || errno != EBADF) ++count;
    }
    return count;
}

std::vector<phoneme::u8> bytes(std::string_view text) {
    return std::vector<phoneme::u8>(text.begin(), text.end());
}

std::string read_runtime_file(FileSystem& files, std::string_view path) {
    auto handle = files.open(path, OpenMode::read, false, false);
    require(handle.has_value(), "open runtime file for verification");
    auto size = files.size(*handle);
    require(size.has_value() && *size >= 0, "read runtime file size");
    std::vector<phoneme::u8> contents(static_cast<std::size_t>(*size));
    auto count = files.read(*handle, contents);
    require(count.has_value() && *count == contents.size(),
            "read complete runtime file");
    require(files.close(*handle).has_value(), "close runtime verification file");
    return std::string(contents.begin(), contents.end());
}

void test_path_normalization() {
    require(!phoneme::filesystem::normalize_virtual_path("/absolute").has_value(),
            "reject absolute virtual paths");
    require(!phoneme::filesystem::normalize_virtual_path("C:/absolute").has_value(),
            "reject drive-qualified virtual paths");
    require(!phoneme::filesystem::normalize_virtual_path("a/../b").has_value(),
            "reject parent components in virtual paths");
    const std::string nul_path("safe\0escape", 11);
    require(!phoneme::filesystem::normalize_virtual_path(nul_path).has_value(),
            "reject embedded NUL bytes");
    require(!phoneme::filesystem::path_from_file_url(
                 "file:///%2e%2e/outside")
                 .has_value(),
            "reject encoded traversal in file URLs");
    auto normalized = phoneme::filesystem::path_from_file_url(
        "file://localhost/save/%E2%98%83.bin");
    require(normalized.has_value() && *normalized == "save/☃.bin",
            "decode UTF-8 file URL paths within the virtual root");
    auto root = phoneme::filesystem::path_from_file_url("file:///");
    require(root.has_value() && root->empty(),
            "allow a FileConnection to the suite root");
}

void test_resources(const std::string& archive_path) {
    const std::array<std::string, 1> archives {archive_path};
    auto relative = ResourceLoader::read_for_class(
        archives, "pkg/Fixture", "data.bin");
    require(relative.has_value() &&
                std::string(relative->begin(), relative->end()) == "relative-data",
            "read package-relative resource");
    auto absolute = ResourceLoader::read_for_class(
        archives, "other/Fixture", "/pkg/data.bin");
    require(absolute.has_value() && *absolute == *relative,
            "read absolute resource");
    auto unicode = ResourceLoader::read_for_class(
        archives, "pkg/Fixture", "tên-☃.bin");
    require(unicode.has_value() &&
                std::string(unicode->begin(), unicode->end()) == "unicode-data",
            "read Unicode resource name");
    auto traversal = ResourceLoader::resolve_class_resource(
        "pkg/Fixture", "../../escape.bin");
    require(!traversal.has_value() &&
                traversal.error().code == ErrorCode::invalid_argument,
            "reject resource traversal above archive root");
}

void test_filesystem(const std::filesystem::path& base) {
    const auto root = base / "suite-root";
    const auto temporary = base / "app-temporary";
    const auto outside = base / "outside";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);

    FileSystem files;
    require(files.configure(root.string(), temporary.string()).has_value(),
            "configure secure filesystem");

    write_host_file(outside / "sentinel.bin", "outside-sentinel");
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, root / "escape", error);
    require(!error, "create hostile symlink fixture");
    auto escaped = files.open("escape/sentinel.bin", OpenMode::read,
                              false, false);
    require(!escaped.has_value() &&
                escaped.error().code == ErrorCode::invalid_argument,
            "block intermediate symlink escape");
    auto symlink_stat = files.stat("escape");
    require(!symlink_stat.has_value() &&
                symlink_stat.error().code == ErrorCode::invalid_argument,
            "block leaf symlink inspection");

    require(files.create_directory("save").has_value(),
            "create sandbox directory");
    auto writer = files.open("save/data.bin", OpenMode::read_write,
                             true, true);
    require(writer.has_value(), "create sandbox file through openat");
    const auto original = bytes("abcdefgh");
    require(files.write(*writer, original).value_or(0) == original.size(),
            "write initial sandbox payload");
    require(files.flush(*writer).has_value(), "flush initial sandbox payload");

    auto reader = files.open("save/data.bin", OpenMode::read, false, false);
    require(reader.has_value(), "open a second handle to the same file");
    std::array<phoneme::u8, 4> prefix {};
    require(files.read(*reader, prefix).value_or(0) == prefix.size() &&
                std::string(prefix.begin(), prefix.end()) == "abcd",
            "read from independent runtime handle");
    require(files.truncate("save/data.bin", 6).has_value(),
            "truncate with an open reader and writer");
    require(files.size(*reader).value_or(-1) == 6,
            "open handle observes truncation");
    require(files.close(*reader).has_value(), "close second handle");

    require(files.rename("save/data.bin", "save/renamed.bin").has_value(),
            "rename while stream handle remains open");
    require(files.seek(*writer, 0, SeekOrigin::end).has_value(),
            "seek renamed open handle");
    const auto suffix = bytes("Z");
    require(files.write(*writer, suffix).value_or(0) == 1,
            "write through handle after rename");
    require(files.close(*writer).has_value(), "close renamed writer");
    require(read_runtime_file(files, "save/renamed.bin") == "abcdefZ",
            "renamed file preserves open-handle writes");

    auto deleted_reader = files.open("save/renamed.bin", OpenMode::read,
                                     false, false);
    require(deleted_reader.has_value(), "open file before unlink policy test");
    require(files.remove("save/renamed.bin").has_value(),
            "unlink file while stream remains open");
    std::array<phoneme::u8, 7> deleted_contents {};
    require(files.read(*deleted_reader, deleted_contents).value_or(0) ==
                deleted_contents.size(),
            "open stream remains valid after unlink");
    require(files.close(*deleted_reader).has_value(),
            "close stream after unlink");
    require(!files.stat("save/renamed.bin").value_or(
                 phoneme::filesystem::FileInfo {}).exists,
            "unlinked name no longer exists");

    require(files.create_file("save/permissions.bin").has_value(),
            "create permission fixture");
    require(files.set_writable("save/permissions.bin", false).has_value(),
            "clear writable capability");
    auto permission_info = files.stat("save/permissions.bin");
    require(permission_info.has_value() && !permission_info->writable,
            "report cleared writable capability");
    require(!files.open("save/permissions.bin", OpenMode::write,
                        false, false)
                 .has_value(),
            "deny write open after writable capability is cleared");
    require(files.set_writable("save/permissions.bin", true).has_value(),
            "restore writable capability");
    require(files.set_readable("save/permissions.bin", false).has_value(),
            "clear readable capability");
    permission_info = files.stat("save/permissions.bin");
    require(permission_info.has_value() && !permission_info->readable,
            "report cleared readable capability");
    require(!files.open("save/permissions.bin", OpenMode::read,
                        false, false)
                 .has_value(),
            "deny read open after readable capability is cleared");
    require(files.set_readable("save/permissions.bin", true).has_value(),
            "restore readable capability");

    auto large = files.open("save/large-sparse.bin", OpenMode::read_write,
                            true, true);
    require(large.has_value(), "create large sparse file fixture");
    constexpr phoneme::i64 large_offset =
        static_cast<phoneme::i64>(3) * 1024 * 1024 * 1024;
    require(files.seek(*large, large_offset, SeekOrigin::begin).has_value(),
            "seek beyond signed 32-bit file offset");
    const auto marker = bytes("L");
    require(files.write(*large, marker).value_or(0) == 1,
            "extend sparse file beyond signed 32-bit offset");
    require(files.size(*large).value_or(0) == large_offset + 1,
            "preserve 64-bit sparse file size");
    require(files.close(*large).has_value(),
            "close large sparse file fixture");

    const auto first = bytes("first-atomic-version");
    const auto second = bytes("second-atomic-version");
    require(files.atomic_write("save/atomic.bin", first).has_value(),
            "install first atomic save");
    require(files.atomic_write("save/atomic.bin", second).has_value(),
            "replace atomic save");
    require(read_runtime_file(files, "save/atomic.bin") ==
                "second-atomic-version",
            "atomic save exposes only complete replacement");
    require(files.set_writable("save/atomic.bin", false).has_value(),
            "clear atomic destination writable flag");
    require(files.atomic_write("save/atomic.bin", bytes("mode-preserved"))
                .has_value(),
            "atomically replace a read-only destination");
    auto atomic_info = files.stat("save/atomic.bin");
    require(atomic_info.has_value() && !atomic_info->writable &&
                read_runtime_file(files, "save/atomic.bin") ==
                    "mode-preserved",
            "atomic replacement preserves destination permissions");
    require(files.set_writable("save/atomic.bin", true).has_value(),
            "restore atomic destination writable flag");

    SandboxResolver atomic_resolver;
    require(atomic_resolver.configure(root.string()).has_value(),
            "configure atomic rollback resolver");
    require(atomic_resolver.atomic_write(
                "save/rollback.bin", bytes("rollback-old"))
                .has_value(),
            "create atomic rollback baseline");
    atomic_resolver.set_fault_injector(
        [](SandboxFaultPoint point) -> phoneme::Status {
            if (point == SandboxFaultPoint::atomic_install_sync) {
                return phoneme::fail(
                    ErrorCode::io_error,
                    "injected post-rename directory sync failure");
            }
            return {};
        });
    auto rollback_result = atomic_resolver.atomic_write(
        "save/rollback.bin", bytes("rollback-new"));
    require(!rollback_result.has_value(),
            "post-rename sync failure is reported");
    require(read_host_file(root / "save/rollback.bin") == "rollback-old",
            "failed atomic replacement restores previous contents");

    auto absent_result = atomic_resolver.atomic_write(
        "save/rollback-absent.bin", bytes("must-not-appear"));
    require(!absent_result.has_value() &&
                !std::filesystem::exists(root / "save/rollback-absent.bin"),
            "failed atomic creation removes newly installed destination");
    atomic_resolver.clear_fault_injector();
    require(atomic_resolver.atomic_write(
                "save/rollback.bin", bytes("rollback-new"))
                .has_value() &&
                read_host_file(root / "save/rollback.bin") == "rollback-new",
            "atomic replacement succeeds after fault is cleared");

    require(files.atomic_write("save/source.bin", bytes("source")).has_value(),
            "create rename collision source");
    require(files.atomic_write("save/destination.bin", bytes("destination"))
                .has_value(),
            "create rename collision destination");
    require(!files.rename("save/source.bin", "save/destination.bin")
                 .has_value(),
            "rename refuses to replace an existing destination");
    require(read_runtime_file(files, "save/source.bin") == "source" &&
                read_runtime_file(files, "save/destination.bin") ==
                    "destination",
            "failed rename preserves both files");

    require(files.create_directory("save/sub").has_value(),
            "create directory-size child");
    require(files.atomic_write("save/sub/value.bin", bytes("12345")).has_value(),
            "write directory-size child");
    auto shallow_size = files.directory_size("save", false);
    auto recursive_size = files.directory_size("save", true);
    require(shallow_size.has_value() && recursive_size.has_value() &&
                *recursive_size >= *shallow_size + 5,
            "directorySize optionally includes descendants");
    auto storage = files.storage_info();
    require(storage.has_value() && storage->total > 0 &&
                storage->available <= storage->total &&
                storage->used <= storage->total,
            "report available total and used storage");
    require(!files.truncate("save/atomic.bin",
                            std::numeric_limits<phoneme::u64>::max())
                 .has_value(),
            "reject truncate offset overflow");

    require(files.create_file("save/.hidden").has_value(),
            "create hidden listing fixture");
    std::filesystem::create_symlink(outside / "sentinel.bin",
                                    root / "save" / "hidden-link", error);
    require(!error, "create listed hostile symlink");
    auto names = files.list("save");
    require(names.has_value() &&
                std::find(names->begin(), names->end(), "hidden-link") ==
                    names->end(),
            "directory listing does not expose symlink entries");

    const int descriptors_before = open_descriptor_count();
    for (int iteration = 0; iteration < 512; ++iteration) {
        auto handle = files.open("save/atomic.bin", OpenMode::read,
                                 false, false);
        require(handle.has_value(), "open descriptor leak stress handle");
        std::array<phoneme::u8, 1> byte {};
        require(files.read(*handle, byte).has_value(),
                "read descriptor leak stress handle");
        require(files.close(*handle).has_value(),
                "close descriptor leak stress handle");
    }
    const int descriptors_after = open_descriptor_count();
    require(descriptors_after <= descriptors_before + 1,
            "stress operations do not leak file descriptors");

    require(files.create_directory("race").has_value(),
            "create rename-race directory");
    write_host_file(outside / "target.bin", "outside-race-sentinel");
    std::atomic_bool stop {false};
    std::thread attacker([&] {
        const auto active = root / "race";
        const auto parked = root / "race-parked";
        while (!stop.load(std::memory_order_relaxed)) {
            std::error_code ignored;
            std::filesystem::rename(active, parked, ignored);
            if (ignored) continue;
            std::filesystem::create_directory_symlink(outside, active, ignored);
            if (!ignored) std::filesystem::remove(active, ignored);
            std::filesystem::rename(parked, active, ignored);
        }
    });
    for (int iteration = 0; iteration < 1500; ++iteration) {
        auto handle = files.open("race/target.bin", OpenMode::write,
                                 true, true);
        if (!handle) continue;
        const auto payload = bytes("inside");
        static_cast<void>(files.write(*handle, payload));
        static_cast<void>(files.close(*handle));
    }
    stop.store(true, std::memory_order_relaxed);
    attacker.join();
    require(read_host_file(outside / "target.bin") ==
                "outside-race-sentinel",
            "rename/path replacement race cannot escape sandbox");

    auto temporary_file = files.create_temporary("background I/O");
    require(temporary_file.has_value() &&
                std::filesystem::exists(temporary_file->host_path),
            "create runtime-owned temporary file");
    require(files.close(temporary_file->handle).has_value(),
            "close runtime-owned temporary file");
}

void test_suite_isolation(const std::filesystem::path& base) {
    FileSystem overlapping;
    require(!overlapping.configure(
                 (base / "overlap-root").string(),
                 (base / "overlap-root" / "temporary").string())
                 .has_value(),
            "reject overlapping persistent and temporary roots");

    FileSystem first;
    FileSystem second;
    require(first.configure((base / "suite-a").string(),
                            (base / "temporary-a").string())
                .has_value(),
            "configure first isolated suite");
    require(second.configure((base / "suite-b").string(),
                             (base / "temporary-b").string())
                .has_value(),
            "configure second isolated suite");
    require(first.atomic_write("shared.bin", bytes("suite-a")).has_value(),
            "write first isolated suite file");
    require(second.atomic_write("shared.bin", bytes("suite-b")).has_value(),
            "write second isolated suite file");
    require(read_runtime_file(first, "shared.bin") == "suite-a" &&
                read_runtime_file(second, "shared.bin") == "suite-b",
            "identical virtual paths remain isolated by suite root");
}

void test_temporary_cleanup(const std::filesystem::path& base) {
    const auto root = base / "cleanup-suite";
    const auto temporary = base / "cleanup-temporary";
    std::string temporary_path;
    {
        FileSystem files;
        require(files.configure(root.string(), temporary.string()).has_value(),
                "configure temporary cleanup fixture");
        auto file = files.create_temporary("cleanup");
        require(file.has_value(), "create cleanup temporary file");
        temporary_path = file->host_path;
        require(files.close(file->handle).has_value(),
                "close cleanup temporary file");
        require(std::filesystem::exists(temporary_path),
                "temporary file exists before app destroy");
    }
    require(!std::filesystem::exists(temporary),
            "temporary root is removed when app filesystem is destroyed");
    require(std::filesystem::exists(root),
            "persistent suite root survives app filesystem destruction");
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 3, "usage: FileSystemSecurityTests <root> <resource-jar>");
    const std::filesystem::path base(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(base, error);
    error.clear();
    std::filesystem::create_directories(base, error);
    require(!error, "create filesystem test root");

    test_path_normalization();
    test_resources(argv[2]);
    test_filesystem(base);
    test_suite_isolation(base);
    test_temporary_cleanup(base);

    std::filesystem::remove_all(base, error);
    require(!error, "remove filesystem test root");
    std::cout << "FileSystemSecurityTests: PASS\n";
    return 0;
}
