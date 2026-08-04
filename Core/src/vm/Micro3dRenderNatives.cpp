#include "Micro3dNativeModules.hpp"

#include <span>
#include <string_view>

#include "Micro3dNativeSupport.hpp"

namespace phoneme::vm {
namespace micro3d {
namespace {

[[nodiscard]] Status require_bound(
    Machine& machine, ObjectRef self, std::string_view operation) {
    auto bound = int_field(machine, self, kGraphics3D, "bound", "Z");
    if (!bound) return std::unexpected(bound.error());
    if (*bound == 0) {
        return fail_java("java/lang/IllegalStateException",
                         std::string(operation) + ": no target is bound");
    }
    return {};
}

void register_lifecycle(NativeMethodRegistry& registry) {
    m3g::add(registry, kGraphics3D, "<init>", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.<init>");
            if (!self) return std::unexpected(self.error());
            auto stored = set_int_field(
                machine, *self, kGraphics3D, "bound", 0, "Z");
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "bind",
             "(Ljavax/microedition/lcdui/Graphics;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.bind");
            auto graphics = m3g::reference_argument(
                args, 1U, "Graphics3D.bind", false);
            if (!self) return std::unexpected(self.error());
            if (!graphics) return std::unexpected(graphics.error());
            auto bound = int_field(
                machine, *self, kGraphics3D, "bound", "Z");
            if (!bound) return std::unexpected(bound.error());
            if (*bound != 0) {
                return fail_java("java/lang/IllegalStateException",
                                 "Micro3D target is already bound");
            }
            auto stored_graphics = set_reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;", *graphics);
            auto stored_bound = set_int_field(
                machine, *self, kGraphics3D, "bound", 1, "Z");
            if (!stored_graphics) {
                return std::unexpected(stored_graphics.error());
            }
            if (!stored_bound) return std::unexpected(stored_bound.error());
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "release",
             "(Ljavax/microedition/lcdui/Graphics;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.release");
            auto graphics = m3g::reference_argument(
                args, 1U, "Graphics3D.release", false);
            if (!self) return std::unexpected(self.error());
            if (!graphics) return std::unexpected(graphics.error());
            auto current = reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;");
            if (!current) return std::unexpected(current.error());
            static_cast<void>(graphics);
            auto stored_graphics = set_reference_field(
                machine, *self, kGraphics3D, "graphics",
                "Ljavax/microedition/lcdui/Graphics;", {});
            auto stored_bound = set_int_field(
                machine, *self, kGraphics3D, "bound", 0, "Z");
            if (!stored_graphics) {
                return std::unexpected(stored_graphics.error());
            }
            if (!stored_bound) {
                return std::unexpected(stored_bound.error());
            }
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "dispose", "()V",
        [](Machine&, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.dispose");
            if (!self) return std::unexpected(self.error());
            return void_result();
        });
    m3g::add(registry, kGraphics3D, "flush", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.flush");
            if (!self) return std::unexpected(self.error());
            auto bound = require_bound(machine, *self, "Graphics3D.flush");
            if (!bound) return std::unexpected(bound.error());
            return void_result();
        });
}

void register_figure_rendering(NativeMethodRegistry& registry) {
    const auto render = [](Machine& machine,
                           std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "Graphics3D.drawFigure");
        auto figure = m3g::reference_argument(
            args, 1U, "Graphics3D.drawFigure", false);
        auto layout = m3g::reference_argument(
            args, 4U, "Graphics3D.drawFigure", false);
        auto effect = m3g::reference_argument(
            args, 5U, "Graphics3D.drawFigure", false);
        if (!self) return std::unexpected(self.error());
        if (!figure) return std::unexpected(figure.error());
        if (!layout) return std::unexpected(layout.error());
        if (!effect) return std::unexpected(effect.error());
        auto bound = require_bound(
            machine, *self, "Graphics3D.drawFigure");
        if (!bound) return std::unexpected(bound.error());
        auto live = require_not_disposed(
            machine, *figure, kFigure, "Figure");
        if (!live) return std::unexpected(live.error());
        return void_result();
    };
    constexpr const char* descriptor =
        "(Lcom/mascotcapsule/micro3d/v3/Figure;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;)V";
    m3g::add(registry, kGraphics3D, "drawFigure", descriptor, render);
    m3g::add(registry, kGraphics3D, "renderFigure", descriptor, render);
}

