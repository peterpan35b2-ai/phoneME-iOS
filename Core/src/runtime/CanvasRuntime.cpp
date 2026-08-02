#include "phoneme/runtime/CanvasRuntime.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::runtime {
namespace {

constexpr i32 kActionUp = 1;
constexpr i32 kActionLeft = 2;
constexpr i32 kActionRight = 5;
constexpr i32 kActionDown = 6;
constexpr i32 kActionFire = 8;
constexpr i32 kActionGameA = 9;
constexpr i32 kActionGameB = 10;
constexpr i32 kActionGameC = 11;
constexpr i32 kActionGameD = 12;
constexpr i32 kScreenUpdated = 3;
constexpr i32 kCanvasComponentType = 22;
constexpr i32 kScreenModeMetadata = -1007;
constexpr usize kDisplayableIdField = 0;

[[nodiscard]] i32 saturating_i32(i64 value) noexcept {
    return static_cast<i32>(std::clamp<i64>(
        value,
        static_cast<i64>(std::numeric_limits<i32>::min()),
        static_cast<i64>(std::numeric_limits<i32>::max())));
}

} // namespace

CanvasRuntime::CanvasRuntime(vm::Machine& machine,
                             Dimensions dimensions,
                             std::array<i32, 7> keymap)
    : machine_(machine), dimensions_(dimensions), keymap_(keymap) {}

void CanvasRuntime::configure_render_hooks(CanvasRenderHooks hooks) {
    render_hooks_ = std::move(hooks);
}

void CanvasRuntime::set_keymap(std::array<i32, 7> keymap) noexcept {
    keymap_ = keymap;
}

Status CanvasRuntime::set_dimensions(Dimensions dimensions) {
    if (!dimensions.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "Canvas dimensions must be positive");
    }
    if (dimensions.width == dimensions_.width &&
        dimensions.height == dimensions_.height) {
        return {};
    }
    dimensions_ = dimensions;
    size_change_pending_ = true;
    for (auto& [key, state] : canvases_) {
        (void)key;
        state.game_graphics = {};
        merge_region(state.repaint_region, full_region());
    }
    return {};
}

Status CanvasRuntime::set_host_foreground(bool foreground) {
    if (host_foreground_ == foreground) {
        return {};
    }
    host_foreground_ = foreground;
    for (const auto& [key, state] : canvases_) {
        (void)key;
        visibility_changes_.push_back(VisibilityChange {
            .object = state.object,
            .visible = state.display_visible,
        });
    }
    return {};
}

void CanvasRuntime::enqueue_key(i32 key_code, bool pressed, u64 sequence) {
    inputs_.push_back(PendingInput {
        .kind = InputKind::key,
        .first = key_code,
        .second = pressed ? 1 : 0,
        .sequence = sequence,
    });
}

void CanvasRuntime::enqueue_pointer(i32 x,
                                    i32 y,
                                    i32 action,
                                    u64 sequence) {
    inputs_.push_back(PendingInput {
        .kind = InputKind::pointer,
        .first = x,
        .second = y,
        .third = action,
        .sequence = sequence,
    });
}

Status CanvasRuntime::pump() {
    if (pumping_) {
        return {};
    }
    pumping_ = true;
    struct PumpGuard final {
        bool& flag;
        ~PumpGuard() { flag = false; }
    } guard {pumping_};

    auto visibility = process_visibility_changes();
    if (!visibility) return visibility;
    auto sizes = process_size_changes();
    if (!sizes) return sizes;
    auto inputs = process_inputs();
    if (!inputs) return inputs;
    auto visibility_after_input = process_visibility_changes();
    if (!visibility_after_input) return visibility_after_input;
    auto flushes = process_flushes();
    if (!flushes) return flushes;
    return process_repaints();
}

usize CanvasRuntime::estimated_bytes() const noexcept {
    usize total = sizeof(*this);
    total += canvases_.size() * sizeof(CanvasState);
    total += inputs_.size() * sizeof(PendingInput);
    total += visibility_changes_.size() * sizeof(VisibilityChange);
    for (const auto& [key, state] : canvases_) {
        (void)key;
        total += state.pressed_keys.size() * sizeof(i32);
    }
    return total;
}

