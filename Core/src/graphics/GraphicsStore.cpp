#include "phoneme/graphics/GraphicsStore.hpp"

#include <utility>

namespace phoneme::graphics {

Status GraphicsStore::attach_image(u64 object_key, Image image_value) {
    if (object_key == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "cannot attach an image to a null object key");
    }
    images_.insert_or_assign(object_key, std::move(image_value));
    return {};
}

Status GraphicsStore::attach_context(u64 object_key,
                                     u64 target_image_key) {
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
}

void GraphicsStore::clear() noexcept {
    contexts_.clear();
    images_.clear();
}

} // namespace phoneme::graphics
