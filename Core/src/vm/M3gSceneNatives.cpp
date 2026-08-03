#include "M3gNativeModules.hpp"

#include <array>
#include <vector>

#include "M3gNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace m3g;

[[nodiscard]] Result<ObjectRef> animation_array(Machine& machine,
                                                 ObjectRef object) {
    auto array = reference_field(machine, object, kObject3D,
                                 "animationTracks",
                                 "[Ljavax/microedition/m3g/AnimationTrack;");
    if (!array) return std::unexpected(array.error());
    if (!array->is_null()) return *array;
    auto created = allocate_array(machine,
        "[Ljavax/microedition/m3g/AnimationTrack;", 4U,
        Value::from_reference({}));
    if (!created) return std::unexpected(created.error());
    auto stored = set_reference_field(machine, object, kObject3D,
        "animationTracks", "[Ljavax/microedition/m3g/AnimationTrack;",
        *created);
    if (!stored) return std::unexpected(stored.error());
    return *created;
}

[[nodiscard]] Status ensure_animation_capacity(Machine& machine,
                                                ObjectRef object,
                                                i32 minimum) {
    auto array = animation_array(machine, object);
    if (!array) return std::unexpected(array.error());
    auto capacity = machine.heap().array_length(*array);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    const usize new_capacity = std::max(static_cast<usize>(minimum),
        *capacity == 0U ? 4U : *capacity * 2U);
    auto replacement = allocate_array(machine,
        "[Ljavax/microedition/m3g/AnimationTrack;", new_capacity,
        Value::from_reference({}));
    if (!replacement) return std::unexpected(replacement.error());
    auto count = int_field(machine, object, kObject3D,
                           "animationTrackCount");
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*array, static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(
            *replacement, static_cast<usize>(index), *value);
        if (!stored) return stored;
    }
    return set_reference_field(machine, object, kObject3D,
        "animationTracks", "[Ljavax/microedition/m3g/AnimationTrack;",
        *replacement);
}

[[nodiscard]] Result<ObjectRef> child_array(Machine& machine,
                                             ObjectRef object) {
    auto array = reference_field(machine, object, kGroup, "children",
                                 "[Ljavax/microedition/m3g/Node;");
    if (!array) return std::unexpected(array.error());
    if (!array->is_null()) return *array;
    auto initialized = initialize_group(machine, object);
    if (!initialized) return std::unexpected(initialized.error());
    return reference_field(machine, object, kGroup, "children",
                           "[Ljavax/microedition/m3g/Node;");
}

[[nodiscard]] Status ensure_child_capacity(Machine& machine,
                                            ObjectRef object,
                                            i32 minimum) {
    auto array = child_array(machine, object);
    if (!array) return std::unexpected(array.error());
    auto capacity = machine.heap().array_length(*array);
    if (!capacity) return std::unexpected(capacity.error());
    if (minimum <= static_cast<i32>(*capacity)) return {};
    const usize new_capacity = std::max(static_cast<usize>(minimum),
        *capacity == 0U ? 4U : *capacity * 2U);
    auto replacement = allocate_array(machine,
        "[Ljavax/microedition/m3g/Node;", new_capacity,
        Value::from_reference({}));
    if (!replacement) return std::unexpected(replacement.error());
    auto count = int_field(machine, object, kGroup, "childCount");
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*array, static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto stored = machine.heap().set_element(
            *replacement, static_cast<usize>(index), *value);
        if (!stored) return stored;
    }
    return set_reference_field(machine, object, kGroup, "children",
                               "[Ljavax/microedition/m3g/Node;", *replacement);
}

[[nodiscard]] Result<bool> is_descendant(Machine& machine,
                                         ObjectRef candidate,
                                         ObjectRef ancestor) {
    ObjectRef current = candidate;
    for (usize depth = 0; depth < 1'024U && !current.is_null(); ++depth) {
        if (current == ancestor) return true;
        auto parent = reference_field(machine, current, kNode, "parent",
                                      "Ljavax/microedition/m3g/Node;");
        if (!parent) return std::unexpected(parent.error());
        current = *parent;
    }
    return false;
}

