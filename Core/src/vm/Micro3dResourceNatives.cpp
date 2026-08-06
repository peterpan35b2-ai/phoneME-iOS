#include "Micro3dNativeModules.hpp"

#include <array>
#include <span>
#include <utility>

#include "Micro3dNativeSupport.hpp"
#include "Micro3dSoftware.hpp"

namespace phoneme::vm {
namespace micro3d {
namespace {

[[nodiscard]] NativeResult initialize_texture(
    Machine& machine, ObjectRef self, ObjectRef data, i32 for_model) {
    auto bytes = byte_array(machine, data, "Texture.<init>");
    if (!bytes) return std::unexpected(bytes.error());
    auto cached = software::cache_texture(machine, self, *bytes);
    if (!cached) return std::unexpected(cached.error());
    auto texture = software::cached_texture(machine, self);
    if (!texture) return std::unexpected(texture.error());
    const i32 width = (*texture)->width;
    const i32 height = (*texture)->height;
    auto stored_data = set_reference_field(
        machine, self, kTexture, "data", "[B", data);
    auto stored_model = set_int_field(
        machine, self, kTexture, "isForModel",
        for_model != 0 ? 1 : 0, "Z");
    auto stored_disposed = set_int_field(
        machine, self, kTexture, "disposed", 0, "Z");
    auto stored_width = set_int_field(
        machine, self, kTexture, "width", width);
    auto stored_height = set_int_field(
        machine, self, kTexture, "height", height);
    if (!stored_data) return std::unexpected(stored_data.error());
    if (!stored_model) return std::unexpected(stored_model.error());
    if (!stored_disposed) {
        return std::unexpected(stored_disposed.error());
    }
    if (!stored_width) return std::unexpected(stored_width.error());
    if (!stored_height) return std::unexpected(stored_height.error());
    return void_result();
}

void register_texture(NativeMethodRegistry& registry) {
    m3g::add(registry, kTexture, "<init>", "([BZ)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Texture.<init>");
            auto data = m3g::reference_argument(
                args, 1U, "Texture.<init>", false);
            auto for_model = m3g::int_argument(
                args, 2U, "Texture.<init>");
            if (!self) return std::unexpected(self.error());
            if (!data) return std::unexpected(data.error());
            if (!for_model) return std::unexpected(for_model.error());
            return initialize_texture(machine, *self, *data, *for_model);
        });
    m3g::add(registry, kTexture, "<init>", "(Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Texture.<init>");
            auto name = m3g::reference_argument(
                args, 1U, "Texture.<init>", false);
            auto for_model = m3g::int_argument(
                args, 2U, "Texture.<init>");
            if (!self) return std::unexpected(self.error());
            if (!name) return std::unexpected(name.error());
            if (!for_model) return std::unexpected(for_model.error());
            auto data = resource_byte_array(
                machine, *name, "Texture.<init>");
            if (!data) return std::unexpected(data.error());
            return initialize_texture(machine, *self, *data, *for_model);
        });
    m3g::add(registry, kTexture, "dispose", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Texture.dispose");
            if (!self) return std::unexpected(self.error());
            software::erase_resource(machine, *self);
            auto stored_data = set_reference_field(
                machine, *self, kTexture, "data", "[B", {});
            auto stored_disposed = set_int_field(
                machine, *self, kTexture, "disposed", 1, "Z");
            if (!stored_data) return std::unexpected(stored_data.error());
            if (!stored_disposed) {
                return std::unexpected(stored_disposed.error());
            }
            return void_result();
        });
}

[[nodiscard]] NativeResult initialize_action_table(
    Machine& machine, ObjectRef self, ObjectRef data) {
    auto bytes = byte_array(machine, data, "ActionTable.<init>");
    if (!bytes) return std::unexpected(bytes.error());
    auto cached = software::cache_actions(machine, self, *bytes);
    if (!cached) return std::unexpected(cached.error());
    auto actions = software::cached_actions(machine, self);
    if (!actions) return std::unexpected(actions.error());
    std::vector<i32> frames;
    frames.reserve((*actions)->actions.size());
    for (const software::Action& action : (*actions)->actions) {
        if (action.keyframes > (std::numeric_limits<i32>::max() >> 16)) {
            return fail(ErrorCode::overflow,
                        "ActionTable frame count overflows 16.16 fixed point");
        }
        frames.push_back(action.keyframes << 16);
    }
    auto frame_array = create_int_array(machine, frames);
    if (!frame_array) return std::unexpected(frame_array.error());
    auto stored_data = set_reference_field(
        machine, self, kActionTable, "data", "[B", data);
    auto stored_frames = set_reference_field(
        machine, self, kActionTable, "actionFrames", "[I", *frame_array);
    auto stored_disposed = set_int_field(
        machine, self, kActionTable, "disposed", 0, "Z");
    if (!stored_data) return std::unexpected(stored_data.error());
    if (!stored_frames) return std::unexpected(stored_frames.error());
    if (!stored_disposed) {
        return std::unexpected(stored_disposed.error());
    }
    return void_result();
}

