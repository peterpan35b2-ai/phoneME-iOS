#include "GameCanvasNatives.hpp"

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

[[nodiscard]] Result<ObjectRef> receiver(std::span<const Value> arguments,
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

[[nodiscard]] Result<i32> integer_argument(std::span<const Value> arguments,
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

} // namespace

void register_game_canvas_natives(NativeMethodRegistry& registry) {
    constexpr const char* kOwner =
        "javax/microedition/lcdui/game/GameCanvas";

    add(registry, kOwner, "<init>", "(Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.<init>");
            auto suppress = integer_argument(arguments, 1U,
                                             "GameCanvas.<init>");
            if (!canvas) return std::unexpected(canvas.error());
            if (!suppress) return std::unexpected(suppress.error());
            auto initialized = initialize_canvas_object(
                machine, *canvas, true, *suppress != 0);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });

    add(registry, kOwner, "paint",
        "(Ljavax/microedition/lcdui/Graphics;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.paint");
            if (!canvas) return std::unexpected(canvas.error());
            // Compatibility path for direct-render GameCanvas implementations.
            // Several Nokia/Sony Ericsson-era games draw exclusively through
            // getGraphics()/flushGraphics() and omit a paint override.
            return std::optional<Value> {};
        });

    add(registry, kOwner, "getKeyStates", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.getKeyStates");
            if (!canvas) return std::unexpected(canvas.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            return std::optional<Value>(Value::from_int(
                (*runtime)->game_key_states(*canvas)));
        });

    add(registry, kOwner, "getGraphics",
        "()Ljavax/microedition/lcdui/Graphics;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.getGraphics");
            if (!canvas) return std::unexpected(canvas.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto graphics = (*runtime)->game_graphics(*canvas);
            if (!graphics) return std::unexpected(graphics.error());
            return std::optional<Value>(Value::from_reference(*graphics));
        });

    add(registry, kOwner, "flushGraphics", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.flushGraphics");
            if (!canvas) return std::unexpected(canvas.error());
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto dimensions = (*runtime)->canvas_dimensions(*canvas);
            if (!dimensions) return std::unexpected(dimensions.error());
            auto flushed = (*runtime)->request_game_flush(
                *canvas, CanvasRect {0, 0,
                                     dimensions->width,
                                     dimensions->height});
            if (!flushed) return std::unexpected(flushed.error());
            return std::optional<Value> {};
        });

    add(registry, kOwner, "flushGraphics", "(IIII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto canvas = receiver(arguments, "GameCanvas.flushGraphics");
            if (!canvas) return std::unexpected(canvas.error());
            std::array<i32, 4> values {};
            for (usize index = 0; index < values.size(); ++index) {
                auto value = integer_argument(arguments, index + 1U,
                                              "GameCanvas.flushGraphics");
                if (!value) return std::unexpected(value.error());
                values[index] = *value;
            }
            auto runtime = bridge(machine);
            if (!runtime) return std::unexpected(runtime.error());
            auto flushed = (*runtime)->request_game_flush(
                *canvas, CanvasRect {values[0], values[1],
                                     values[2], values[3]});
            if (!flushed) return std::unexpected(flushed.error());
            return std::optional<Value> {};
        });
}

} // namespace phoneme::vm