Status CanvasRuntime::register_canvas(vm::ObjectRef canvas,
                                      bool game_canvas,
                                      bool suppress_key_events) {
    if (canvas.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot register a null Canvas");
    }
    auto [iterator, inserted] = canvases_.try_emplace(
        canvas.bits,
        CanvasState {
            .object = canvas,
            .game_canvas = game_canvas,
            .suppress_key_events = suppress_key_events,
        });
    if (!inserted) {
        iterator->second.game_canvas = game_canvas;
        iterator->second.suppress_key_events = suppress_key_events;
    }
    return {};
}

Status CanvasRuntime::set_display_visible(vm::ObjectRef displayable,
                                          bool visible) {
    CanvasState* state = find_state(displayable);
    if (state == nullptr) {
        return {};
    }
    state->display_visible = visible;
    visibility_changes_.push_back(VisibilityChange {
        .object = displayable,
        .visible = visible,
    });
    return {};
}

Status CanvasRuntime::request_repaint(vm::ObjectRef canvas,
                                      vm::CanvasRect region) {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    auto clipped = clipped_region(region);
    if (clipped.has_value()) {
        merge_region((*state)->repaint_region, *clipped);
    }
    return {};
}

Status CanvasRuntime::request_service_repaints(vm::ObjectRef canvas) {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    (*state)->service_requested = true;
    return {};
}

Status CanvasRuntime::set_fullscreen(vm::ObjectRef canvas, bool fullscreen) {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    if ((*state)->fullscreen == fullscreen) {
        return {};
    }
    (*state)->fullscreen = fullscreen;
    merge_region((*state)->repaint_region, full_region());

    auto id_value = machine_.heap().field(canvas, kDisplayableIdField);
    if (!id_value) return std::unexpected(id_value.error());
    auto id = id_value->as_int();
    if (!id) return std::unexpected(id.error());
    machine_.emit_ui_event(vm::UiBridgeEvent {
        .kind = kScreenUpdated,
        .component_id = *id,
        .component_type = kCanvasComponentType,
        .arguments = {fullscreen ? 1 : 0, 0, 0, kScreenModeMetadata},
    });
    return {};
}

Result<Dimensions> CanvasRuntime::canvas_dimensions(
    vm::ObjectRef canvas) const {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    return dimensions_;
}

i32 CanvasRuntime::game_action_for_key(i32 key_code) const noexcept {
    if (key_code == keymap_[0] || key_code == '2') return kActionUp;
    if (key_code == keymap_[1] || key_code == '8') return kActionDown;
    if (key_code == keymap_[2] || key_code == '4') return kActionLeft;
    if (key_code == keymap_[3] || key_code == '6') return kActionRight;
    if (key_code == keymap_[4] || key_code == '5') return kActionFire;
    if (key_code == '1') return kActionGameA;
    if (key_code == '3') return kActionGameB;
    if (key_code == '7') return kActionGameC;
    if (key_code == '9') return kActionGameD;
    return 0;
}

Result<i32> CanvasRuntime::key_code_for_action(i32 game_action) const {
    switch (game_action) {
    case kActionUp: return keymap_[0];
    case kActionDown: return keymap_[1];
    case kActionLeft: return keymap_[2];
    case kActionRight: return keymap_[3];
    case kActionFire: return keymap_[4];
    case kActionGameA: return static_cast<i32>('1');
    case kActionGameB: return static_cast<i32>('3');
    case kActionGameC: return static_cast<i32>('7');
    case kActionGameD: return static_cast<i32>('9');
    default:
        return fail_java("java/lang/IllegalArgumentException",
                         "unsupported Canvas game action");
    }
}

std::string CanvasRuntime::key_name(i32 key_code) const {
    const i32 action = game_action_for_key(key_code);
    switch (action) {
    case kActionUp: return "UP";
    case kActionDown: return "DOWN";
    case kActionLeft: return "LEFT";
    case kActionRight: return "RIGHT";
    case kActionFire: return "FIRE";
    case kActionGameA: return "GAME_A";
    case kActionGameB: return "GAME_B";
    case kActionGameC: return "GAME_C";
    case kActionGameD: return "GAME_D";
    default: break;
    }
    if (key_code == keymap_[5]) return "SOFT1";
    if (key_code == keymap_[6]) return "SOFT2";
    if (key_code >= 32 && key_code <= 126) {
        return std::string(1, static_cast<char>(key_code));
    }
    return "KEY_" + std::to_string(key_code);
}

