#pragma once

#include <array>
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
    [[nodiscard]] bool automatic_collection_due() const noexcept;
    [[nodiscard]] usize estimated_bytes() const noexcept;
    void prune(const std::function<bool(u64)>& is_live);
    void clear() noexcept;

private:
    static constexpr usize kLookupCacheSize = 16U;

    struct ImageLookupCacheEntry final {
        u64 key {0U};
        Image* value {nullptr};
    };
    struct ContextLookupCacheEntry final {
        u64 key {0U};
        GraphicsContext* value {nullptr};
    };

    [[nodiscard]] static constexpr usize lookup_cache_index(u64 key) noexcept {
        key ^= key >> 33U;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33U;
        return static_cast<usize>(key) & (kLookupCacheSize - 1U);
    }
    void invalidate_lookup_caches() const noexcept;

    std::unordered_map<u64, Image> images_;
    std::unordered_map<u64, GraphicsContext> contexts_;
    mutable std::array<ImageLookupCacheEntry, kLookupCacheSize> image_lookup_cache_ {};
    mutable std::array<ContextLookupCacheEntry, kLookupCacheSize> context_lookup_cache_ {};
    usize image_storage_bytes_ {0};
    usize image_allocation_bytes_since_prune_ {0};
};

} // namespace phoneme::graphics