[[nodiscard]] Result<Matrix> composite_matrix(Machine& machine,
                                              ObjectRef object) {
    std::vector<ObjectRef> chain;
    ObjectRef current = object;
    for (usize depth = 0; depth < 1'024U && !current.is_null(); ++depth) {
        chain.push_back(current);
        auto parent = reference_field(machine, current, kNode, "parent",
                                      "Ljavax/microedition/m3g/Node;");
        if (!parent) break;
        current = *parent;
    }
    std::reverse(chain.begin(), chain.end());
    Matrix result = identity_matrix();
    for (ObjectRef entry : chain) {
        auto local = local_transform(machine, entry);
        if (!local) return std::unexpected(local.error());
        auto matrix = transform_matrix(machine, *local);
        if (!matrix) return std::unexpected(matrix.error());
        result = multiply(result, *matrix);
    }
    return result;
}

[[nodiscard]] Result<ObjectRef> find_by_user_id(Machine& machine,
                                                 ObjectRef object,
                                                 i32 user_id,
                                                 usize depth = 0U) {
    if (depth > 1'024U) {
        return fail(ErrorCode::overflow, "M3G scene graph is too deep");
    }
    auto current_id = int_field(machine, object, kObject3D, "userID");
    if (!current_id) return std::unexpected(current_id.error());
    if (*current_id == user_id) return object;
    auto is_group = machine.object_is_instance(object, kGroup);
    if (!is_group) return std::unexpected(is_group.error());
    if (!*is_group) return ObjectRef {};
    auto children = child_array(machine, object);
    auto count = int_field(machine, object, kGroup, "childCount");
    if (!children) return std::unexpected(children.error());
    if (!count) return std::unexpected(count.error());
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*children,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto child = value->as_reference();
        if (!child) return std::unexpected(child.error());
        if (child->is_null()) continue;
        auto found = find_by_user_id(machine, *child, user_id, depth + 1U);
        if (!found) return std::unexpected(found.error());
        if (!found->is_null()) return *found;
    }
    return ObjectRef {};
}