i32 CanvasRuntime::game_key_states(vm::ObjectRef canvas) const noexcept {
    const CanvasState* state = find_state(canvas);
    return state == nullptr ? 0 : state->key_states;
}

Result<vm::ObjectRef> CanvasRuntime::game_graphics(vm::ObjectRef canvas) {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    if (!(*state)->game_canvas) {
        return fail_java("java/lang/IllegalStateException",
                         "Canvas is not a GameCanvas");
    }
    if (!(*state)->game_graphics.is_null()) {
        return (*state)->game_graphics;
    }
    if (!render_hooks_.acquire_game_graphics) {
        return vm::ObjectRef {};
    }
    auto graphics = render_hooks_.acquire_game_graphics(
        machine_, canvas, dimensions_);
    if (!graphics) return std::unexpected(graphics.error());
    (*state)->game_graphics = *graphics;
    return *graphics;
}

Status CanvasRuntime::request_game_flush(vm::ObjectRef canvas,
                                         vm::CanvasRect region) {
    auto state = require_state(canvas);
    if (!state) return std::unexpected(state.error());
    if (!(*state)->game_canvas) {
        return fail_java("java/lang/IllegalStateException",
                         "Canvas is not a GameCanvas");
    }
    auto clipped = clipped_region(region);
    if (clipped.has_value()) {
        merge_region((*state)->flush_region, *clipped);
    }
    return {};
}

void CanvasRuntime::append_reference_roots(
    std::vector<vm::ObjectRef>& roots) const {
    for (const auto& [key, state] : canvases_) {
        (void)key;
        if (!state.object.is_null()) roots.push_back(state.object);
        if (!state.game_graphics.is_null()) roots.push_back(state.game_graphics);
    }
}

CanvasRuntime::CanvasState* CanvasRuntime::find_state(
    vm::ObjectRef canvas) noexcept {
    const auto found = canvases_.find(canvas.bits);
    return found == canvases_.end() ? nullptr : &found->second;
}

const CanvasRuntime::CanvasState* CanvasRuntime::find_state(
    vm::ObjectRef canvas) const noexcept {
    const auto found = canvases_.find(canvas.bits);
    return found == canvases_.end() ? nullptr : &found->second;
}

Result<CanvasRuntime::CanvasState*> CanvasRuntime::require_state(
    vm::ObjectRef canvas) {
    CanvasState* state = find_state(canvas);
    if (state == nullptr) {
        return fail(ErrorCode::invalid_argument,
                    "Canvas object is not registered");
    }
    return state;
}

Result<const CanvasRuntime::CanvasState*> CanvasRuntime::require_state(
    vm::ObjectRef canvas) const {
    const CanvasState* state = find_state(canvas);
    if (state == nullptr) {
        return fail(ErrorCode::invalid_argument,
                    "Canvas object is not registered");
    }
    return state;
}

vm::CanvasRect CanvasRuntime::full_region() const noexcept {
    return vm::CanvasRect {
        .x = 0,
        .y = 0,
        .width = dimensions_.width,
        .height = dimensions_.height,
    };
}

std::optional<vm::CanvasRect> CanvasRuntime::clipped_region(
    vm::CanvasRect region) const noexcept {
    if (!dimensions_.valid() || region.width <= 0 || region.height <= 0) {
        return std::nullopt;
    }
    const i64 left = std::max<i64>(0, region.x);
    const i64 top = std::max<i64>(0, region.y);
    const i64 right = std::min<i64>(
        dimensions_.width,
        static_cast<i64>(region.x) + static_cast<i64>(region.width));
    const i64 bottom = std::min<i64>(
        dimensions_.height,
        static_cast<i64>(region.y) + static_cast<i64>(region.height));
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }
    return vm::CanvasRect {
        .x = saturating_i32(left),
        .y = saturating_i32(top),
        .width = saturating_i32(right - left),
        .height = saturating_i32(bottom - top),
    };
}

