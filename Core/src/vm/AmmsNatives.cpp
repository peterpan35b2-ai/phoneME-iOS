#include "AmmsNatives.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr std::string_view kGlobalManager =
    "javax/microedition/amms/GlobalManager";
constexpr std::string_view kEqualizer =
    "phoneme/amms/EqualizerControlImpl";
constexpr i32 kBands = 5;
constexpr i32 kMinimumLevel = -1200;
constexpr i32 kMaximumLevel = 1200;

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

[[nodiscard]] Result<ObjectRef> receiver(std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument, "AMMS receiver is missing");
    }
    auto object = arguments.front().as_reference();
    if (!object) return std::unexpected(object.error());
    if (object->is_null()) {
        return fail_java("java/lang/NullPointerException", "AMMS receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "AMMS reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!allow_null && value->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "AMMS reference argument is null");
    }
    return *value;
}

[[nodiscard]] Result<i32> int_argument(std::span<const Value> arguments,
                                       usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "AMMS integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<ObjectRef> allocate_instance(Machine& machine,
                                                  std::string_view class_name) {
    auto object = machine.class_states().allocate_instance(machine.heap(),
                                                           class_name);
    if (object || object.error().code != ErrorCode::overflow) return object;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.class_states().allocate_instance(machine.heap(), class_name);
}

[[nodiscard]] Result<ObjectRef> allocate_array(Machine& machine,
                                               std::string descriptor,
                                               usize length,
                                               Value initial) {
    auto array = machine.heap().allocate_array(descriptor, length, initial);
    if (array || array.error().code != ErrorCode::overflow) return array;
    auto collected = machine.collect_garbage();
    if (!collected) return std::unexpected(collected.error());
    return machine.heap().allocate_array(descriptor, length, initial);
}

[[nodiscard]] Result<ObjectRef> make_string(Machine& machine,
                                            std::u16string text) {
    auto object = allocate_instance(machine, "java/lang/String");
    if (!object) return std::unexpected(object.error());
    auto attached = machine.heap().attach_string(*object, std::move(text));
    if (!attached) return std::unexpected(attached.error());
    return *object;
}

[[nodiscard]] Result<FieldLocation> field_location(
    Machine& machine,
    std::string_view name,
    std::string_view descriptor) {
    return machine.class_states().resolve_field(kEqualizer, name, descriptor,
                                                false);
}

[[nodiscard]] Status set_field(Machine& machine,
                               ObjectRef object,
                               std::string_view name,
                               std::string_view descriptor,
                               Value value) {
    auto location = field_location(machine, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().set_field(object, location->index, value);
}

[[nodiscard]] Result<Value> field_value(Machine& machine,
                                        ObjectRef object,
                                        std::string_view name,
                                        std::string_view descriptor) {
    auto location = field_location(machine, name, descriptor);
    if (!location) return std::unexpected(location.error());
    return machine.heap().field(object, location->index);
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                ObjectRef object,
                                                std::string_view name,
                                                std::string_view descriptor) {
    auto value = field_value(machine, object, name, descriptor);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Result<i32> integer_field(Machine& machine,
                                        ObjectRef object,
                                        std::string_view name) {
    auto value = field_value(machine, object, name, "I");
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<ObjectRef> create_equalizer(Machine& machine) {
    auto equalizer = allocate_instance(machine, kEqualizer);
    if (!equalizer) return std::unexpected(equalizer.error());
    auto root = machine.pin_native_root(*equalizer);
    if (!root) return std::unexpected(root.error());
    auto levels = allocate_array(machine, "[I", kBands, Value::from_int(0));
    auto preset = make_string(machine, u"flat");
    if (!levels || !preset) {
        if (!levels) return std::unexpected(levels.error());
        return std::unexpected(preset.error());
    }
    auto levels_root = machine.pin_native_root(*levels);
    auto preset_root = machine.pin_native_root(*preset);
    if (!levels_root || !preset_root) {
        return fail(ErrorCode::internal_error,
                    "failed to root AMMS equalizer state");
    }
    const std::array<Status, 5> stored {{
        set_field(machine, *equalizer, "levels", "[I",
                  Value::from_reference(*levels)),
        set_field(machine, *equalizer, "enabled", "I", Value::from_int(0)),
        set_field(machine, *equalizer, "enforced", "I", Value::from_int(0)),
        set_field(machine, *equalizer, "scope", "I", Value::from_int(3)),
        set_field(machine, *equalizer, "preset", "Ljava/lang/String;",
                  Value::from_reference(*preset)),
    }};
    for (const auto& status : stored) {
        if (!status) return std::unexpected(status.error());
    }
    return *equalizer;
}

[[nodiscard]] Result<ObjectRef> levels_array(Machine& machine,
                                             ObjectRef equalizer) {
    return reference_field(machine, equalizer, "levels", "[I");
}

[[nodiscard]] Status validate_band(i32 band) {
    if (band < 0 || band >= kBands) {
        return fail_java("java/lang/IllegalArgumentException",
                         "equalizer band is out of range");
    }
    return {};
}

void register_global_manager(NativeMethodRegistry& registry) {
    add(registry, std::string(kGlobalManager), "getControl",
        "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto name = reference_argument(arguments, 0U);
            if (!name) return std::unexpected(name.error());
            if (name->is_null()) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto text = machine.heap().string_value(*name);
            if (!text) return std::unexpected(text.error());
            if (text->find(u"EqualizerControl") == std::u16string::npos) {
                return std::optional<Value>(Value::from_reference({}));
            }
            auto equalizer = create_equalizer(machine);
            if (!equalizer) return std::unexpected(equalizer.error());
            return std::optional<Value>(Value::from_reference(*equalizer));
        });
    add(registry, std::string(kGlobalManager), "getControls",
        "()[Ljavax/microedition/media/Control;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto equalizer = create_equalizer(machine);
            if (!equalizer) return std::unexpected(equalizer.error());
            auto root = machine.pin_native_root(*equalizer);
            if (!root) return std::unexpected(root.error());
            auto controls = allocate_array(
                machine, "[Ljavax/microedition/media/Control;", 1U,
                Value::from_reference({}));
            if (!controls) return std::unexpected(controls.error());
            auto stored = machine.heap().set_element(
                *controls, 0U, Value::from_reference(*equalizer));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*controls));
        });
}

