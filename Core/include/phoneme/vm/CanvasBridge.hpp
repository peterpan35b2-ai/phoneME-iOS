#pragma once

#include <string>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/base/Types.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

struct CanvasRect final {
    i32 x {0};
    i32 y {0};
    i32 width {0};
    i32 height {0};
};

class CanvasBridge {
public:
    virtual ~CanvasBridge() = default;

    [[nodiscard]] virtual Status register_canvas(ObjectRef canvas,
                                                 bool game_canvas,
                                                 bool suppress_key_events) = 0;
    [[nodiscard]] virtual Status set_display_visible(ObjectRef displayable,
                                                     bool visible) = 0;
    [[nodiscard]] virtual Status flush_visibility_callbacks() = 0;
    [[nodiscard]] virtual Status request_repaint(ObjectRef canvas,
                                                 CanvasRect region) = 0;
    [[nodiscard]] virtual Status request_service_repaints(
        ObjectRef canvas) = 0;
    [[nodiscard]] virtual Status pump_blocking_wait_work() = 0;
    [[nodiscard]] virtual Status set_fullscreen(ObjectRef canvas,
                                                bool fullscreen) = 0;
    [[nodiscard]] virtual Result<Dimensions> canvas_dimensions(
        ObjectRef canvas) const = 0;
    [[nodiscard]] virtual Dimensions display_dimensions() const noexcept = 0;
    [[nodiscard]] virtual bool pointer_events_supported() const noexcept = 0;
    [[nodiscard]] virtual bool pointer_motion_supported() const noexcept = 0;
    [[nodiscard]] virtual bool repeat_events_supported() const noexcept = 0;
    [[nodiscard]] virtual i32 game_action_for_key(i32 key_code) const noexcept = 0;
    [[nodiscard]] virtual Result<i32> key_code_for_action(
        i32 game_action) const = 0;
    [[nodiscard]] virtual std::string key_name(i32 key_code) const = 0;
    [[nodiscard]] virtual i32 game_key_states(ObjectRef canvas) const noexcept = 0;
    [[nodiscard]] virtual Result<ObjectRef> game_graphics(ObjectRef canvas) = 0;
    [[nodiscard]] virtual Status request_game_flush(ObjectRef canvas,
                                                    CanvasRect region) = 0;
    virtual void append_reference_roots(
        std::vector<ObjectRef>& roots) const = 0;
};

} // namespace phoneme::vm
