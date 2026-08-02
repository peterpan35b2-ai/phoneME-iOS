#include "phoneme/platform/MappedFile.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "phoneme/base/Checked.hpp"

namespace phoneme::platform {

MappedFile::~MappedFile() { reset(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : descriptor_(other.descriptor_), mapping_(other.mapping_), size_(other.size_) {
    other.descriptor_ = -1;
    other.mapping_ = nullptr;
    other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    descriptor_ = other.descriptor_;
    mapping_ = other.mapping_;
    size_ = other.size_;
    other.descriptor_ = -1;
    other.mapping_ = nullptr;
    other.size_ = 0;
    return *this;
}

Result<MappedFile> MappedFile::open_readonly(const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "open failed for " + path + ": " + std::strerror(errno));
    }

    struct stat status {};
    if (::fstat(descriptor, &status) != 0) {
        const std::string message = "fstat failed for " + path + ": " +
                                    std::strerror(errno);
        ::close(descriptor);
        return fail(ErrorCode::io_error, message);
    }

    if (status.st_size <= 0) {
        ::close(descriptor);
        return fail(ErrorCode::io_error, "file is empty: " + path);
    }

    auto size = checked_narrow<usize>(status.st_size);
    if (!size) {
        ::close(descriptor);
        return std::unexpected(size.error());
    }

    void* mapping = ::mmap(nullptr, *size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping == MAP_FAILED) {
        const std::string message = "mmap failed for " + path + ": " +
                                    std::strerror(errno);
        ::close(descriptor);
        return fail(ErrorCode::io_error, message);
    }

#if defined(MADV_SEQUENTIAL)
    (void)::madvise(mapping, *size, MADV_SEQUENTIAL);
#endif

    return MappedFile(descriptor, mapping, *size);
}

void MappedFile::reset() noexcept {
    if (mapping_ != nullptr && size_ != 0) {
        (void)::munmap(mapping_, size_);
    }
    if (descriptor_ >= 0) {
        (void)::close(descriptor_);
    }
    descriptor_ = -1;
    mapping_ = nullptr;
    size_ = 0;
}

} // namespace phoneme::platform