void register_command_rendering(NativeMethodRegistry& registry) {
    const auto render = [](Machine& machine,
                           std::span<const Value> args) -> NativeResult {
        auto self = m3g::receiver(args, "Graphics3D.drawCommandList");
        auto layout = m3g::reference_argument(
            args, 4U, "Graphics3D.drawCommandList", false);
        auto effect = m3g::reference_argument(
            args, 5U, "Graphics3D.drawCommandList", false);
        auto commands = m3g::reference_argument(
            args, 6U, "Graphics3D.drawCommandList", false);
        if (!self) return std::unexpected(self.error());
        if (!layout) return std::unexpected(layout.error());
        if (!effect) return std::unexpected(effect.error());
        if (!commands) return std::unexpected(commands.error());
        auto bound = require_bound(
            machine, *self, "Graphics3D.drawCommandList");
        if (!bound) return std::unexpected(bound.error());
        auto values = read_int_array(
            machine, *commands, "Graphics3D.drawCommandList");
        if (!values) return std::unexpected(values.error());
        if (values->empty()) {
            return fail_java("java/lang/IllegalArgumentException",
                             "Micro3D command list is empty");
        }
        return void_result();
    };
    m3g::add(registry, kGraphics3D, "drawCommandList",
        "([Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V", render);
    m3g::add(registry, kGraphics3D, "drawCommandList",
        "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;[I)V", render);
}

void register_primitive_rendering(NativeMethodRegistry& registry) {
    m3g::add(registry, kGraphics3D, "renderPrimitives",
        "(Lcom/mascotcapsule/micro3d/v3/Texture;II"
        "Lcom/mascotcapsule/micro3d/v3/FigureLayout;"
        "Lcom/mascotcapsule/micro3d/v3/Effect3D;II[I[I[I[I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Graphics3D.renderPrimitives");
            auto layout = m3g::reference_argument(
                args, 4U, "Graphics3D.renderPrimitives", false);
            auto effect = m3g::reference_argument(
                args, 5U, "Graphics3D.renderPrimitives", false);
            auto command = m3g::int_argument(
                args, 6U, "Graphics3D.renderPrimitives");
            auto count = m3g::int_argument(
                args, 7U, "Graphics3D.renderPrimitives");
            if (!self) return std::unexpected(self.error());
            if (!layout) return std::unexpected(layout.error());
            if (!effect) return std::unexpected(effect.error());
            if (!command) return std::unexpected(command.error());
            if (!count) return std::unexpected(count.error());
            for (usize index = 8U; index <= 11U; ++index) {
                auto array = m3g::reference_argument(
                    args, index, "Graphics3D.renderPrimitives", false);
                if (!array) return std::unexpected(array.error());
                auto values = read_int_array(
                    machine, *array, "Graphics3D.renderPrimitives");
                if (!values) return std::unexpected(values.error());
            }
            auto bound = require_bound(
                machine, *self, "Graphics3D.renderPrimitives");
            if (!bound) return std::unexpected(bound.error());
            if (*command < 0 || *count <= 0 || *count >= 256) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid Micro3D primitive command");
            }
            return void_result();
        });
}

} // namespace
} // namespace micro3d

void register_micro3d_render_natives(NativeMethodRegistry& registry) {
    micro3d::register_lifecycle(registry);
    micro3d::register_figure_rendering(registry);
    micro3d::register_command_rendering(registry);
    micro3d::register_primitive_rendering(registry);
}

} // namespace phoneme::vm