void register_action_table(NativeMethodRegistry& registry) {
    m3g::add(registry, kActionTable, "<init>", "([B)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "ActionTable.<init>");
            auto data = m3g::reference_argument(
                args, 1U, "ActionTable.<init>", false);
            if (!self) return std::unexpected(self.error());
            if (!data) return std::unexpected(data.error());
            return initialize_action_table(machine, *self, *data);
        });
    m3g::add(registry, kActionTable, "<init>", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "ActionTable.<init>");
            auto name = m3g::reference_argument(
                args, 1U, "ActionTable.<init>", false);
            if (!self) return std::unexpected(self.error());
            if (!name) return std::unexpected(name.error());
            auto data = resource_byte_array(
                machine, *name, "ActionTable.<init>");
            if (!data) return std::unexpected(data.error());
            return initialize_action_table(machine, *self, *data);
        });
    m3g::add(registry, kActionTable, "dispose", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "ActionTable.dispose");
            if (!self) return std::unexpected(self.error());
            software::erase_resource(machine, *self);
            auto stored_data = set_reference_field(
                machine, *self, kActionTable, "data", "[B", {});
            auto stored_frames = set_reference_field(
                machine, *self, kActionTable, "actionFrames", "[I", {});
            auto stored_disposed = set_int_field(
                machine, *self, kActionTable, "disposed", 1, "Z");
            if (!stored_data) return std::unexpected(stored_data.error());
            if (!stored_frames) return std::unexpected(stored_frames.error());
            if (!stored_disposed) {
                return std::unexpected(stored_disposed.error());
            }
            return void_result();
        });
    for (const char* method_name : {"getNumAction", "getNumActions"}) {
        m3g::add(registry, kActionTable, method_name, "()I",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "ActionTable.getNumActions");
                if (!self) return std::unexpected(self.error());
                auto live = require_not_disposed(
                    machine, *self, kActionTable, "ActionTable");
                if (!live) return std::unexpected(live.error());
                auto frames = reference_field(
                    machine, *self, kActionTable, "actionFrames", "[I");
                if (!frames) return std::unexpected(frames.error());
                auto length = machine.heap().array_length(*frames);
                if (!length) return std::unexpected(length.error());
                if (*length > static_cast<usize>(
                                  std::numeric_limits<i32>::max())) {
                    return fail(ErrorCode::overflow,
                                "ActionTable action count overflows int");
                }
                return int_result(static_cast<i32>(*length));
            });
    }
    for (const char* method_name : {"getNumFrame", "getNumFrames"}) {
        m3g::add(registry, kActionTable, method_name, "(I)I",
            [](Machine& machine,
               std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "ActionTable.getNumFrames");
                auto index = m3g::int_argument(
                    args, 1U, "ActionTable.getNumFrames");
                if (!self) return std::unexpected(self.error());
                if (!index) return std::unexpected(index.error());
                auto live = require_not_disposed(
                    machine, *self, kActionTable, "ActionTable");
                if (!live) return std::unexpected(live.error());
                auto frames = reference_field(
                    machine, *self, kActionTable, "actionFrames", "[I");
                if (!frames) return std::unexpected(frames.error());
                auto length = machine.heap().array_length(*frames);
                if (!length) return std::unexpected(length.error());
                if (*index < 0 || static_cast<usize>(*index) >= *length) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "invalid ActionTable action index");
                }
                auto value = machine.heap().element(
                    *frames, static_cast<usize>(*index));
                if (!value) return std::unexpected(value.error());
                auto integer = value->as_int();
                if (!integer) return std::unexpected(integer.error());
                return int_result(*integer);
            });
    }
}