void register_object3d(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kObject3D, "()V", initialize_object3d);
    register_int_property(registry, kObject3D, kObject3D, "userID",
                          "setUserID", "getUserID");
    add(registry, kObject3D, "setUserObject", "(Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.setUserObject");
            auto value = reference_argument(arguments, 1U,
                                            "Object3D.setUserObject", true);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_reference_field(machine, *object, kObject3D,
                "userObject", "Ljava/lang/Object;", *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kObject3D, "getUserObject", "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.getUserObject");
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, kObject3D,
                "userObject", "Ljava/lang/Object;");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
    add(registry, kObject3D, "animate", "(I)I",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, kObject3D, "duplicate",
        "()Ljavax/microedition/m3g/Object3D;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.duplicate");
            if (!object) return std::unexpected(object.error());
            auto clone = machine.heap().clone_object(*object);
            if (!clone) return std::unexpected(clone.error());
            return std::optional<Value>(Value::from_reference(*clone));
        });
    add(registry, kObject3D, "find",
        "(I)Ljavax/microedition/m3g/Object3D;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.find");
            auto user_id = int_argument(arguments, 1U, "Object3D.find");
            if (!object) return std::unexpected(object.error());
            if (!user_id) return std::unexpected(user_id.error());
            auto found = find_by_user_id(machine, *object, *user_id);
            if (!found) return std::unexpected(found.error());
            return std::optional<Value>(Value::from_reference(*found));
        });
    add(registry, kObject3D, "getReferences",
        "([Ljavax/microedition/m3g/Object3D;)I",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, kObject3D, "addAnimationTrack",
        "(Ljavax/microedition/m3g/AnimationTrack;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.addAnimationTrack");
            auto track = reference_argument(arguments, 1U,
                                            "Object3D.addAnimationTrack");
            if (!object) return std::unexpected(object.error());
            if (!track) return std::unexpected(track.error());
            auto count = int_field(machine, *object, kObject3D,
                                   "animationTrackCount");
            if (!count) return std::unexpected(count.error());
            auto capacity = ensure_animation_capacity(machine, *object,
                                                       *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto tracks = animation_array(machine, *object);
            if (!tracks) return std::unexpected(tracks.error());
            auto stored = machine.heap().set_element(
                *tracks, static_cast<usize>(*count),
                Value::from_reference(*track));
            if (!stored) return std::unexpected(stored.error());
            auto updated = set_int_field(machine, *object, kObject3D,
                                         "animationTrackCount", *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, kObject3D, "removeAnimationTrack",
        "(Ljavax/microedition/m3g/AnimationTrack;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.removeAnimationTrack");
            auto track = reference_argument(arguments, 1U,
                                            "Object3D.removeAnimationTrack");
            if (!object) return std::unexpected(object.error());
            if (!track) return std::unexpected(track.error());
            auto tracks = animation_array(machine, *object);
            auto count = int_field(machine, *object, kObject3D,
                                   "animationTrackCount");
            if (!tracks) return std::unexpected(tracks.error());
            if (!count) return std::unexpected(count.error());
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*tracks,
                                                    static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto current = value->as_reference();
                if (!current) return std::unexpected(current.error());
                if (*current != *track) continue;
                for (i32 move = index; move + 1 < *count; ++move) {
                    auto next = machine.heap().element(
                        *tracks, static_cast<usize>(move + 1));
                    if (!next) return std::unexpected(next.error());
                    auto shifted = machine.heap().set_element(
                        *tracks, static_cast<usize>(move), *next);
                    if (!shifted) return std::unexpected(shifted.error());
                }
                auto cleared = machine.heap().set_element(
                    *tracks, static_cast<usize>(*count - 1),
                    Value::from_reference({}));
                if (!cleared) return std::unexpected(cleared.error());
                auto updated = set_int_field(machine, *object, kObject3D,
                                             "animationTrackCount", *count - 1);
                if (!updated) return std::unexpected(updated.error());
                break;
            }
            return std::optional<Value> {};
        });
    add(registry, kObject3D, "getAnimationTrack",
        "(I)Ljavax/microedition/m3g/AnimationTrack;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Object3D.getAnimationTrack");
            auto index = int_argument(arguments, 1U,
                                      "Object3D.getAnimationTrack");
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto count = int_field(machine, *object, kObject3D,
                                   "animationTrackCount");
            auto tracks = animation_array(machine, *object);
            if (!count) return std::unexpected(count.error());
            if (!tracks) return std::unexpected(tracks.error());
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "animation track index is out of range");
            }
            auto value = machine.heap().element(*tracks,
                                                static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    register_int_property(registry, kObject3D, kObject3D,
                          "animationTrackCount", "setAnimationTrackCountInternal",
                          "getAnimationTrackCount");
}

