#pragma once

namespace phoneme::vm::openjdk_fdlibm {

// Bit-reproducible subset of the fdlibm implementation used by OpenJDK 8
// StrictMath. These functions are intentionally separate from the platform
// libm so Java-visible results do not change across Apple OS releases.
[[nodiscard]] double asin(double value) noexcept;
[[nodiscard]] double acos(double value) noexcept;
[[nodiscard]] double atan(double value) noexcept;
[[nodiscard]] double exp(double value) noexcept;

} // namespace phoneme::vm::openjdk_fdlibm