void register_equalizer(NativeMethodRegistry& registry) {
    add(registry, std::string(kEqualizer), "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            if (!equalizer) return std::unexpected(equalizer.error());
            auto levels = allocate_array(machine, "[I", kBands,
                                         Value::from_int(0));
            auto preset = make_string(machine, u"flat");
            if (!levels || !preset) {
                if (!levels) return std::unexpected(levels.error());
                return std::unexpected(preset.error());
            }
            auto a = set_field(machine, *equalizer, "levels", "[I",
                               Value::from_reference(*levels));
            auto b = set_field(machine, *equalizer, "enabled", "I",
                               Value::from_int(0));
            auto c = set_field(machine, *equalizer, "enforced", "I",
                               Value::from_int(0));
            auto d = set_field(machine, *equalizer, "scope", "I",
                               Value::from_int(3));
            auto e = set_field(machine, *equalizer, "preset",
                               "Ljava/lang/String;",
                               Value::from_reference(*preset));
            if (!a) return std::unexpected(a.error());
            if (!b) return std::unexpected(b.error());
            if (!c) return std::unexpected(c.error());
            if (!d) return std::unexpected(d.error());
            if (!e) return std::unexpected(e.error());
            return std::optional<Value> {};
        });
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"isEnabled",
                                                           "enabled"},
             {"isEnforced", "enforced"}}) {
        add(registry, std::string(kEqualizer), std::string(method), "()Z",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto equalizer = receiver(arguments);
                if (!equalizer) return std::unexpected(equalizer.error());
                auto value = integer_field(machine, *equalizer, field);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value != 0));
            });
    }
    for (const auto& [method, field] : {
             std::pair<std::string_view, std::string_view>{"setEnabled",
                                                           "enabled"},
             {"setEnforced", "enforced"}}) {
        add(registry, std::string(kEqualizer), std::string(method), "(Z)V",
            [field](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto equalizer = receiver(arguments);
                auto value = int_argument(arguments, 1U);
                if (!equalizer || !value) {
                    if (!equalizer) return std::unexpected(equalizer.error());
                    return std::unexpected(value.error());
                }
                auto stored = set_field(machine, *equalizer, field, "I",
                                        Value::from_int(*value != 0));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    }
    add(registry, std::string(kEqualizer), "getScope", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            if (!equalizer) return std::unexpected(equalizer.error());
            auto scope = integer_field(machine, *equalizer, "scope");
            if (!scope) return std::unexpected(scope.error());
            return std::optional<Value>(Value::from_int(*scope));
        });
    add(registry, std::string(kEqualizer), "setScope", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto scope = int_argument(arguments, 1U);
            if (!equalizer || !scope) {
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::unexpected(scope.error());
            }
            if (*scope < 1 || *scope > 3) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid AMMS effect scope");
            }
            auto stored = set_field(machine, *equalizer, "scope", "I",
                                    Value::from_int(*scope));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kEqualizer), "getPreset",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            if (!equalizer) return std::unexpected(equalizer.error());
            auto preset = reference_field(machine, *equalizer, "preset",
                                          "Ljava/lang/String;");
            if (!preset) return std::unexpected(preset.error());
            return std::optional<Value>(Value::from_reference(*preset));
        });
    add(registry, std::string(kEqualizer), "getPresetNames",
        "()[Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            if (!equalizer) return std::unexpected(equalizer.error());
            auto flat = make_string(machine, u"flat");
            if (!flat) return std::unexpected(flat.error());
            auto root = machine.pin_native_root(*flat);
            if (!root) return std::unexpected(root.error());
            auto names = allocate_array(machine, "[Ljava/lang/String;", 1U,
                                        Value::from_reference({}));
            if (!names) return std::unexpected(names.error());
            auto stored = machine.heap().set_element(
                *names, 0U, Value::from_reference(*flat));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*names));
        });
    add(registry, std::string(kEqualizer), "setPreset",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto preset = reference_argument(arguments, 1U, false);
            if (!equalizer || !preset) {
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::unexpected(preset.error());
            }
            auto text = machine.heap().string_value(*preset);
            if (!text) return std::unexpected(text.error());
            if (*text != u"flat") {
                return fail_java("java/lang/IllegalArgumentException",
                                 "unsupported equalizer preset");
            }
            auto stored = set_field(machine, *equalizer, "preset",
                                    "Ljava/lang/String;",
                                    Value::from_reference(*preset));
            if (!stored) return std::unexpected(stored.error());
            auto levels = levels_array(machine, *equalizer);
            if (!levels) return std::unexpected(levels.error());
            for (usize index = 0; index < static_cast<usize>(kBands); ++index) {
                auto reset = machine.heap().set_element(*levels, index,
                                                        Value::from_int(0));
                if (!reset) return std::unexpected(reset.error());
            }
            return std::optional<Value> {};
        });
    for (const auto& [method, value] : {
             std::pair<std::string_view, i32>{"getNumberOfBands", kBands},
             {"getMinBandLevel", kMinimumLevel},
             {"getMaxBandLevel", kMaximumLevel}}) {
        add(registry, std::string(kEqualizer), std::string(method), "()I",
            [value](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto equalizer = receiver(arguments);
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::optional<Value>(Value::from_int(value));
            });
    }
    add(registry, std::string(kEqualizer), "getBandLevel", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto band = int_argument(arguments, 1U);
            if (!equalizer || !band) {
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::unexpected(band.error());
            }
            auto valid = validate_band(*band);
            if (!valid) return std::unexpected(valid.error());
            auto levels = levels_array(machine, *equalizer);
            if (!levels) return std::unexpected(levels.error());
            auto value = machine.heap().element(*levels,
                                                static_cast<usize>(*band));
            if (!value) return std::unexpected(value.error());
            auto level = value->as_int();
            if (!level) return std::unexpected(level.error());
            return std::optional<Value>(Value::from_int(*level));
        });
    add(registry, std::string(kEqualizer), "setBandLevel", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto band = int_argument(arguments, 1U);
            auto level = int_argument(arguments, 2U);
            if (!equalizer || !band || !level) {
                if (!equalizer) return std::unexpected(equalizer.error());
                if (!band) return std::unexpected(band.error());
                return std::unexpected(level.error());
            }
            auto valid = validate_band(*band);
            if (!valid) return std::unexpected(valid.error());
            if (*level < kMinimumLevel || *level > kMaximumLevel) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "equalizer level is out of range");
            }
            auto levels = levels_array(machine, *equalizer);
            if (!levels) return std::unexpected(levels.error());
            auto stored = machine.heap().set_element(
                *levels, static_cast<usize>(*band), Value::from_int(*level));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kEqualizer), "getCenterFreq", "(I)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto band = int_argument(arguments, 1U);
            if (!equalizer || !band) {
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::unexpected(band.error());
            }
            auto valid = validate_band(*band);
            if (!valid) return std::unexpected(valid.error());
            constexpr std::array<i32, kBands> frequencies {{
                60'000, 230'000, 910'000, 3'600'000, 14'000'000,
            }};
            return std::optional<Value>(Value::from_int(
                frequencies[static_cast<usize>(*band)]));
        });
    add(registry, std::string(kEqualizer), "getBand", "(I)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto equalizer = receiver(arguments);
            auto frequency = int_argument(arguments, 1U);
            if (!equalizer || !frequency) {
                if (!equalizer) return std::unexpected(equalizer.error());
                return std::unexpected(frequency.error());
            }
            constexpr std::array<i32, kBands> frequencies {{
                60'000, 230'000, 910'000, 3'600'000, 14'000'000,
            }};
            usize best = 0U;
            i64 best_delta = std::numeric_limits<i64>::max();
            for (usize index = 0; index < frequencies.size(); ++index) {
                const i64 delta = std::abs(static_cast<i64>(*frequency) -
                                           frequencies[index]);
                if (delta < best_delta) {
                    best_delta = delta;
                    best = index;
                }
            }
            return std::optional<Value>(Value::from_int(static_cast<i32>(best)));
        });
    for (const auto& [method, band] : {
             std::pair<std::string_view, i32>{"setBass", 0},
             {"setTreble", kBands - 1}}) {
        add(registry, std::string(kEqualizer), std::string(method), "(I)V",
            [band](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto equalizer = receiver(arguments);
                auto level = int_argument(arguments, 1U);
                if (!equalizer || !level) {
                    if (!equalizer) return std::unexpected(equalizer.error());
                    return std::unexpected(level.error());
                }
                const i32 clamped = std::clamp(*level, kMinimumLevel,
                                               kMaximumLevel);
                auto levels = levels_array(machine, *equalizer);
                if (!levels) return std::unexpected(levels.error());
                auto stored = machine.heap().set_element(
                    *levels, static_cast<usize>(band), Value::from_int(clamped));
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    }
}

} // namespace

void register_amms_natives(NativeMethodRegistry& registry) {
    register_global_manager(registry);
    register_equalizer(registry);
}

} // namespace phoneme::vm