void register_node(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kNode, "()V", initialize_node);
    register_int_property(registry, kNode, kNode, "renderingEnabled",
                          "setRenderingEnable", "isRenderingEnabled", "Z");
    register_int_property(registry, kNode, kNode, "pickingEnabled",
                          "setPickingEnable", "isPickingEnabled", "Z");
    register_float_property(registry, kNode, kNode, "alphaFactor",
                            "setAlphaFactor", "getAlphaFactor");
    register_int_property(registry, kNode, kNode, "scope",
                          "setScope", "getScope");
    add(registry, kNode, "getParent", "()Ljavax/microedition/m3g/Node;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Node.getParent");
            if (!object) return std::unexpected(object.error());
            auto parent = reference_field(machine, *object, kNode, "parent",
                                           "Ljavax/microedition/m3g/Node;");
            if (!parent) return std::unexpected(parent.error());
            return std::optional<Value>(Value::from_reference(*parent));
        });
    add(registry, kNode, "setAlignment",
        "(Ljavax/microedition/m3g/Node;ILjavax/microedition/m3g/Node;I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Node.setAlignment");
            auto z_target = reference_argument(arguments, 1U,
                                               "Node.setAlignment", true);
            auto z_reference = int_argument(arguments, 2U, "Node.setAlignment");
            auto y_target = reference_argument(arguments, 3U,
                                               "Node.setAlignment", true);
            auto y_reference = int_argument(arguments, 4U, "Node.setAlignment");
            if (!object) return std::unexpected(object.error());
            if (!z_target || !z_reference || !y_target || !y_reference) {
                return fail(ErrorCode::invalid_argument,
                            "Node alignment arguments are invalid");
            }
            const std::array<Status, 4> stored {
                set_reference_field(machine, *object, kNode, "zTarget",
                    "Ljavax/microedition/m3g/Node;", *z_target),
                set_int_field(machine, *object, kNode, "zReference", *z_reference),
                set_reference_field(machine, *object, kNode, "yTarget",
                    "Ljavax/microedition/m3g/Node;", *y_target),
                set_int_field(machine, *object, kNode, "yReference", *y_reference),
            };
            for (const Status& status : stored) {
                if (!status) return std::unexpected(status.error());
            }
            return std::optional<Value> {};
        });
    add(registry, kNode, "align", "(Ljavax/microedition/m3g/Node;)V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            return std::optional<Value> {};
        });
    add(registry, kNode, "getAlignmentTarget",
        "(I)Ljavax/microedition/m3g/Node;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Node.getAlignmentTarget");
            auto axis = int_argument(arguments, 1U, "Node.getAlignmentTarget");
            if (!object) return std::unexpected(object.error());
            if (!axis) return std::unexpected(axis.error());
            const char* name = *axis == 147 ? "zTarget" : "yTarget";
            auto target = reference_field(machine, *object, kNode, name,
                                           "Ljavax/microedition/m3g/Node;");
            if (!target) return std::unexpected(target.error());
            return std::optional<Value>(Value::from_reference(*target));
        });
    add(registry, kNode, "getAlignmentReference", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Node.getAlignmentReference");
            auto axis = int_argument(arguments, 1U, "Node.getAlignmentReference");
            if (!object) return std::unexpected(object.error());
            if (!axis) return std::unexpected(axis.error());
            const char* name = *axis == 147 ? "zReference" : "yReference";
            auto value = int_field(machine, *object, kNode, name);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value));
        });
    add(registry, kNode, "getTransformTo",
        "(Ljavax/microedition/m3g/Node;Ljavax/microedition/m3g/Transform;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Node.getTransformTo");
            auto target = reference_argument(arguments, 1U,
                                             "Node.getTransformTo");
            auto destination = reference_argument(arguments, 2U,
                                                  "Node.getTransformTo");
            if (!object || !target || !destination) {
                return fail(ErrorCode::invalid_argument,
                            "Node.getTransformTo arguments are invalid");
            }
            auto source_matrix = composite_matrix(machine, *object);
            auto target_matrix = composite_matrix(machine, *target);
            if (!source_matrix) return std::unexpected(source_matrix.error());
            if (!target_matrix) return std::unexpected(target_matrix.error());
            auto inverse = inverse_matrix(*target_matrix);
            if (!inverse) return std::optional<Value>(Value::from_int(0));
            auto stored = set_transform_matrix(
                machine, *destination, multiply(*inverse, *source_matrix));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(1));
        });
}