void CanvasRuntime::merge_region(
    std::optional<vm::CanvasRect>& destination,
    vm::CanvasRect region) noexcept {
    if (!destination.has_value()) {
        destination = region;
        return;
    }
    const i64 left = std::min<i64>(destination->x, region.x);
    const i64 top = std::min<i64>(destination->y, region.y);
    const i64 right = std::max<i64>(
        static_cast<i64>(destination->x) + destination->width,
        static_cast<i64>(region.x) + region.width);
    const i64 bottom = std::max<i64>(
        static_cast<i64>(destination->y) + destination->height,
        static_cast<i64>(region.y) + region.height);
    *destination = vm::CanvasRect {
        .x = saturating_i32(left),
        .y = saturating_i32(top),
        .width = saturating_i32(right - left),
        .height = saturating_i32(bottom - top),
    };
}

Status CanvasRuntime::process_visibility_changes() {
    while (!visibility_changes_.empty()) {
        const VisibilityChange change = visibility_changes_.front();
        visibility_changes_.pop_front();
        CanvasState* state = find_state(change.object);
        if (state == nullptr) continue;
        state->display_visible = change.visible;
        auto updated = update_effective_visibility(*state);
        if (!updated) return updated;
    }
    return {};
}

Status CanvasRuntime::process_size_changes() {
    if (!size_change_pending_) return {};
    size_change_pending_ = false;
    std::vector<vm::ObjectRef> canvases;
    canvases.reserve(canvases_.size());
    for (const auto& [key, state] : canvases_) {
        (void)key;
        canvases.push_back(state.object);
    }
    const std::array<vm::Value, 2> arguments {
        vm::Value::from_int(dimensions_.width),
        vm::Value::from_int(dimensions_.height),
    };
    for (vm::ObjectRef canvas : canvases) {
        auto invoked = invoke_void(canvas,
                                   "javax/microedition/lcdui/Canvas",
                                   "sizeChanged",
                                   "(II)V",
                                   arguments);
        if (!invoked) return invoked;
    }
    return {};
}

Status CanvasRuntime::process_inputs() {
    while (!inputs_.empty()) {
        const PendingInput input = inputs_.front();
        inputs_.pop_front();
        (void)input.sequence;

        CanvasState* state = nullptr;
        for (auto& [key, candidate] : canvases_) {
            (void)key;
            if (candidate.effectively_visible) {
                state = &candidate;
                break;
            }
        }
        if (state == nullptr) continue;

        if (input.kind == InputKind::key) {
            const i32 key_code = input.first;
            const bool pressed = input.second != 0;
            const bool was_pressed = state->pressed_keys.contains(key_code);
            const i32 mask = key_state_mask(key_code);
            std::string_view callback;
            if (pressed) {
                state->pressed_keys.insert(key_code);
                state->key_states |= mask;
                callback = was_pressed ? "keyRepeated" : "keyPressed";
            } else {
                state->pressed_keys.erase(key_code);
                state->key_states &= ~mask;
                callback = "keyReleased";
            }
            const bool suppress = state->game_canvas &&
                                  state->suppress_key_events;
            const vm::ObjectRef object = state->object;
            if (!suppress) {
                auto invoked = invoke_key_callback(*state, callback, key_code);
                if (!invoked) return invoked;
            }
            state = find_state(object);
            if (state == nullptr) continue;
        } else {
            std::string_view callback;
            switch (input.third) {
            case kPointerPressed: callback = "pointerPressed"; break;
            case kPointerReleased: callback = "pointerReleased"; break;
            case kPointerDragged: callback = "pointerDragged"; break;
            default: continue;
            }
            auto invoked = invoke_pointer_callback(
                *state, callback, input.first, input.second);
            if (!invoked) return invoked;
        }
    }
    return {};
}

Status CanvasRuntime::process_flushes() {
    std::vector<u64> pending;
    for (const auto& [key, state] : canvases_) {
        if (state.flush_region.has_value()) pending.push_back(key);
    }
    for (u64 key : pending) {
        auto found = canvases_.find(key);
        if (found == canvases_.end() ||
            !found->second.flush_region.has_value()) {
            continue;
        }
        const vm::ObjectRef canvas = found->second.object;
        const vm::CanvasRect region = *found->second.flush_region;
        found->second.flush_region.reset();
        auto graphics = game_graphics(canvas);
        if (!graphics) return std::unexpected(graphics.error());
        if (render_hooks_.flush_game_graphics) {
            auto flushed = render_hooks_.flush_game_graphics(
                machine_, canvas, *graphics, region);
            if (!flushed) return flushed;
        } else {
            auto current = find_state(canvas);
            if (current != nullptr) {
                merge_region(current->repaint_region, region);
            }
        }
    }
    return {};
}