[[nodiscard]] NativeResult initialize_figure(
    Machine& machine, ObjectRef self, ObjectRef data) {
    auto bytes = byte_array(machine, data, "Figure.<init>");
    if (!bytes) return std::unexpected(bytes.error());
    auto cached = software::cache_model(machine, self, *bytes);
    if (!cached) return std::unexpected(cached.error());
    auto model = software::cached_model(machine, self);
    if (!model) return std::unexpected(model.error());
    auto stored_data = set_reference_field(
        machine, self, kFigure, "data", "[B", data);
    auto stored_texture = set_int_field(
        machine, self, kFigure, "textureIndex", -1);
    auto stored_pattern = set_int_field(
        machine, self, kFigure, "pattern", 0);
    auto stored_posture_table = set_reference_field(
        machine, self, kFigure, "postureTable",
        "Lcom/mascotcapsule/micro3d/v3/ActionTable;", {});
    auto stored_posture_action = set_int_field(
        machine, self, kFigure, "postureAction", -1);
    auto stored_posture_frame = set_int_field(
        machine, self, kFigure, "postureFrame", 0);
    auto stored_patterns = set_int_field(
        machine, self, kFigure, "numPatterns", (*model)->patterns);
    auto stored_textures = set_int_field(
        machine, self, kFigure, "numTextures", 0);
    auto stored_disposed = set_int_field(
        machine, self, kFigure, "disposed", 0, "Z");
    if (!stored_data) return std::unexpected(stored_data.error());
    if (!stored_texture) return std::unexpected(stored_texture.error());
    if (!stored_pattern) return std::unexpected(stored_pattern.error());
    if (!stored_posture_table) {
        return std::unexpected(stored_posture_table.error());
    }
    if (!stored_posture_action) {
        return std::unexpected(stored_posture_action.error());
    }
    if (!stored_posture_frame) {
        return std::unexpected(stored_posture_frame.error());
    }
    if (!stored_patterns) return std::unexpected(stored_patterns.error());
    if (!stored_textures) return std::unexpected(stored_textures.error());
    if (!stored_disposed) {
        return std::unexpected(stored_disposed.error());
    }
    return void_result();
}