void register_group(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kGroup, "()V", initialize_group);
    add(registry, kGroup, "addChild", "(Ljavax/microedition/m3g/Node;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Group.addChild");
            auto child = reference_argument(arguments, 1U, "Group.addChild");
            if (!object) return std::unexpected(object.error());
            if (!child) return std::unexpected(child.error());
            if (*child == *object) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Group cannot contain itself");
            }
            auto cycle = is_descendant(machine, *object, *child);
            if (!cycle) return std::unexpected(cycle.error());
            if (*cycle) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Group child would create a cycle");
            }
            auto existing_parent = reference_field(machine, *child, kNode,
                "parent", "Ljavax/microedition/m3g/Node;");
            if (!existing_parent) return std::unexpected(existing_parent.error());
            if (!existing_parent->is_null()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Node already has a parent");
            }
            auto count = int_field(machine, *object, kGroup, "childCount");
            if (!count) return std::unexpected(count.error());
            auto capacity = ensure_child_capacity(machine, *object, *count + 1);
            if (!capacity) return std::unexpected(capacity.error());
            auto children = child_array(machine, *object);
            if (!children) return std::unexpected(children.error());
            auto stored = machine.heap().set_element(
                *children, static_cast<usize>(*count),
                Value::from_reference(*child));
            if (!stored) return std::unexpected(stored.error());
            auto parent = set_reference_field(machine, *child, kNode, "parent",
                "Ljavax/microedition/m3g/Node;", *object);
            if (!parent) return std::unexpected(parent.error());
            auto updated = set_int_field(machine, *object, kGroup,
                                         "childCount", *count + 1);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, kGroup, "removeChild",
        "(Ljavax/microedition/m3g/Node;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Group.removeChild");
            auto child = reference_argument(arguments, 1U,
                                            "Group.removeChild", true);
            if (!object) return std::unexpected(object.error());
            if (!child) return std::unexpected(child.error());
            auto children = child_array(machine, *object);
            auto count = int_field(machine, *object, kGroup, "childCount");
            if (!children) return std::unexpected(children.error());
            if (!count) return std::unexpected(count.error());
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(*children,
                                                    static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto current = value->as_reference();
                if (!current) return std::unexpected(current.error());
                if (*current != *child) continue;
                for (i32 move = index; move + 1 < *count; ++move) {
                    auto next = machine.heap().element(
                        *children, static_cast<usize>(move + 1));
                    if (!next) return std::unexpected(next.error());
                    auto shifted = machine.heap().set_element(
                        *children, static_cast<usize>(move), *next);
                    if (!shifted) return std::unexpected(shifted.error());
                }
                auto cleared = machine.heap().set_element(
                    *children, static_cast<usize>(*count - 1),
                    Value::from_reference({}));
                if (!cleared) return std::unexpected(cleared.error());
                if (!child->is_null()) {
                    auto parent = set_reference_field(machine, *child, kNode,
                        "parent", "Ljavax/microedition/m3g/Node;", {});
                    if (!parent) return std::unexpected(parent.error());
                }
                auto updated = set_int_field(machine, *object, kGroup,
                                             "childCount", *count - 1);
                if (!updated) return std::unexpected(updated.error());
                return std::optional<Value> {};
            }
            return std::optional<Value> {};
        });
    add(registry, kGroup, "getChild", "(I)Ljavax/microedition/m3g/Node;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Group.getChild");
            auto index = int_argument(arguments, 1U, "Group.getChild");
            if (!object) return std::unexpected(object.error());
            if (!index) return std::unexpected(index.error());
            auto count = int_field(machine, *object, kGroup, "childCount");
            auto children = child_array(machine, *object);
            if (!count) return std::unexpected(count.error());
            if (!children) return std::unexpected(children.error());
            if (*index < 0 || *index >= *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Group child index is out of range");
            }
            auto value = machine.heap().element(*children,
                                                static_cast<usize>(*index));
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(*value);
        });
    register_int_property(registry, kGroup, kGroup, "childCount",
                          "setChildCountInternal", "getChildCount");
    const auto pick = [&registry](const char* descriptor) {
        add(registry, kGroup, "pick", descriptor,
            [](Machine&, std::span<const Value>)
                -> Result<std::optional<Value>> {
                return std::optional<Value>(Value::from_int(0));
            });
    };
    pick("(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z");
    pick("(IFFFFLjavax/microedition/m3g/Camera;Ljavax/microedition/m3g/RayIntersection;)Z");
}

