#include "CanvasNatives.hpp"

#include <array>
#include <exception>
#include <span>
#include <string>
#include <utility>

#include "phoneme/vm/CanvasBridge.hpp"
#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {
namespace {

constexpr i32 kEventScreenCreated = 2;
constexpr i32 kCanvasComponentType = 22;
constexpr usize kDisplayableIdField = 0;
constexpr usize kDisplayableTypeField = 1;
constexpr usize kDisplayableTitleField = 2;
constexpr usize kDisplayableListenerField = 3;
constexpr usize kDisplayableCommandsField = 4;
constexpr usize kDisplayableCommandCountField = 5;
constexpr usize kDisplayableShownField = 6;

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) std::terminate();
}

[[nodiscard]] Result<ObjectRef> receiver(
    std::span<const Value> arguments,
    std::string_view operation) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " is missing its receiver");
    }
    auto object = arguments.front().as_reference();
    if (!object || object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         std::string(operation) + " receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<i32> integer_argument(
    std::span<const Value> arguments,
    usize index,
    std::string_view operation) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    std::string(operation) + " is missing an argument");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<CanvasBridge*> bridge(Machine& machine) {
    CanvasBridge* value = machine.canvas_bridge();
    if (value == nullptr) {
        return fail(ErrorCode::invalid_state,
                    "Canvas runtime is not configured for this VM");
    }
    return value;
}

[[nodiscard]] Result<ObjectRef> create_string(Machine& machine,
                                              std::u16string text) {
    auto object = machine.class_states().allocate_instance(
        machine.heap(), "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Status set_int_field(Machine& machine,
                                   ObjectRef object,
                                   usize index,
                                   i32 value) {
    return machine.heap().set_field(object, index, Value::from_int(value));
}

[[nodiscard]] Status set_reference_field(Machine& machine,
                                         ObjectRef object,
                                         usize index,
                                         ObjectRef value) {
    return machine.heap().set_field(object, index,
                                    Value::from_reference(value));
}

[[nodiscard]] Result<i32> ensure_native_id(Machine& machine,
                                           ObjectRef canvas) {
    auto current = machine.heap().field(canvas, kDisplayableIdField);
    if (!current) return std::unexpected(current.error());
    auto id = current->as_int();
    if (!id) return std::unexpected(id.error());
    if (*id == 0) {
        *id = machine.allocate_ui_component_id();
        if (*id == 0) {
            return fail_java("java/lang/OutOfMemoryError",
                             "Canvas component ID space is exhausted");
        }
        auto stored = set_int_field(machine, canvas,
                                    kDisplayableIdField, *id);
        if (!stored) return std::unexpected(stored.error());
    }
    auto registered = machine.register_ui_component(*id, canvas);
    if (!registered) return std::unexpected(registered.error());
    return *id;
}

[[nodiscard]] Result<std::optional<Value>> dimension_value(
    Machine& machine,
    std::span<const Value> arguments,
    bool width) {
    auto canvas = receiver(arguments, width ? "Canvas.getWidth"
                                            : "Canvas.getHeight");
    if (!canvas) return std::unexpected(canvas.error());
    auto runtime = bridge(machine);
    if (!runtime) return std::unexpected(runtime.error());
    auto dimensions = (*runtime)->canvas_dimensions(*canvas);
    if (!dimensions) return std::unexpected(dimensions.error());
    return std::optional<Value>(Value::from_int(
        width ? dimensions->width : dimensions->height));
}

[[nodiscard]] Result<std::optional<Value>> no_op_callback(
    Machine&,
    std::span<const Value>) {
    return std::optional<Value> {};
}

} // namespace