void register_figure(NativeMethodRegistry& registry) {
    m3g::add(registry, kFigure, "<init>", "([B)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.<init>");
            auto data = m3g::reference_argument(
                args, 1U, "Figure.<init>", false);
            if (!self) return std::unexpected(self.error());
            if (!data) return std::unexpected(data.error());
            return initialize_figure(machine, *self, *data);
        });
    m3g::add(registry, kFigure, "<init>", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.<init>");
            auto name = m3g::reference_argument(
                args, 1U, "Figure.<init>", false);
            if (!self) return std::unexpected(self.error());
            if (!name) return std::unexpected(name.error());
            auto data = resource_byte_array(
                machine, *name, "Figure.<init>");
            if (!data) return std::unexpected(data.error());
            return initialize_figure(machine, *self, *data);
        });
    m3g::add(registry, kFigure, "dispose", "()V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.dispose");
            if (!self) return std::unexpected(self.error());
            software::erase_resource(machine, *self);
            auto stored_data = set_reference_field(
                machine, *self, kFigure, "data", "[B", {});
            auto stored_textures = set_reference_field(
                machine, *self, kFigure, "textures",
                "[Lcom/mascotcapsule/micro3d/v3/Texture;", {});
            auto stored_posture = set_reference_field(
                machine, *self, kFigure, "postureTable",
                "Lcom/mascotcapsule/micro3d/v3/ActionTable;", {});
            auto stored_disposed = set_int_field(
                machine, *self, kFigure, "disposed", 1, "Z");
            if (!stored_data) return std::unexpected(stored_data.error());
            if (!stored_textures) {
                return std::unexpected(stored_textures.error());
            }
            if (!stored_posture) {
                return std::unexpected(stored_posture.error());
            }
            if (!stored_disposed) {
                return std::unexpected(stored_disposed.error());
            }
            return void_result();
        });

    constexpr std::array<std::pair<const char*, const char*>, 2> getters {{
        {"getNumPattern", "numPatterns"},
        {"getNumTextures", "numTextures"},
    }};
    for (const auto& [method_name, field_name] : getters) {
        m3g::add(registry, kFigure, method_name, "()I",
            [field_name](Machine& machine,
                         std::span<const Value> args) -> NativeResult {
                auto self = m3g::receiver(args, "Figure getter");
                if (!self) return std::unexpected(self.error());
                auto live = require_not_disposed(
                    machine, *self, kFigure, "Figure");
                if (!live) return std::unexpected(live.error());
                auto value = int_field(
                    machine, *self, kFigure, field_name);
                if (!value) return std::unexpected(value.error());
                return int_result(*value);
            });
    }
    m3g::add(registry, kFigure, "setPattern", "(I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.setPattern");
            auto pattern = m3g::int_argument(
                args, 1U, "Figure.setPattern");
            if (!self) return std::unexpected(self.error());
            if (!pattern) return std::unexpected(pattern.error());
            auto live = require_not_disposed(
                machine, *self, kFigure, "Figure");
            if (!live) return std::unexpected(live.error());
            auto stored = set_int_field(
                machine, *self, kFigure, "pattern", *pattern);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });

    m3g::add(registry, kFigure, "setTexture",
             "(Lcom/mascotcapsule/micro3d/v3/Texture;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.setTexture");
            auto texture = m3g::reference_argument(
                args, 1U, "Figure.setTexture", true);
            if (!self) return std::unexpected(self.error());
            if (!texture) return std::unexpected(texture.error());
            auto live = require_not_disposed(
                machine, *self, kFigure, "Figure");
            if (!live) return std::unexpected(live.error());
            if (texture->is_null()) {
                return fail_java("java/lang/NullPointerException",
                                 "Figure texture is null");
            }
            auto for_model = int_field(
                machine, *texture, kTexture, "isForModel", "Z");
            if (!for_model) return std::unexpected(for_model.error());
            if (*for_model == 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Figure texture is not a model texture");
            }
            auto array = m3g::allocate_array(
                machine, "[Lcom/mascotcapsule/micro3d/v3/Texture;", 1U,
                Value::from_reference(ObjectRef {}));
            if (!array) return std::unexpected(array.error());
            auto element = machine.heap().set_element(
                *array, 0U, Value::from_reference(*texture));
            if (!element) return std::unexpected(element.error());
            auto stored_textures = set_reference_field(
                machine, *self, kFigure, "textures",
                "[Lcom/mascotcapsule/micro3d/v3/Texture;", *array);
            auto stored_index = set_int_field(
                machine, *self, kFigure, "textureIndex", 0);
            auto stored_count = set_int_field(
                machine, *self, kFigure, "numTextures", 1);
            if (!stored_textures) {
                return std::unexpected(stored_textures.error());
            }
            if (!stored_index) return std::unexpected(stored_index.error());
            if (!stored_count) return std::unexpected(stored_count.error());
            return void_result();
        });
    m3g::add(registry, kFigure, "setTexture",
             "([Lcom/mascotcapsule/micro3d/v3/Texture;)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.setTexture");
            auto textures = m3g::reference_argument(
                args, 1U, "Figure.setTexture", false);
            if (!self) return std::unexpected(self.error());
            if (!textures) return std::unexpected(textures.error());
            auto class_name = machine.heap().class_name(*textures);
            auto length = machine.heap().array_length(*textures);
            if (!class_name) return std::unexpected(class_name.error());
            if (!length) return std::unexpected(length.error());
            if (*class_name !=
                "[Lcom/mascotcapsule/micro3d/v3/Texture;") {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Figure expects Texture[]");
            }
            if (*length == 0U) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Figure Texture[] is empty");
            }
            for (usize index = 0; index < *length; ++index) {
                auto element = machine.heap().element(*textures, index);
                if (!element) return std::unexpected(element.error());
                auto texture = element->as_reference();
                if (!texture) return std::unexpected(texture.error());
                if (texture->is_null()) {
                    return fail_java("java/lang/NullPointerException",
                                     "Figure Texture[] contains null");
                }
                auto for_model = int_field(
                    machine, *texture, kTexture, "isForModel", "Z");
                if (!for_model) return std::unexpected(for_model.error());
                if (*for_model == 0) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Figure texture is not a model texture");
                }
            }
            if (*length > static_cast<usize>(
                              std::numeric_limits<i32>::max())) {
                return fail(ErrorCode::overflow,
                            "Figure texture count overflows int");
            }
            auto stored_textures = set_reference_field(
                machine, *self, kFigure, "textures",
                "[Lcom/mascotcapsule/micro3d/v3/Texture;", *textures);
            auto stored_index = set_int_field(
                machine, *self, kFigure, "textureIndex", -1);
            auto stored_count = set_int_field(
                machine, *self, kFigure, "numTextures",
                static_cast<i32>(*length));
            if (!stored_textures) {
                return std::unexpected(stored_textures.error());
            }
            if (!stored_index) return std::unexpected(stored_index.error());
            if (!stored_count) return std::unexpected(stored_count.error());
            return void_result();
        });
    m3g::add(registry, kFigure, "selectTexture", "(I)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.selectTexture");
            auto index = m3g::int_argument(
                args, 1U, "Figure.selectTexture");
            if (!self) return std::unexpected(self.error());
            if (!index) return std::unexpected(index.error());
            auto textures = reference_field(
                machine, *self, kFigure, "textures",
                "[Lcom/mascotcapsule/micro3d/v3/Texture;");
            if (!textures) return std::unexpected(textures.error());
            if (textures->is_null() || *index < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid texture selection");
            }
            auto length = machine.heap().array_length(*textures);
            if (!length) return std::unexpected(length.error());
            if (static_cast<usize>(*index) >= *length) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid texture selection");
            }
            auto stored = set_int_field(
                machine, *self, kFigure, "textureIndex", *index);
            if (!stored) return std::unexpected(stored.error());
            return void_result();
        });
    m3g::add(registry, kFigure, "getTexture",
             "()Lcom/mascotcapsule/micro3d/v3/Texture;",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.getTexture");
            if (!self) return std::unexpected(self.error());
            auto live = require_not_disposed(
                machine, *self, kFigure, "Figure");
            if (!live) return std::unexpected(live.error());
            auto index = int_field(
                machine, *self, kFigure, "textureIndex");
            auto textures = reference_field(
                machine, *self, kFigure, "textures",
                "[Lcom/mascotcapsule/micro3d/v3/Texture;");
            if (!index) return std::unexpected(index.error());
            if (!textures) return std::unexpected(textures.error());
            if (*index < 0 || textures->is_null()) return reference_result({});
            auto length = machine.heap().array_length(*textures);
            if (!length) return std::unexpected(length.error());
            if (static_cast<usize>(*index) >= *length) {
                return reference_result({});
            }
            auto element = machine.heap().element(
                *textures, static_cast<usize>(*index));
            if (!element) return std::unexpected(element.error());
            auto reference = element->as_reference();
            if (!reference) return std::unexpected(reference.error());
            return reference_result(*reference);
        });
    m3g::add(registry, kFigure, "setPosture",
             "(Lcom/mascotcapsule/micro3d/v3/ActionTable;II)V",
        [](Machine& machine, std::span<const Value> args) -> NativeResult {
            auto self = m3g::receiver(args, "Figure.setPosture");
            auto table = m3g::reference_argument(
                args, 1U, "Figure.setPosture", false);
            auto action = m3g::int_argument(
                args, 2U, "Figure.setPosture");
            auto frame = m3g::int_argument(
                args, 3U, "Figure.setPosture");
            if (!self) return std::unexpected(self.error());
            if (!table) return std::unexpected(table.error());
            if (!action) return std::unexpected(action.error());
            if (!frame) return std::unexpected(frame.error());
            auto live_figure = require_not_disposed(
                machine, *self, kFigure, "Figure");
            auto live_table = require_not_disposed(
                machine, *table, kActionTable, "ActionTable");
            if (!live_figure) return std::unexpected(live_figure.error());
            if (!live_table) return std::unexpected(live_table.error());
            auto actions = software::cached_actions(machine, *table);
            auto model = software::cached_model(machine, *self);
            if (!actions) return std::unexpected(actions.error());
            if (!model) return std::unexpected(model.error());
            if (*action < 0 ||
                static_cast<usize>(*action) >= (*actions)->actions.size()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid posture action index");
            }
            const auto& selected =
                (*actions)->actions[static_cast<usize>(*action)];
            if (selected.bones.size() != (*model)->bones.size()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "MTRA bone count does not match Figure");
            }
            auto pattern = int_field(machine, *self, kFigure, "pattern");
            if (!pattern) return std::unexpected(pattern.error());
            const i32 normalized_frame = std::max(*frame, 0);
            const i32 next_pattern = software::dynamic_pattern(
                **actions, *action, normalized_frame, *pattern);
            auto stored_table = set_reference_field(
                machine, *self, kFigure, "postureTable",
                "Lcom/mascotcapsule/micro3d/v3/ActionTable;", *table);
            auto stored_action = set_int_field(
                machine, *self, kFigure, "postureAction", *action);
            auto stored_frame = set_int_field(
                machine, *self, kFigure, "postureFrame", normalized_frame);
            auto stored_pattern = set_int_field(
                machine, *self, kFigure, "pattern", next_pattern);
            if (!stored_table) return std::unexpected(stored_table.error());
            if (!stored_action) return std::unexpected(stored_action.error());
            if (!stored_frame) return std::unexpected(stored_frame.error());
            if (!stored_pattern) return std::unexpected(stored_pattern.error());
            return void_result();
        });
}

} // namespace
} // namespace micro3d

void register_micro3d_resource_natives(NativeMethodRegistry& registry) {
    micro3d::register_texture(registry);
    micro3d::register_action_table(registry);
    micro3d::register_figure(registry);
}

} // namespace phoneme::vm