void register_world(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, kWorld, "()V", initialize_group);
    register_reference_property(registry, kWorld, kWorld, "activeCamera",
        "Ljavax/microedition/m3g/Camera;", "setActiveCamera",
        "getActiveCamera");
    add(registry, kWorld, "setBackground",
        "(Ljavax/microedition/m3g/Background;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "World.setBackground");
            auto value = reference_argument(arguments, 1U,
                                            "World.setBackground", true);
            if (!object) return std::unexpected(object.error());
            if (!value) return std::unexpected(value.error());
            auto stored = set_reference_field(machine, *object, kWorld,
                "background", "Ljavax/microedition/m3g/Background;", *value);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, kWorld, "getBackground",
        "()Ljavax/microedition/m3g/Background;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "World.getBackground");
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, kWorld,
                "background", "Ljavax/microedition/m3g/Background;");
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });
}

void register_camera(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, "javax/microedition/m3g/Camera",
                              "()V", initialize_node);
    const auto projection = [&registry](const char* name, i32 type) {
        add(registry, "javax/microedition/m3g/Camera", name, "(FFFF)V",
            [type](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Camera projection");
                if (!object) return std::unexpected(object.error());
                std::array<float, 4> values {};
                for (usize index = 0; index < values.size(); ++index) {
                    auto value = float_argument(arguments, index + 1U,
                                                "Camera projection");
                    if (!value) return std::unexpected(value.error());
                    values[index] = *value;
                }
                auto array = float_array(machine, values);
                if (!array) return std::unexpected(array.error());
                auto type_stored = set_int_field(machine, *object,
                    "javax/microedition/m3g/Camera", "projectionType", type);
                auto values_stored = set_reference_field(machine, *object,
                    "javax/microedition/m3g/Camera", "projection", "[F", *array);
                if (!type_stored) return std::unexpected(type_stored.error());
                if (!values_stored) return std::unexpected(values_stored.error());
                return std::optional<Value> {};
            });
    };
    projection("setParallel", 48);
    projection("setPerspective", 50);
    add(registry, "javax/microedition/m3g/Camera", "setGeneric",
        "(Ljavax/microedition/m3g/Transform;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Camera.setGeneric");
            auto source = reference_argument(arguments, 1U, "Camera.setGeneric");
            if (!object) return std::unexpected(object.error());
            if (!source) return std::unexpected(source.error());
            auto matrix = transform_matrix(machine, *source);
            if (!matrix) return std::unexpected(matrix.error());
            auto array = float_array(machine, *matrix);
            if (!array) return std::unexpected(array.error());
            auto type_stored = set_int_field(machine, *object,
                "javax/microedition/m3g/Camera", "projectionType", 52);
            auto values_stored = set_reference_field(machine, *object,
                "javax/microedition/m3g/Camera", "projection", "[F", *array);
            if (!type_stored) return std::unexpected(type_stored.error());
            if (!values_stored) return std::unexpected(values_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/m3g/Camera", "getProjection",
        "([F)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Camera.getProjection");
            auto destination = reference_argument(arguments, 1U,
                                                  "Camera.getProjection", true);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto type = int_field(machine, *object,
                                  "javax/microedition/m3g/Camera",
                                  "projectionType");
            if (!type) return std::unexpected(type.error());
            if (!destination->is_null()) {
                auto array = reference_field(machine, *object,
                    "javax/microedition/m3g/Camera", "projection", "[F");
                if (!array) return std::unexpected(array.error());
                if (!array->is_null()) {
                    auto values = read_float_array(machine, *array,
                                                   "Camera.getProjection");
                    if (!values) return std::unexpected(values.error());
                    auto stored = write_float_array(machine, *destination,
                                                    *values);
                    if (!stored) return std::unexpected(stored.error());
                }
            }
            return std::optional<Value>(Value::from_int(*type));
        });
    add(registry, "javax/microedition/m3g/Camera", "getProjection",
        "(Ljavax/microedition/m3g/Transform;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Camera.getProjection");
            auto destination = reference_argument(arguments, 1U,
                                                  "Camera.getProjection", true);
            if (!object) return std::unexpected(object.error());
            if (!destination) return std::unexpected(destination.error());
            auto type = int_field(machine, *object,
                                  "javax/microedition/m3g/Camera",
                                  "projectionType");
            if (!type) return std::unexpected(type.error());
            if (!destination->is_null()) {
                Matrix matrix = identity_matrix();
                auto array = reference_field(machine, *object,
                    "javax/microedition/m3g/Camera", "projection", "[F");
                if (!array) return std::unexpected(array.error());
                if (*type == 52 && !array->is_null()) {
                    auto values = read_float_array(machine, *array,
                                                   "Camera.getProjection");
                    if (!values) return std::unexpected(values.error());
                    if (values->size() >= 16U) {
                        std::copy_n(values->begin(), 16, matrix.begin());
                    }
                }
                auto stored = set_transform_matrix(machine, *destination, matrix);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value>(Value::from_int(*type));
        });
}