Status initialize_canvas_object(Machine& machine,
                                ObjectRef canvas,
                                bool game_canvas,
                                bool suppress_key_events) {
    auto runtime = bridge(machine);
    if (!runtime) return std::unexpected(runtime.error());
    auto id = ensure_native_id(machine, canvas);
    if (!id) return std::unexpected(id.error());
    auto title = create_string(machine, {});
    if (!title) return std::unexpected(title.error());
    auto commands = machine.heap().allocate_array(
        "[Ljavax/microedition/lcdui/Command;",
        4U,
        Value::from_reference({}));
    if (!commands) {
        if (commands.error().code == ErrorCode::overflow) {
            return fail_java("java/lang/OutOfMemoryError",
                             "Canvas command array allocation failed");
        }
        return std::unexpected(commands.error());
    }

    const Status type_stored = set_int_field(
        machine, canvas, kDisplayableTypeField, kCanvasComponentType);
    const Status title_stored = set_reference_field(
        machine, canvas, kDisplayableTitleField, *title);
    const Status listener_stored = set_reference_field(
        machine, canvas, kDisplayableListenerField, {});
    const Status commands_stored = set_reference_field(
        machine, canvas, kDisplayableCommandsField, *commands);
    const Status count_stored = set_int_field(
        machine, canvas, kDisplayableCommandCountField, 0);
    const Status shown_stored = set_int_field(
        machine, canvas, kDisplayableShownField, 0);
    if (!type_stored) return type_stored;
    if (!title_stored) return title_stored;
    if (!listener_stored) return listener_stored;
    if (!commands_stored) return commands_stored;
    if (!count_stored) return count_stored;
    if (!shown_stored) return shown_stored;

    auto registered = (*runtime)->register_canvas(
        canvas, game_canvas, suppress_key_events);
    if (!registered) return registered;
    machine.emit_ui_event(UiBridgeEvent {
        .kind = kEventScreenCreated,
        .component_id = *id,
        .component_type = kCanvasComponentType,
    });
    return {};
}

