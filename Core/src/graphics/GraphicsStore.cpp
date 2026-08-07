#include "phoneme/graphics/GraphicsStore.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace phoneme::graphics {
namespace {

constexpr usize kGraphicsGcAllocationInterval = 16U * 1024U * 1024U;

[[nodiscard]] usize image_storage_bytes(const Image& image) noexcept {
    const usize pixel_count = image.pixels().size();
    if (pixel_count > std::numeric_limits<usize>::max() / sizeof(Pixel)) {
        return std::numeric_limits<usize>::max();
    }
    return pixel_count * sizeof(Pixel);
}

[[nodiscard]] usize saturated_add(usize left, usize right) noexcept {
    if (right > std::numeric_limits<usize>::max() - left) {
        return std::numeric_limits<usize>::max();
    }
    return left + right;
}

} // namespace

Status GraphicsStore::attach_image(u64 object_key, Image image_value) {
    if (object_key == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "cannot attach an image to a null object key");
    }
    image_allocation_bytes_since_prune_ = saturated_add(
        image_allocation_bytes_since_prune_, image_storage_bytes(image_value));
    images_.insert_or_assign(object_key, std::move(image_value));
    return {};
}

Status GraphicsStore::attach_context(u64 object_key,
                                     u64 target_image_key,
                                     bool display_target) {
    if (object_key == 0U || target_image_key == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "graphics context keys must be non-zero");
    }
    auto target = image(target_image_key);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (!(*target)->is_mutable()) {
        return fail(ErrorCode::invalid_state,
                    "graphics contexts require a mutable target image");
    }
    GraphicsContext context;
    context.target_key = target_image_key;
    context.display_target = display_target;
    context.clip = target_bounds(**target);
    contexts_.insert_or_assign(object_key, context);
    return {};
}

Result<Image*> GraphicsStore::image(u64 object_key) {
    auto iterator = images_.find(object_key);
    if (iterator == images_.end()) {
        return fail(ErrorCode::invalid_state,
                    "Java Image has no native graphics payload");
    }
    return &iterator->second;
}

Result<const Image*> GraphicsStore::image(u64 object_key) const {
    auto iterator = images_.find(object_key);
    if (iterator == images_.end()) {
        return fail(ErrorCode::invalid_state,
                    "Java Image has no native graphics payload");
    }
    return &iterator->second;
}

Result<GraphicsContext*> GraphicsStore::context(u64 object_key) {
    auto iterator = contexts_.find(object_key);
    if (iterator == contexts_.end()) {
        return fail(ErrorCode::invalid_state,
                    "Java Graphics has no native graphics context");
    }
    return &iterator->second;
}

Result<const GraphicsContext*> GraphicsStore::context(u64 object_key) const {
    auto iterator = contexts_.find(object_key);
    if (iterator == contexts_.end()) {
        return fail(ErrorCode::invalid_state,
                    "Java Graphics has no native graphics context");
    }
    return &iterator->second;
}

Result<std::optional<DirtyImageUpdate>> GraphicsStore::consume_dirty_update(
    u64 image_object_key) {
    auto payload = image(image_object_key);
    if (!payload) return std::unexpected(payload.error());
    Image& source = **payload;
    if (!source.has_dirty_region()) {
        return std::optional<DirtyImageUpdate> {};
    }

    const ImageRegion region = source.dirty_region();
    if (region.width <= 0 || region.height <= 0 || region.x < 0 ||
        region.y < 0 ||
        static_cast<i64>(region.x) + region.width > source.width() ||
        static_cast<i64>(region.y) + region.height > source.height()) {
        return fail(ErrorCode::internal_error,
                    "image dirty region is outside its pixel storage");
    }
    auto count = validated_pixel_count(region.width, region.height);
    if (!count) return std::unexpected(count.error());

    DirtyImageUpdate update;
    update.image_width = source.width();
    update.image_height = source.height();
    update.region = region;
    update.pixels.resize(*count);
    for (i32 row = 0; row < region.height; ++row) {
        const usize source_offset =
            static_cast<usize>(region.y + row) *
                static_cast<usize>(source.width()) +
            static_cast<usize>(region.x);
        const usize destination_offset =
            static_cast<usize>(row) * static_cast<usize>(region.width);
        std::copy_n(source.pixels().begin() +
                        static_cast<std::ptrdiff_t>(source_offset),
                    static_cast<usize>(region.width),
                    update.pixels.begin() +
                        static_cast<std::ptrdiff_t>(destination_offset));
    }
    source.clear_dirty_region();
    return std::optional<DirtyImageUpdate>(std::move(update));
}

void GraphicsStore::erase_image(u64 object_key) noexcept {
    images_.erase(object_key);
    for (auto iterator = contexts_.begin(); iterator != contexts_.end();) {
        if (iterator->second.target_key == object_key) {
            iterator = contexts_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void GraphicsStore::erase_context(u64 object_key) noexcept {
    contexts_.erase(object_key);
}

bool GraphicsStore::automatic_collection_due() const noexcept {
    return image_allocation_bytes_since_prune_ >= kGraphicsGcAllocationInterval;
}

void GraphicsStore::prune(const std::function<bool(u64)>& is_live) {
    for (auto iterator = images_.begin(); iterator != images_.end();) {
        if (!is_live(iterator->first)) {
            iterator = images_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (auto iterator = contexts_.begin(); iterator != contexts_.end();) {
        if (!is_live(iterator->first) ||
            images_.find(iterator->second.target_key) == images_.end()) {
            iterator = contexts_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    image_allocation_bytes_since_prune_ = 0U;
}

void GraphicsStore::clear() noexcept {
    contexts_.clear();
    images_.clear();
    image_allocation_bytes_since_prune_ = 0U;
}

} // namespace phoneme::graphics