void register_light(NativeMethodRegistry& registry) {
    register_noop_constructor(registry, "javax/microedition/m3g/Light",
                              "()V", initialize_node);
    register_int_property(registry, "javax/microedition/m3g/Light",
                          "javax/microedition/m3g/Light", "mode",
                          "setMode", "getMode");
    register_float_property(registry, "javax/microedition/m3g/Light",
                            "javax/microedition/m3g/Light", "intensity",
                            "setIntensity", "getIntensity");
    register_int_property(registry, "javax/microedition/m3g/Light",
                          "javax/microedition/m3g/Light", "color",
                          "setColor", "getColor");
    register_float_property(registry, "javax/microedition/m3g/Light",
                            "javax/microedition/m3g/Light", "spotAngle",
                            "setSpotAngle", "getSpotAngle");
    register_float_property(registry, "javax/microedition/m3g/Light",
                            "javax/microedition/m3g/Light", "spotExponent",
                            "setSpotExponent", "getSpotExponent");
    add(registry, "javax/microedition/m3g/Light", "setAttenuation", "(FFF)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments, "Light.setAttenuation");
            if (!object) return std::unexpected(object.error());
            std::array<float, 3> values {};
            for (usize index = 0; index < values.size(); ++index) {
                auto value = float_argument(arguments, index + 1U,
                                            "Light.setAttenuation");
                if (!value) return std::unexpected(value.error());
                values[index] = *value;
            }
            auto array = float_array(machine, values);
            if (!array) return std::unexpected(array.error());
            auto stored = set_reference_field(machine, *object,
                "javax/microedition/m3g/Light", "attenuation", "[F", *array);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    const auto attenuation_getter = [&registry](const char* name, usize index) {
        add(registry, "javax/microedition/m3g/Light", name, "()F",
            [index](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments, "Light attenuation getter");
                if (!object) return std::unexpected(object.error());
                auto array = reference_field(machine, *object,
                    "javax/microedition/m3g/Light", "attenuation", "[F");
                if (!array) return std::unexpected(array.error());
                if (array->is_null()) {
                    return std::optional<Value>(Value::from_float(index == 0U
                        ? 1.0F : 0.0F));
                }
                auto value = machine.heap().element(*array, index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(*value);
            });
    };
    attenuation_getter("getConstantAttenuation", 0U);
    attenuation_getter("getLinearAttenuation", 1U);
    attenuation_getter("getQuadraticAttenuation", 2U);
}

} // namespace

void register_m3g_scene_natives(NativeMethodRegistry& registry) {
    register_object3d(registry);
    register_node(registry);
    register_group(registry);
    register_world(registry);
    register_camera(registry);
    register_light(registry);
}

} // namespace phoneme::vm