void register_canvas_natives(NativeMethodRegistry& registry) {
    constexpr const char* kCanvas = "javax/microedition/lcdui/Canvas";
    constexpr const char* kNokiaFullCanvas = "com/nokia/mid/ui/FullCanvas";

    const auto initialize_canvas = [](Machine& machine,
                                      std::span<const Value> arguments)
        -> Result<std::optional<Value>> {
        auto canvas = receiver(arguments, "Canvas.<init>");
        if (!canvas) return std::unexpected(canvas.error());
        auto initialized = initialize_canvas_object(
            machine, *canvas, false, false);
        if (!initialized) return std::unexpected(initialized.error());
        return std::optional<Value> {};
    };
    add(registry, kCanvas, "<init>", "()V", initialize_canvas);
    add(registry, kNokiaFullCanvas, "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "FullCanvas.<init>");
            if (!canvas) return std::unexpected(canvas.error());
            auto initialized = initialize_canvas_object(
                machine, *canvas, false, false);
            if (!initialized) return std::unexpected(initialized.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto fullscreen = (*runtime)->set_fullscreen(*canvas, true);
            if (!fullscreen) return std::unexpected(fullscreen.error());
            return std::optional<Value> {};
        });
    for (const char* method_name : {"addCommand", "setCommandListener"}) {
        add(registry, kNokiaFullCanvas, method_name,
            method_name == std::string_view("addCommand")
                ? "(Ljavax/microedition/lcdui/Command;)V"
                : "(Ljavax/microedition/lcdui/CommandListener;)V",
            [method_name](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto canvas = receiver(arguments, method_name);
                if (!canvas) return std::unexpected(canvas.error());
                return fail_java(
                    "java/lang/IllegalStateException",
                    std::string(method_name) +
                        " is not supported by Nokia FullCanvas");
            });
    }

    add(registry, kCanvas, "getWidth", "()I",
        [](Machine& machine, std::span<const Value> arguments) {
            return dimension_value(machine, arguments, true);
        });
    add(registry, kCanvas, "getHeight", "()I",
        [](Machine& machine, std::span<const Value> arguments) {
            return dimension_value(machine, arguments, false);
        });
    add(registry, kCanvas, "isDoubleBuffered", "()Z",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, kCanvas, "hasPointerEvents", "()Z",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            return std::optional<Value>(Value::from_int(
                (*runtime)->pointer_events_supported() ? 1 : 0));
        });
    add(registry, kCanvas, "hasPointerMotionEvents", "()Z",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            return std::optional<Value>(Value::from_int(
                (*runtime)->pointer_motion_supported() ? 1 : 0));
        });
    add(registry, kCanvas, "hasRepeatEvents", "()Z",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            return std::optional<Value>(Value::from_int(
                (*runtime)->repeat_events_supported() ? 1 : 0));
        });

    add(registry, kCanvas, "getGameAction", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto key_code = integer_argument(
                arguments, 1U, "Canvas.getGameAction");
            if (!key_code) return std::unexpected(key_code.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            return std::optional<Value>(Value::from_int(
                (*runtime)->game_action_for_key(*key_code)));
        });
    add(registry, kCanvas, "getKeyCode", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto action = integer_argument(
                arguments, 1U, "Canvas.getKeyCode");
            if (!action) return std::unexpected(action.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto key_code = (*runtime)->key_code_for_action(*action);
            if (!key_code) return std::unexpected(key_code.error());
            return std::optional<Value>(Value::from_int(*key_code));
        });
    add(registry, kCanvas, "getKeyName", "(I)Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto key_code = integer_argument(
                arguments, 1U, "Canvas.getKeyName");
            if (!key_code) return std::unexpected(key_code.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            const std::string name = (*runtime)->key_name(*key_code);
            std::u16string text;
            text.reserve(name.size());
            for (const char character : name) {
                text.push_back(static_cast<char16_t>(
                    static_cast<unsigned char>(character)));
            }
            auto string = create_string(machine, std::move(text));
            if (!string) return std::unexpected(string.error());
            return std::optional<Value>(Value::from_reference(*string));
        });

    add(registry, kCanvas, "repaint", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "Canvas.repaint");
            if (!canvas) return std::unexpected(canvas.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto dimensions = (*runtime)->canvas_dimensions(*canvas);
            if (!dimensions) return std::unexpected(dimensions.error());
            auto requested = (*runtime)->request_repaint(
                *canvas,
                CanvasRect {0, 0, dimensions->width, dimensions->height});
            if (!requested) return std::unexpected(requested.error());
            return std::optional<Value> {};
        });
    add(registry, kCanvas, "repaint", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "Canvas.repaint");
            if (!canvas) return std::unexpected(canvas.error());
            std::array<i32, 4> values {};
            for (usize index = 0; index < values.size(); ++index) {
                auto value = integer_argument(
                    arguments, index + 1U, "Canvas.repaint");
                if (!value) return std::unexpected(value.error());
                values[index] = *value;
            }
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto requested = (*runtime)->request_repaint(
                *canvas,
                CanvasRect {values[0], values[1], values[2], values[3]});
            if (!requested) return std::unexpected(requested.error());
            return std::optional<Value> {};
        });
    add(registry, kCanvas, "serviceRepaints", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "Canvas.serviceRepaints");
            if (!canvas) return std::unexpected(canvas.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto requested = (*runtime)->request_service_repaints(*canvas);
            if (!requested) return std::unexpected(requested.error());
            return std::optional<Value> {};
        });
    add(registry, kCanvas, "setFullScreenMode", "(Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "Canvas.setFullScreenMode");
            auto fullscreen = integer_argument(
                arguments, 1U, "Canvas.setFullScreenMode");
            if (!canvas) return std::unexpected(canvas.error());
            if (!fullscreen) return std::unexpected(fullscreen.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto changed = (*runtime)->set_fullscreen(
                *canvas, *fullscreen != 0);
            if (!changed) return std::unexpected(changed.error());
            return std::optional<Value> {};
        });

    add(registry, kCanvas, "keyPressed", "(I)V", no_op_callback);
    add(registry, kCanvas, "keyReleased", "(I)V", no_op_callback);
    add(registry, kCanvas, "keyRepeated", "(I)V", no_op_callback);
    add(registry, kCanvas, "pointerPressed", "(II)V", no_op_callback);
    add(registry, kCanvas, "pointerReleased", "(II)V", no_op_callback);
    add(registry, kCanvas, "pointerDragged", "(II)V", no_op_callback);
    add(registry, kCanvas, "showNotify", "()V", no_op_callback);
    add(registry, kCanvas, "hideNotify", "()V", no_op_callback);
    add(registry, kCanvas, "sizeChanged", "(II)V", no_op_callback);
}

} // namespace phoneme::vm
