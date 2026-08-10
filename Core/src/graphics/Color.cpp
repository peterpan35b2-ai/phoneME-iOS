#include "phoneme/graphics/Color.hpp"

namespace phoneme::graphics {
// Pixel compositing hot paths are header-inlined in Color.hpp so the
// WebAssembly interpreter renderer does not pay a cross-TU call per pixel.
} // namespace phoneme::graphics
