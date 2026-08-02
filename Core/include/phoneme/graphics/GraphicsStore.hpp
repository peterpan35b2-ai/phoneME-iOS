#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "phoneme/graphics/Graphics.hpp"

namespace phoneme::graphics {

struct DirtyImageUpdate final {
    i32 image_width {0};
    i32 image_height {0};
    ImageRegion region {};
    std::vector<Pixel> pixels;
};

class GraphicsStore final {
public:
    [[nodiscard]] Status attach_image(u64 object_key, Image image);
    [[nodiscard]] Status attach_context(u64 object_key,
                                        u64 target_image_key,
                                        bool display_target = false);

    [[nodiscard]] Result<Image*> image(u64 object_key);
    [[nodiscard]] Result<const Image*> image(u64 object_key) const;
    [[nodiscard]] Result<GraphicsContext*> context(u64 object_key);
    [[nodiscard]] Result<const GraphicsContext*> context(
        u64 object_key) const;
    [[nodiscard]] Result<std::optional<DirtyImageUpdate>>
    consume_dirty_update(u64 image_object_key);

    void erase_image(u64 object_key) noexcept;
    void erase_context(u64 object_key) noexcept;
    void prune(const std::function<bool(u64)>& is_live);
    void clear() noexcept;

private:
    std::unordered_map<u64, Image> images_;
    std::unordered_map<u64, GraphicsContext> contexts_;
};

} // namespace phoneme::graphics
