#include <array>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include <unistd.h>

#include "phoneme/runtime/RecordStoreRegistry.hpp"

namespace {

using phoneme::runtime::RecordStoreFaultPoint;

[[nodiscard]] bool matches(std::string_view checkpoint,
                           RecordStoreFaultPoint point) {
    if (checkpoint == "after-file-sync") {
        return point == RecordStoreFaultPoint::after_file_sync;
    }
    if (checkpoint == "after-backup-link") {
        return point == RecordStoreFaultPoint::after_backup_link;
    }
    if (checkpoint == "after-rename") {
        return point == RecordStoreFaultPoint::after_rename;
    }
    if (checkpoint == "after-directory-sync") {
        return point == RecordStoreFaultPoint::after_directory_sync;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: RmsCrashHarness <root> <checkpoint>\n";
        return 64;
    }

    const std::string_view checkpoint(argv[2]);
    phoneme::runtime::RecordStoreRegistry registry;
    auto configured = registry.configure(argv[1]);
    if (!configured) {
        std::cerr << configured.error().message << '\n';
        return 65;
    }
    auto opened = registry.open("crash-store", false);
    if (!opened) {
        std::cerr << opened.error().message << '\n';
        return 66;
    }

    registry.set_fault_injector(
        [checkpoint](RecordStoreFaultPoint point) -> phoneme::Status {
            if (matches(checkpoint, point)) {
                (void)::kill(::getpid(), SIGKILL);
                ::_exit(99);
            }
            return {};
        });

    constexpr std::array<phoneme::u8, 1> record {9U};
    auto added = registry.add_record("crash-store", record);
    if (!added) {
        std::cerr << added.error().message << '\n';
        return 67;
    }
    std::cerr << "checkpoint was not reached\n";
    return 68;
}
