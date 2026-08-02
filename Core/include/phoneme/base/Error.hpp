#pragma once

#include <expected>
#include <string>
#include <utility>

#include "phoneme/base/Types.hpp"

namespace phoneme {

enum class ErrorCode : i32 {
    none = 0,
    invalid_argument,
    out_of_range,
    overflow,
    io_error,
    malformed_archive,
    unsupported_archive,
    checksum_mismatch,
    malformed_class,
    verification_failed,
    unsupported_class_version,
    class_not_found,
    method_not_found,
    java_exception,
    invalid_state,
    already_running,
    not_configured,
    not_running,
    unsupported_feature,
    internal_error,
};

struct Error final {
    ErrorCode code {ErrorCode::none};
    std::string message;
    std::string java_exception_class;

    [[nodiscard]] static Error make(ErrorCode code, std::string message) {
        return Error {code, std::move(message), {}};
    }

    [[nodiscard]] static Error make_java(std::string class_name,
                                         std::string message) {
        return Error {ErrorCode::java_exception,
                      std::move(message),
                      std::move(class_name)};
    }
};

template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code,
                                                 std::string message) {
    return std::unexpected(Error::make(code, std::move(message)));
}

[[nodiscard]] inline std::unexpected<Error> fail_java(
    std::string class_name,
    std::string message) {
    return std::unexpected(Error::make_java(std::move(class_name),
                                            std::move(message)));
}

} // namespace phoneme