Status CanvasRuntime::process_repaints() {
    std::vector<u64> pending;
    for (const auto& [key, state] : canvases_) {
        if (state.effectively_visible && state.repaint_region.has_value()) {
            pending.push_back(key);
        }
    }
    for (u64 key : pending) {
        auto found = canvases_.find(key);
        if (found == canvases_.end() ||
            !found->second.effectively_visible ||
            !found->second.repaint_region.has_value()) {
            continue;
        }
        const vm::ObjectRef canvas = found->second.object;
        const vm::CanvasRect region = *found->second.repaint_region;
        found->second.repaint_region.reset();
        found->second.service_requested = false;

        vm::ObjectRef graphics;
        if (render_hooks_.acquire_paint_graphics) {
            auto acquired = render_hooks_.acquire_paint_graphics(
                machine_, canvas, dimensions_, region);
            if (!acquired) return std::unexpected(acquired.error());
            graphics = *acquired;
        }
        const std::array<vm::Value, 1> arguments {
            vm::Value::from_reference(graphics),
        };
        auto painted = invoke_void(canvas,
                                   "javax/microedition/lcdui/Canvas",
                                   "paint",
                                   "(Ljavax/microedition/lcdui/Graphics;)V",
                                   arguments);
        if (!painted) return painted;
        if (render_hooks_.commit_paint) {
            auto committed = render_hooks_.commit_paint(
                machine_, canvas, graphics, region);
            if (!committed) return committed;
        }
    }
    return {};
}

Status CanvasRuntime::invoke_void(vm::ObjectRef receiver,
                                  std::string_view declared_class,
                                  std::string_view method_name,
                                  std::string_view descriptor,
                                  std::span<const vm::Value> arguments) {
    auto result = machine_.invoke_instance(receiver,
                                           declared_class,
                                           method_name,
                                           descriptor,
                                           arguments);
    if (!result) return std::unexpected(result.error());
    if (result->completed_normally()) return {};
    if (!result->throwable.has_value()) {
        return fail(ErrorCode::internal_error,
                    "Canvas callback failed without a Java throwable");
    }
    auto throwable = machine_.heap().class_name(*result->throwable);
    if (!throwable) return std::unexpected(throwable.error());
    return fail(ErrorCode::java_exception,
                "Canvas callback threw " + *throwable);
}

Status CanvasRuntime::invoke_key_callback(CanvasState& state,
                                          std::string_view method,
                                          i32 key_code) {
    const std::array<vm::Value, 1> arguments {
        vm::Value::from_int(key_code),
    };
    return invoke_void(state.object,
                       "javax/microedition/lcdui/Canvas",
                       method,
                       "(I)V",
                       arguments);
}

Status CanvasRuntime::invoke_pointer_callback(CanvasState& state,
                                              std::string_view method,
                                              i32 x,
                                              i32 y) {
    const std::array<vm::Value, 2> arguments {
        vm::Value::from_int(x),
        vm::Value::from_int(y),
    };
    return invoke_void(state.object,
                       "javax/microedition/lcdui/Canvas",
                       method,
                       "(II)V",
                       arguments);
}

Status CanvasRuntime::update_effective_visibility(CanvasState& state) {
    const bool desired = host_foreground_ && state.display_visible;
    if (desired == state.effectively_visible) return {};
    state.effectively_visible = desired;
    const vm::ObjectRef canvas = state.object;
    if (desired) {
        auto shown = invoke_void(canvas,
                                 "javax/microedition/lcdui/Canvas",
                                 "showNotify",
                                 "()V");
        if (!shown) return shown;
        CanvasState* current = find_state(canvas);
        if (current != nullptr) {
            merge_region(current->repaint_region, full_region());
        }
        return {};
    }
    state.key_states = 0;
    state.pressed_keys.clear();
    return invoke_void(canvas,
                       "javax/microedition/lcdui/Canvas",
                       "hideNotify",
                       "()V");
}

i32 CanvasRuntime::key_state_mask(i32 key_code) const noexcept {
    const i32 action = game_action_for_key(key_code);
    if (action <= 0 || action >= 31) return 0;
    return static_cast<i32>(1U << static_cast<u32>(action));
}

} // namespace phoneme::runtime
