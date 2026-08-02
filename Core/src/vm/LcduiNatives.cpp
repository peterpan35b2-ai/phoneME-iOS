#include "LcduiNatives.hpp"
#include "ChoiceNatives.hpp"
#include "phoneme/vm/LcduiBridge.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {
namespace {

constexpr i32 kEventScreenCreated = 2;
constexpr i32 kEventScreenUpdated = 3;
constexpr i32 kEventScreenShown = 4;
constexpr i32 kEventScreenHidden = 5;
constexpr i32 kEventItemCreated = 7;
constexpr i32 kEventItemUpdated = 8;
constexpr i32 kEventItemShown = 9;
constexpr i32 kEventItemHidden = 10;
constexpr i32 kEventItemDeleted = 11;
constexpr i32 kEventCommandsReset = 14;
constexpr i32 kEventCommand = 15;

constexpr i32 kTypeDateField = 5;
constexpr i32 kTypeProgressGauge = 6;
constexpr i32 kTypeInteractiveGauge = 7;
constexpr i32 kTypeSpacer = 11;
constexpr i32 kTypePlainString = 12;
constexpr i32 kTypeHyperlinkString = 13;
constexpr i32 kTypeButtonString = 14;
constexpr i32 kTypeTextField = 15;
constexpr i32 kTypeForm = 23;

constexpr i32 kTextFieldMetadata = -1001;
constexpr i32 kGaugeMetadata = -1002;
constexpr i32 kDateFieldMetadata = -1003;
constexpr i32 kScreenKindMetadata = -1006;
constexpr i32 kScreenKindTextBox = 2;

constexpr usize kCommandIdField = 0;
constexpr usize kCommandLabelField = 1;
constexpr usize kCommandLongLabelField = 2;
constexpr usize kCommandTypeField = 3;
constexpr usize kCommandPriorityField = 4;

constexpr usize kDisplayableIdField = 0;
constexpr usize kDisplayableTypeField = 1;
constexpr usize kDisplayableTitleField = 2;
constexpr usize kDisplayableListenerField = 3;
constexpr usize kDisplayableCommandsField = 4;
constexpr usize kDisplayableCommandCountField = 5;
constexpr usize kDisplayableShownField = 6;

constexpr usize kDisplayCurrentField = 0;

constexpr usize kItemIdField = 0;
constexpr usize kItemTypeField = 1;
constexpr usize kItemLabelField = 2;
constexpr usize kItemParentField = 3;
constexpr usize kItemLayoutField = 4;
constexpr usize kItemListenerField = 5;

constexpr usize kFormItemsField = 7;
constexpr usize kFormItemCountField = 8;

constexpr usize kStringItemTextField = 6;
constexpr usize kStringItemAppearanceField = 7;

constexpr usize kTextFieldTextField = 6;
constexpr usize kTextFieldMaxSizeField = 7;
constexpr usize kTextFieldConstraintsField = 8;
constexpr usize kTextFieldCaretField = 9;
constexpr usize kTextFieldInputModeField = 10;

constexpr usize kGaugeInteractiveField = 6;
constexpr usize kGaugeMaxValueField = 7;
constexpr usize kGaugeValueField = 8;

constexpr usize kDateFieldDateField = 6;
constexpr usize kDateFieldInputModeField = 7;
constexpr usize kDateFieldTimeZoneField = 8;

constexpr usize kSpacerWidthField = 6;
constexpr usize kSpacerHeightField = 7;

constexpr usize kTextBoxPeerIdField = 7;
constexpr usize kTextBoxTextField = 8;
constexpr usize kTextBoxMaxSizeField = 9;
constexpr usize kTextBoxConstraintsField = 10;
constexpr usize kTextBoxCaretField = 11;
constexpr usize kTextBoxInputModeField = 12;

constexpr usize kDateTimeField = 0;
constexpr usize kTimeZoneIdField = 0;

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
        return fail(ErrorCode::invalid_argument,
                    "LCDUI native method is missing its receiver");
    }
    auto object = arguments.front().as_reference();
    if (!object || object->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "LCDUI native receiver is null");
    }
    return *object;
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "LCDUI native reference argument is missing");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) return std::unexpected(reference.error());
    if (!allow_null && reference->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "LCDUI reference argument is null");
    }
    return *reference;
}

[[nodiscard]] Result<i32> integer_argument(std::span<const Value> arguments,
                                           usize index) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "LCDUI native integer argument is missing");
    }
    return arguments[index].as_int();
}

[[nodiscard]] Result<Value> field_value(Machine& machine,
                                        ObjectRef object,
                                        usize index) {
    return machine.heap().field(object, index);
}

[[nodiscard]] Result<i32> int_field(Machine& machine,
                                    ObjectRef object,
                                    usize index) {
    auto value = field_value(machine, object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_int();
}

[[nodiscard]] Result<ObjectRef> reference_field(Machine& machine,
                                                ObjectRef object,
                                                usize index) {
    auto value = field_value(machine, object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Result<i64> long_field(Machine& machine,
                                     ObjectRef object,
                                     usize index) {
    auto value = field_value(machine, object, index);
    if (!value) return std::unexpected(value.error());
    return value->as_long();
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

[[nodiscard]] Status set_long_field(Machine& machine,
                                    ObjectRef object,
                                    usize index,
                                    i64 value) {
    return machine.heap().set_field(object, index, Value::from_long(value));
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

[[nodiscard]] Result<ObjectRef> empty_string(Machine& machine) {
    return create_string(machine, {});
}

[[nodiscard]] Result<std::u16string> string_text(Machine& machine,
                                                 ObjectRef string,
                                                 bool null_as_empty = true) {
    if (string.is_null()) {
        if (null_as_empty) return std::u16string {};
        return fail_java("java/lang/NullPointerException",
                         "LCDUI String argument is null");
    }
    return machine.heap().string_value(string);
}

void append_utf8(std::string& output, u32 code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] std::string utf8_text(std::u16string_view text) {
    std::string output;
    output.reserve(text.size() * 2U);
    for (usize index = 0; index < text.size(); ++index) {
        u32 code_point = static_cast<u16>(text[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text.size()) {
            const u32 low = static_cast<u16>(text[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                             (low - 0xDC00U);
                ++index;
            } else {
                code_point = 0xFFFDU;
            }
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        append_utf8(output, code_point);
    }
    return output;
}

[[nodiscard]] Result<std::string> utf8_string(Machine& machine,
                                              ObjectRef string,
                                              bool null_as_empty = true) {
    auto text = string_text(machine, string, null_as_empty);
    if (!text) return std::unexpected(text.error());
    return utf8_text(*text);
}

[[nodiscard]] Result<i32> ensure_native_id(Machine& machine,
                                           ObjectRef object,
                                           usize field_index) {
    auto current = int_field(machine, object, field_index);
    if (!current) return std::unexpected(current.error());
    if (*current != 0) {
        auto registered = machine.register_ui_component(*current, object);
        if (!registered) return std::unexpected(registered.error());
        return *current;
    }
    const i32 allocated = machine.allocate_ui_component_id();
    if (allocated == 0) {
        return fail_java("java/lang/OutOfMemoryError",
                         "LCDUI component ID space is exhausted");
    }
    auto stored = set_int_field(machine, object, field_index, allocated);
    if (!stored) return std::unexpected(stored.error());
    auto registered = machine.register_ui_component(allocated, object);
    if (!registered) return std::unexpected(registered.error());
    return allocated;
}

[[nodiscard]] Result<ObjectRef> ensure_reference_array(
    Machine& machine,
    ObjectRef owner,
    usize field_index,
    std::string class_name,
    usize minimum_capacity) {
    auto current = reference_field(machine, owner, field_index);
    if (!current) return std::unexpected(current.error());
    usize current_capacity = 0;
    if (!current->is_null()) {
        auto length = machine.heap().array_length(*current);
        if (!length) return std::unexpected(length.error());
        current_capacity = *length;
        if (current_capacity >= minimum_capacity) return *current;
    }

    usize capacity = std::max<usize>(4U, current_capacity);
    while (capacity < minimum_capacity) {
        if (capacity > std::numeric_limits<usize>::max() / 2U) {
            return fail_java("java/lang/OutOfMemoryError",
                             "LCDUI array capacity overflow");
        }
        capacity *= 2U;
    }
    auto replacement = machine.heap().allocate_array(
        std::move(class_name), capacity, Value::from_reference({}));
    if (!replacement) {
        if (replacement.error().code == ErrorCode::overflow) {
            return fail_java("java/lang/OutOfMemoryError",
                             "LCDUI array allocation failed");
        }
        return std::unexpected(replacement.error());
    }
    if (!current->is_null()) {
        for (usize index = 0; index < current_capacity; ++index) {
            auto value = machine.heap().element(*current, index);
            if (!value) return std::unexpected(value.error());
            auto stored = machine.heap().set_element(*replacement, index, *value);
            if (!stored) return std::unexpected(stored.error());
        }
    }
    auto assigned = set_reference_field(machine, owner, field_index,
                                        *replacement);
    if (!assigned) return std::unexpected(assigned.error());
    return *replacement;
}

[[nodiscard]] Result<bool> is_shown(Machine& machine,
                                    ObjectRef displayable) {
    auto shown = int_field(machine, displayable, kDisplayableShownField);
    if (!shown) return std::unexpected(shown.error());
    return *shown != 0;
}

[[nodiscard]] Status initialize_displayable(Machine& machine,
                                            ObjectRef object,
                                            i32 type,
                                            ObjectRef title) {
    auto id = ensure_native_id(machine, object, kDisplayableIdField);
    if (!id) return std::unexpected(id.error());
    if (title.is_null()) {
        auto empty = empty_string(machine);
        if (!empty) return std::unexpected(empty.error());
        title = *empty;
    }
    auto type_stored = set_int_field(machine, object,
                                     kDisplayableTypeField, type);
    auto title_stored = set_reference_field(machine, object,
                                            kDisplayableTitleField, title);
    auto listener_stored = set_reference_field(machine, object,
                                               kDisplayableListenerField, {});
    auto count_stored = set_int_field(machine, object,
                                      kDisplayableCommandCountField, 0);
    auto shown_stored = set_int_field(machine, object,
                                      kDisplayableShownField, 0);
    if (!type_stored) return type_stored;
    if (!title_stored) return title_stored;
    if (!listener_stored) return listener_stored;
    if (!count_stored) return count_stored;
    if (!shown_stored) return shown_stored;
    auto commands = ensure_reference_array(
        machine, object, kDisplayableCommandsField,
        "[Ljavax/microedition/lcdui/Command;", 4U);
    if (!commands) return std::unexpected(commands.error());
    return {};
}

[[nodiscard]] Status initialize_item(Machine& machine,
                                     ObjectRef item,
                                     i32 type,
                                     ObjectRef label) {
    auto id = ensure_native_id(machine, item, kItemIdField);
    if (!id) return std::unexpected(id.error());
    if (label.is_null()) {
        auto empty = empty_string(machine);
        if (!empty) return std::unexpected(empty.error());
        label = *empty;
    }
    auto type_stored = set_int_field(machine, item, kItemTypeField, type);
    auto label_stored = set_reference_field(machine, item,
                                            kItemLabelField, label);
    auto parent_stored = set_int_field(machine, item, kItemParentField, 0);
    auto layout_stored = set_int_field(machine, item, kItemLayoutField, 0);
    auto listener_stored = set_reference_field(machine, item,
                                               kItemListenerField, {});
    if (!type_stored) return type_stored;
    if (!label_stored) return label_stored;
    if (!parent_stored) return parent_stored;
    if (!layout_stored) return layout_stored;
    return listener_stored;
}

[[nodiscard]] Result<UiBridgeEvent> screen_event(Machine& machine,
                                                 ObjectRef displayable,
                                                 i32 kind) {
    auto id = ensure_native_id(machine, displayable, kDisplayableIdField);
    auto type = int_field(machine, displayable, kDisplayableTypeField);
    auto title = reference_field(machine, displayable, kDisplayableTitleField);
    if (!id) return std::unexpected(id.error());
    if (!type) return std::unexpected(type.error());
    if (!title) return std::unexpected(title.error());
    auto title_text = utf8_string(machine, *title);
    if (!title_text) return std::unexpected(title_text.error());
    return UiBridgeEvent {
        .kind = kind,
        .component_id = *id,
        .component_type = *type,
        .text = std::move(*title_text),
    };
}

[[nodiscard]] Result<UiBridgeEvent> item_event(Machine& machine,
                                               ObjectRef item,
                                               i32 kind) {
    auto id = ensure_native_id(machine, item, kItemIdField);
    auto type = int_field(machine, item, kItemTypeField);
    auto label = reference_field(machine, item, kItemLabelField);
    auto parent = int_field(machine, item, kItemParentField);
    if (!id) return std::unexpected(id.error());
    if (!type) return std::unexpected(type.error());
    if (!label) return std::unexpected(label.error());
    if (!parent) return std::unexpected(parent.error());
    auto label_text = utf8_string(machine, *label);
    if (!label_text) return std::unexpected(label_text.error());

    UiBridgeEvent event {
        .kind = kind,
        .component_id = *id,
        .parent_id = *parent,
        .component_type = *type,
        .text = std::move(*label_text),
    };
    auto class_name = machine.heap().class_name(item);
    if (!class_name) return std::unexpected(class_name.error());
    if (*class_name == "javax/microedition/lcdui/StringItem") {
        auto text = reference_field(machine, item, kStringItemTextField);
        if (!text) return std::unexpected(text.error());
        auto encoded = utf8_string(machine, *text);
        if (!encoded) return std::unexpected(encoded.error());
        event.detail = std::move(*encoded);
    } else if (*class_name == "javax/microedition/lcdui/TextField") {
        auto text = reference_field(machine, item, kTextFieldTextField);
        auto maximum = int_field(machine, item, kTextFieldMaxSizeField);
        auto constraints = int_field(machine, item, kTextFieldConstraintsField);
        auto caret = int_field(machine, item, kTextFieldCaretField);
        if (!text) return std::unexpected(text.error());
        if (!maximum) return std::unexpected(maximum.error());
        if (!constraints) return std::unexpected(constraints.error());
        if (!caret) return std::unexpected(caret.error());
        auto encoded = utf8_string(machine, *text);
        if (!encoded) return std::unexpected(encoded.error());
        event.arguments = {*maximum, *constraints, *caret,
                           kTextFieldMetadata};
        event.detail = std::move(*encoded);
    } else if (*class_name == "javax/microedition/lcdui/Gauge") {
        auto interactive = int_field(machine, item, kGaugeInteractiveField);
        auto maximum = int_field(machine, item, kGaugeMaxValueField);
        auto value = int_field(machine, item, kGaugeValueField);
        if (!interactive) return std::unexpected(interactive.error());
        if (!maximum) return std::unexpected(maximum.error());
        if (!value) return std::unexpected(value.error());
        event.arguments = {*value, *maximum, *interactive,
                           kGaugeMetadata};
    } else if (*class_name == "javax/microedition/lcdui/DateField") {
        auto date = reference_field(machine, item, kDateFieldDateField);
        auto mode = int_field(machine, item, kDateFieldInputModeField);
        auto zone = reference_field(machine, item, kDateFieldTimeZoneField);
        if (!date) return std::unexpected(date.error());
        if (!mode) return std::unexpected(mode.error());
        if (!zone) return std::unexpected(zone.error());
        const i32 bridge_mode = *mode == 2 ? 1 : (*mode == 1 ? 2 : 3);
        event.arguments = {0, bridge_mode, 0, kDateFieldMetadata};
        if (!date->is_null()) {
            auto milliseconds = long_field(machine, *date, kDateTimeField);
            if (!milliseconds) return std::unexpected(milliseconds.error());
            event.value64 = *milliseconds / 1'000;
        }
        if (!zone->is_null()) {
            auto zone_id = reference_field(machine, *zone, kTimeZoneIdField);
            if (!zone_id) return std::unexpected(zone_id.error());
            auto encoded = utf8_string(machine, *zone_id);
            if (!encoded) return std::unexpected(encoded.error());
            event.detail = std::move(*encoded);
        }
    } else if (*class_name == "javax/microedition/lcdui/Spacer") {
        auto width = int_field(machine, item, kSpacerWidthField);
        auto height = int_field(machine, item, kSpacerHeightField);
        if (!width) return std::unexpected(width.error());
        if (!height) return std::unexpected(height.error());
        event.arguments = {0, 0, *width, *height};
    }
    return event;
}

[[nodiscard]] Result<UiBridgeEvent> text_box_event(Machine& machine,
                                                   ObjectRef text_box,
                                                   i32 kind) {
    auto peer_id = ensure_native_id(machine, text_box, kTextBoxPeerIdField);
    auto screen_id = ensure_native_id(machine, text_box,
                                      kDisplayableIdField);
    auto text = reference_field(machine, text_box, kTextBoxTextField);
    auto maximum = int_field(machine, text_box, kTextBoxMaxSizeField);
    auto constraints = int_field(machine, text_box,
                                 kTextBoxConstraintsField);
    auto caret = int_field(machine, text_box, kTextBoxCaretField);
    if (!peer_id) return std::unexpected(peer_id.error());
    if (!screen_id) return std::unexpected(screen_id.error());
    if (!text) return std::unexpected(text.error());
    if (!maximum) return std::unexpected(maximum.error());
    if (!constraints) return std::unexpected(constraints.error());
    if (!caret) return std::unexpected(caret.error());
    auto encoded = utf8_string(machine, *text);
    if (!encoded) return std::unexpected(encoded.error());
    return UiBridgeEvent {
        .kind = kind,
        .component_id = *peer_id,
        .parent_id = *screen_id,
        .component_type = kTypeTextField,
        .arguments = {*maximum, *constraints, *caret,
                      kTextFieldMetadata},
        .detail = std::move(*encoded),
    };
}

[[nodiscard]] Status emit_text_box(Machine& machine,
                                   ObjectRef text_box,
                                   i32 kind) {
    auto event = text_box_event(machine, text_box, kind);
    if (!event) return std::unexpected(event.error());
    machine.emit_ui_event(std::move(*event));
    return {};
}

[[nodiscard]] Status emit_item(Machine& machine,
                               ObjectRef item,
                               i32 kind) {
    auto event = item_event(machine, item, kind);
    if (!event) return std::unexpected(event.error());
    machine.emit_ui_event(std::move(*event));
    return {};
}

[[nodiscard]] Result<UiBridgeEvent> command_event(Machine& machine,
                                                  ObjectRef command,
                                                  i32 order) {
    auto id = ensure_native_id(machine, command, kCommandIdField);
    auto label = reference_field(machine, command, kCommandLabelField);
    auto long_label = reference_field(machine, command,
                                      kCommandLongLabelField);
    auto type = int_field(machine, command, kCommandTypeField);
    auto priority = int_field(machine, command, kCommandPriorityField);
    if (!id) return std::unexpected(id.error());
    if (!label) return std::unexpected(label.error());
    if (!long_label) return std::unexpected(long_label.error());
    if (!type) return std::unexpected(type.error());
    if (!priority) return std::unexpected(priority.error());
    auto label_text = utf8_string(machine, *label);
    auto long_text = utf8_string(machine, *long_label);
    if (!label_text) return std::unexpected(label_text.error());
    if (!long_text) return std::unexpected(long_text.error());
    return UiBridgeEvent {
        .kind = kEventCommand,
        .component_id = *id,
        .index = order,
        .arguments = {*type, *priority, 0, 0},
        .text = std::move(*label_text),
        .detail = std::move(*long_text),
    };
}

[[nodiscard]] Status emit_commands(Machine& machine,
                                   ObjectRef displayable) {
    machine.emit_ui_event(UiBridgeEvent {.kind = kEventCommandsReset});
    auto commands = reference_field(machine, displayable,
                                    kDisplayableCommandsField);
    auto count = int_field(machine, displayable,
                           kDisplayableCommandCountField);
    if (!commands) return std::unexpected(commands.error());
    if (!count) return std::unexpected(count.error());
    if (commands->is_null()) return {};
    for (i32 index = 0; index < *count; ++index) {
        auto value = machine.heap().element(*commands,
                                            static_cast<usize>(index));
        if (!value) return std::unexpected(value.error());
        auto command = value->as_reference();
        if (!command) return std::unexpected(command.error());
        if (command->is_null()) continue;
        auto event = command_event(machine, *command, index);
        if (!event) return std::unexpected(event.error());
        machine.emit_ui_event(std::move(*event));
    }
    return {};
}

[[nodiscard]] Status attach_item(Machine& machine,
                                 ObjectRef form,
                                 ObjectRef item,
                                 i32 index,
                                 bool emit_created) {
    auto form_id = ensure_native_id(machine, form, kDisplayableIdField);
    auto parent = int_field(machine, item, kItemParentField);
    if (!form_id) return std::unexpected(form_id.error());
    if (!parent) return std::unexpected(parent.error());
    if (*parent != 0 && *parent != *form_id) {
        return fail_java("java/lang/IllegalStateException",
                         "LCDUI Item already belongs to another Form");
    }
    auto parent_stored = set_int_field(machine, item, kItemParentField,
                                       *form_id);
    if (!parent_stored) return parent_stored;
    if (emit_created) {
        auto created = emit_item(machine, item, kEventItemCreated);
        if (!created) return created;
    }
    auto shown = emit_item(machine, item, kEventItemShown);
    if (!shown) return shown;
    auto is_choice_group = machine.object_is_instance(
        item, "javax/microedition/lcdui/ChoiceGroup");
    if (!is_choice_group) return std::unexpected(is_choice_group.error());
    if (*is_choice_group) {
        auto choices = emit_choice_elements(machine, item);
        if (!choices) return choices;
    }
    (void)index;
    return {};
}

[[nodiscard]] Status validate_item(Machine& machine, ObjectRef item) {
    if (item.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "LCDUI Item is null");
    }
    auto valid = machine.object_is_instance(
        item, "javax/microedition/lcdui/Item");
    if (!valid) return std::unexpected(valid.error());
    if (!*valid) {
        return fail_java("java/lang/IllegalArgumentException",
                         "object is not an LCDUI Item");
    }
    return {};
}

[[nodiscard]] Result<i32> append_item(Machine& machine,
                                      ObjectRef form,
                                      ObjectRef item) {
    auto valid = validate_item(machine, item);
    if (!valid) return std::unexpected(valid.error());
    auto count = int_field(machine, form, kFormItemCountField);
    if (!count) return std::unexpected(count.error());
    auto items = ensure_reference_array(
        machine, form, kFormItemsField,
        "[Ljavax/microedition/lcdui/Item;",
        static_cast<usize>(*count) + 1U);
    if (!items) return std::unexpected(items.error());
    auto stored = machine.heap().set_element(
        *items, static_cast<usize>(*count), Value::from_reference(item));
    if (!stored) return std::unexpected(stored.error());
    auto count_stored = set_int_field(machine, form, kFormItemCountField,
                                      *count + 1);
    if (!count_stored) return std::unexpected(count_stored.error());
    auto attached = attach_item(machine, form, item, *count, true);
    if (!attached) return std::unexpected(attached.error());
    return *count;
}

[[nodiscard]] Result<ObjectRef> form_item(Machine& machine,
                                          ObjectRef form,
                                          i32 index) {
    auto count = int_field(machine, form, kFormItemCountField);
    if (!count) return std::unexpected(count.error());
    if (index < 0 || index >= *count) {
        return fail_java("java/lang/IndexOutOfBoundsException",
                         "Form item index is outside bounds");
    }
    auto items = reference_field(machine, form, kFormItemsField);
    if (!items) return std::unexpected(items.error());
    auto value = machine.heap().element(*items, static_cast<usize>(index));
    if (!value) return std::unexpected(value.error());
    return value->as_reference();
}

[[nodiscard]] Status update_item_if_attached(Machine& machine,
                                             ObjectRef item) {
    auto parent = int_field(machine, item, kItemParentField);
    if (!parent) return std::unexpected(parent.error());
    if (*parent == 0) return {};
    return emit_item(machine, item, kEventItemUpdated);
}

[[nodiscard]] Result<ObjectRef> normalized_string(Machine& machine,
                                                  ObjectRef value) {
    if (!value.is_null()) return value;
    return empty_string(machine);
}

[[nodiscard]] Result<i32> text_length(Machine& machine,
                                      ObjectRef string) {
    auto text = machine.heap().string_value(string);
    if (!text) return std::unexpected(text.error());
    if (text->size() > static_cast<usize>(std::numeric_limits<i32>::max())) {
        return fail(ErrorCode::overflow,
                    "LCDUI String length exceeds Java int range");
    }
    return static_cast<i32>(text->size());
}

[[nodiscard]] Result<std::u16string> decode_ui_utf8(
    std::string_view encoded) {
    std::u16string output;
    output.reserve(encoded.size());
    usize index = 0;
    while (index < encoded.size()) {
        const u8 first = static_cast<u8>(encoded[index]);
        if (first <= 0x7FU) {
            output.push_back(static_cast<char16_t>(first));
            ++index;
            continue;
        }
        u32 code_point = 0;
        usize count = 0;
        u32 minimum = 0;
        if ((first & 0xE0U) == 0xC0U) {
            code_point = first & 0x1FU;
            count = 2U;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            code_point = first & 0x0FU;
            count = 3U;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            code_point = first & 0x07U;
            count = 4U;
            minimum = 0x10000U;
        } else {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI text contains invalid UTF-8");
        }
        if (count > encoded.size() - index) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI text contains truncated UTF-8");
        }
        for (usize offset = 1U; offset < count; ++offset) {
            const u8 next = static_cast<u8>(encoded[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return fail(ErrorCode::invalid_argument,
                            "LCDUI text contains invalid UTF-8 continuation");
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI text contains invalid UTF-8 scalar");
        }
        if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            output.push_back(static_cast<char16_t>(
                0xD800U | (code_point >> 10U)));
            output.push_back(static_cast<char16_t>(
                0xDC00U | (code_point & 0x3FFU)));
        }
        index += count;
    }
    return output;
}

[[nodiscard]] Result<ObjectRef> current_displayable(Machine& machine) {
    auto singleton_field = machine.class_states().resolve_field(
        "javax/microedition/lcdui/Display", "singleton",
        "Ljavax/microedition/lcdui/Display;", true);
    if (!singleton_field) return std::unexpected(singleton_field.error());
    auto singleton = machine.class_states().static_field(*singleton_field);
    if (!singleton) return std::unexpected(singleton.error());
    auto display = singleton->as_reference();
    if (!display) return std::unexpected(display.error());
    if (display->is_null()) return ObjectRef {};
    return reference_field(machine, *display, kDisplayCurrentField);
}

[[nodiscard]] Status dispatch_command_listener(Machine& machine,
                                               ObjectRef command,
                                               ObjectRef displayable) {
    auto listener = reference_field(machine, displayable,
                                    kDisplayableListenerField);
    if (!listener) return std::unexpected(listener.error());
    if (listener->is_null()) return {};

    const Value callback_arguments[] {
        Value::from_reference(command),
        Value::from_reference(displayable),
    };
    auto result = machine.invoke_instance(
        *listener,
        "javax/microedition/lcdui/CommandListener",
        "commandAction",
        "(Ljavax/microedition/lcdui/Command;Ljavax/microedition/lcdui/Displayable;)V",
        callback_arguments);
    if (!result) return std::unexpected(result.error());
    if (result->completed_normally()) return {};
    if (!result->throwable.has_value()) {
        return fail(ErrorCode::internal_error,
                    "LCDUI command callback failed without throwable");
    }
    auto throwable = machine.heap().class_name(*result->throwable);
    if (!throwable) return std::unexpected(throwable.error());
    return fail(ErrorCode::java_exception,
                "LCDUI command callback threw " + *throwable);
}

[[nodiscard]] Result<i32> normalized_gauge_value(bool interactive,
                                                  i32 maximum,
                                                  i32 value) {
    if (maximum == -1) {
        if (interactive || value < 0 || value > 3) {
            return fail_java("java/lang/IllegalArgumentException",
                             "invalid indefinite Gauge state");
        }
        return value;
    }
    if (maximum <= 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Gauge maximum must be positive");
    }
    return std::clamp(value, 0, maximum);
}

} // namespace

void register_lcdui_natives(NativeMethodRegistry& registry) {
    add(registry, "javax/microedition/lcdui/Command", "<init>",
        "(Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto command = receiver(arguments);
            auto label = reference_argument(arguments, 1U, false);
            auto type = integer_argument(arguments, 2U);
            auto priority = integer_argument(arguments, 3U);
            if (!command) return std::unexpected(command.error());
            if (!label) return std::unexpected(label.error());
            if (!type) return std::unexpected(type.error());
            if (!priority) return std::unexpected(priority.error());
            if (*type < 1 || *type > 8) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid MIDP Command type");
            }
            auto id = ensure_native_id(machine, *command, kCommandIdField);
            if (!id) return std::unexpected(id.error());
            auto label_stored = set_reference_field(machine, *command,
                                                    kCommandLabelField, *label);
            auto long_stored = set_reference_field(machine, *command,
                                                   kCommandLongLabelField, *label);
            auto type_stored = set_int_field(machine, *command,
                                             kCommandTypeField, *type);
            auto priority_stored = set_int_field(machine, *command,
                                                 kCommandPriorityField,
                                                 *priority);
            if (!label_stored) return std::unexpected(label_stored.error());
            if (!long_stored) return std::unexpected(long_stored.error());
            if (!type_stored) return std::unexpected(type_stored.error());
            if (!priority_stored)
                return std::unexpected(priority_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Command", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto command = receiver(arguments);
            auto label = reference_argument(arguments, 1U, false);
            auto long_label = reference_argument(arguments, 2U, false);
            auto type = integer_argument(arguments, 3U);
            auto priority = integer_argument(arguments, 4U);
            if (!command) return std::unexpected(command.error());
            if (!label) return std::unexpected(label.error());
            if (!long_label) return std::unexpected(long_label.error());
            if (!type) return std::unexpected(type.error());
            if (!priority) return std::unexpected(priority.error());
            if (*type < 1 || *type > 8) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "invalid MIDP Command type");
            }
            auto id = ensure_native_id(machine, *command, kCommandIdField);
            if (!id) return std::unexpected(id.error());
            auto first = set_reference_field(machine, *command,
                                             kCommandLabelField, *label);
            auto second = set_reference_field(machine, *command,
                                              kCommandLongLabelField,
                                              *long_label);
            auto third = set_int_field(machine, *command,
                                       kCommandTypeField, *type);
            auto fourth = set_int_field(machine, *command,
                                        kCommandPriorityField, *priority);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            if (!third) return std::unexpected(third.error());
            if (!fourth) return std::unexpected(fourth.error());
            return std::optional<Value> {};
        });

    const auto command_reference_getter = [&registry](
        const char* name,
        usize field_index) {
        add(registry, "javax/microedition/lcdui/Command", name,
            "()Ljava/lang/String;",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto command = receiver(arguments);
                if (!command) return std::unexpected(command.error());
                auto value = reference_field(machine, *command, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_reference(*value));
            });
    };
    command_reference_getter("getLabel", kCommandLabelField);
    command_reference_getter("getLongLabel", kCommandLongLabelField);
    const auto command_int_getter = [&registry](const char* name,
                                                usize field_index) {
        add(registry, "javax/microedition/lcdui/Command", name, "()I",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto command = receiver(arguments);
                if (!command) return std::unexpected(command.error());
                auto value = int_field(machine, *command, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    command_int_getter("getCommandType", kCommandTypeField);
    command_int_getter("getPriority", kCommandPriorityField);

    add(registry, "javax/microedition/lcdui/Displayable", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_displayable(
                machine, *object, kTypeForm, {});
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Screen", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto initialized = initialize_displayable(
                machine, *object, kTypeForm, {});
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Displayable", "getTitle",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto title = reference_field(machine, *object,
                                         kDisplayableTitleField);
            if (!title) return std::unexpected(title.error());
            return std::optional<Value>(Value::from_reference(*title));
        });
    add(registry, "javax/microedition/lcdui/Displayable", "setTitle",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto title = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!title) return std::unexpected(title.error());
            auto normalized = normalized_string(machine, *title);
            if (!normalized) return std::unexpected(normalized.error());
            auto stored = set_reference_field(machine, *object,
                                              kDisplayableTitleField,
                                              *normalized);
            if (!stored) return std::unexpected(stored.error());
            auto shown = is_shown(machine, *object);
            if (!shown) return std::unexpected(shown.error());
            if (*shown) {
                auto event = screen_event(machine, *object,
                                          kEventScreenUpdated);
                if (!event) return std::unexpected(event.error());
                machine.emit_ui_event(std::move(*event));
            }
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Displayable", "isShown", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto shown = is_shown(machine, *object);
            if (!shown) return std::unexpected(shown.error());
            return std::optional<Value>(Value::from_int(*shown ? 1 : 0));
        });
    add(registry, "javax/microedition/lcdui/Displayable",
        "setCommandListener",
        "(Ljavax/microedition/lcdui/CommandListener;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto listener = reference_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!listener) return std::unexpected(listener.error());
            auto stored = set_reference_field(machine, *object,
                                              kDisplayableListenerField,
                                              *listener);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Displayable", "addCommand",
        "(Ljavax/microedition/lcdui/Command;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto displayable = receiver(arguments);
            auto command = reference_argument(arguments, 1U, false);
            if (!displayable) return std::unexpected(displayable.error());
            if (!command) return std::unexpected(command.error());
            auto count = int_field(machine, *displayable,
                                   kDisplayableCommandCountField);
            if (!count) return std::unexpected(count.error());
            auto commands = ensure_reference_array(
                machine, *displayable, kDisplayableCommandsField,
                "[Ljavax/microedition/lcdui/Command;",
                static_cast<usize>(*count) + 1U);
            if (!commands) return std::unexpected(commands.error());
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(
                    *commands, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto existing = value->as_reference();
                if (!existing) return std::unexpected(existing.error());
                if (*existing == *command) return std::optional<Value> {};
            }
            auto stored = machine.heap().set_element(
                *commands, static_cast<usize>(*count),
                Value::from_reference(*command));
            if (!stored) return std::unexpected(stored.error());
            auto updated = set_int_field(machine, *displayable,
                                         kDisplayableCommandCountField,
                                         *count + 1);
            if (!updated) return std::unexpected(updated.error());
            auto shown = is_shown(machine, *displayable);
            if (!shown) return std::unexpected(shown.error());
            if (*shown) {
                auto emitted = emit_commands(machine, *displayable);
                if (!emitted) return std::unexpected(emitted.error());
            }
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Displayable", "removeCommand",
        "(Ljavax/microedition/lcdui/Command;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto displayable = receiver(arguments);
            auto command = reference_argument(arguments, 1U);
            if (!displayable) return std::unexpected(displayable.error());
            if (!command) return std::unexpected(command.error());
            if (command->is_null()) return std::optional<Value> {};
            auto commands = reference_field(machine, *displayable,
                                            kDisplayableCommandsField);
            auto count = int_field(machine, *displayable,
                                   kDisplayableCommandCountField);
            if (!commands) return std::unexpected(commands.error());
            if (!count) return std::unexpected(count.error());
            i32 found = -1;
            for (i32 index = 0; index < *count; ++index) {
                auto value = machine.heap().element(
                    *commands, static_cast<usize>(index));
                if (!value) return std::unexpected(value.error());
                auto existing = value->as_reference();
                if (!existing) return std::unexpected(existing.error());
                if (*existing == *command) {
                    found = index;
                    break;
                }
            }
            if (found < 0) return std::optional<Value> {};
            for (i32 index = found; index + 1 < *count; ++index) {
                auto next = machine.heap().element(
                    *commands, static_cast<usize>(index + 1));
                if (!next) return std::unexpected(next.error());
                auto stored = machine.heap().set_element(
                    *commands, static_cast<usize>(index), *next);
                if (!stored) return std::unexpected(stored.error());
            }
            auto cleared = machine.heap().set_element(
                *commands, static_cast<usize>(*count - 1),
                Value::from_reference({}));
            auto updated = set_int_field(machine, *displayable,
                                         kDisplayableCommandCountField,
                                         *count - 1);
            if (!cleared) return std::unexpected(cleared.error());
            if (!updated) return std::unexpected(updated.error());
            auto shown = is_shown(machine, *displayable);
            if (!shown) return std::unexpected(shown.error());
            if (*shown) {
                auto emitted = emit_commands(machine, *displayable);
                if (!emitted) return std::unexpected(emitted.error());
            }
            return std::optional<Value> {};
        });

    add(registry, "javax/microedition/lcdui/Display", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto display = receiver(arguments);
            if (!display) return std::unexpected(display.error());
            auto stored = set_reference_field(machine, *display,
                                              kDisplayCurrentField, {});
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Display", "getDisplay",
        "(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto midlet = reference_argument(arguments, 0U, false);
            if (!midlet) return std::unexpected(midlet.error());
            auto singleton_field = machine.class_states().resolve_field(
                "javax/microedition/lcdui/Display", "singleton",
                "Ljavax/microedition/lcdui/Display;", true);
            if (!singleton_field)
                return std::unexpected(singleton_field.error());
            auto singleton = machine.class_states().static_field(
                *singleton_field);
            if (!singleton) return std::unexpected(singleton.error());
            auto display = singleton->as_reference();
            if (!display) return std::unexpected(display.error());
            if (display->is_null()) {
                auto allocated = machine.class_states().allocate_instance(
                    machine.heap(), "javax/microedition/lcdui/Display");
                if (!allocated) return std::unexpected(allocated.error());
                auto current_stored = set_reference_field(
                    machine, *allocated, kDisplayCurrentField, {});
                if (!current_stored)
                    return std::unexpected(current_stored.error());
                auto singleton_stored =
                    machine.class_states().set_static_field(
                        *singleton_field, Value::from_reference(*allocated));
                if (!singleton_stored)
                    return std::unexpected(singleton_stored.error());
                display = *allocated;
            }
            return std::optional<Value>(Value::from_reference(*display));
        });
    add(registry, "javax/microedition/lcdui/Display", "getCurrent",
        "()Ljavax/microedition/lcdui/Displayable;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto display = receiver(arguments);
            if (!display) return std::unexpected(display.error());
            auto current = reference_field(machine, *display,
                                           kDisplayCurrentField);
            if (!current) return std::unexpected(current.error());
            return std::optional<Value>(Value::from_reference(*current));
        });
    add(registry, "javax/microedition/lcdui/Display", "setCurrent",
        "(Ljavax/microedition/lcdui/Displayable;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto display = receiver(arguments);
            auto next = reference_argument(arguments, 1U);
            if (!display) return std::unexpected(display.error());
            if (!next) return std::unexpected(next.error());
            if (!next->is_null()) {
                auto valid = machine.object_is_instance(
                    *next, "javax/microedition/lcdui/Displayable");
                if (!valid) return std::unexpected(valid.error());
                if (!*valid) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "Display.setCurrent target is not Displayable");
                }
            }
            auto current = reference_field(machine, *display,
                                           kDisplayCurrentField);
            if (!current) return std::unexpected(current.error());
            if (*current == *next) return std::optional<Value> {};
            if (!current->is_null()) {
                auto hidden = screen_event(machine, *current,
                                           kEventScreenHidden);
                if (!hidden) return std::unexpected(hidden.error());
                machine.emit_ui_event(std::move(*hidden));
                auto stored = set_int_field(machine, *current,
                                            kDisplayableShownField, 0);
                if (!stored) return std::unexpected(stored.error());
                if (auto* canvas = machine.canvas_bridge(); canvas != nullptr) {
                    auto visibility = canvas->set_display_visible(
                        *current, false);
                    if (!visibility)
                        return std::unexpected(visibility.error());
                }
            }
            auto assigned = set_reference_field(machine, *display,
                                                kDisplayCurrentField, *next);
            if (!assigned) return std::unexpected(assigned.error());
            if (next->is_null()) {
                machine.emit_ui_event(
                    UiBridgeEvent {.kind = kEventCommandsReset});
                return std::optional<Value> {};
            }
            auto shown_stored = set_int_field(machine, *next,
                                              kDisplayableShownField, 1);
            if (!shown_stored) return std::unexpected(shown_stored.error());
            if (auto* canvas = machine.canvas_bridge(); canvas != nullptr) {
                auto visibility = canvas->set_display_visible(*next, true);
                if (!visibility)
                    return std::unexpected(visibility.error());
            }
            auto shown = screen_event(machine, *next, kEventScreenShown);
            if (!shown) return std::unexpected(shown.error());
            machine.emit_ui_event(std::move(*shown));
            auto commands = emit_commands(machine, *next);
            if (!commands) return std::unexpected(commands.error());
            return std::optional<Value> {};
        });

    add(registry, "javax/microedition/lcdui/Item", "<init>",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto label = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!label) return std::unexpected(label.error());
            auto initialized = initialize_item(machine, *item,
                                               kTypePlainString, *label);
            if (!initialized) return std::unexpected(initialized.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Item", "getLabel",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto label = reference_field(machine, *item, kItemLabelField);
            if (!label) return std::unexpected(label.error());
            return std::optional<Value>(Value::from_reference(*label));
        });
    add(registry, "javax/microedition/lcdui/Item", "setLabel",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto label = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!label) return std::unexpected(label.error());
            auto normalized = normalized_string(machine, *label);
            if (!normalized) return std::unexpected(normalized.error());
            auto stored = set_reference_field(machine, *item,
                                              kItemLabelField, *normalized);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Item", "getLayout", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto layout = int_field(machine, *item, kItemLayoutField);
            if (!layout) return std::unexpected(layout.error());
            return std::optional<Value>(Value::from_int(*layout));
        });
    add(registry, "javax/microedition/lcdui/Item", "setLayout", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto layout = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!layout) return std::unexpected(layout.error());
            auto stored = set_int_field(machine, *item,
                                        kItemLayoutField, *layout);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Item",
        "setItemCommandListener",
        "(Ljavax/microedition/lcdui/ItemCommandListener;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto listener = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!listener) return std::unexpected(listener.error());
            auto stored = set_reference_field(machine, *item,
                                              kItemListenerField, *listener);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto form_constructor = [&registry](const char* descriptor,
                                               bool has_items) {
        add(registry, "javax/microedition/lcdui/Form", "<init>", descriptor,
            [has_items](Machine& machine,
                        std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto form = receiver(arguments);
                auto title = reference_argument(arguments, 1U);
                if (!form) return std::unexpected(form.error());
                if (!title) return std::unexpected(title.error());
                auto initialized = initialize_displayable(
                    machine, *form, kTypeForm, *title);
                if (!initialized) return std::unexpected(initialized.error());
                auto count_stored = set_int_field(machine, *form,
                                                  kFormItemCountField, 0);
                if (!count_stored)
                    return std::unexpected(count_stored.error());
                auto items = ensure_reference_array(
                    machine, *form, kFormItemsField,
                    "[Ljavax/microedition/lcdui/Item;", 4U);
                if (!items) return std::unexpected(items.error());
                auto created = screen_event(machine, *form,
                                            kEventScreenCreated);
                if (!created) return std::unexpected(created.error());
                machine.emit_ui_event(std::move(*created));
                if (has_items) {
                    auto source = reference_argument(arguments, 2U, false);
                    if (!source) return std::unexpected(source.error());
                    auto length = machine.heap().array_length(*source);
                    if (!length) return std::unexpected(length.error());
                    for (usize index = 0; index < *length; ++index) {
                        auto value = machine.heap().element(*source, index);
                        if (!value) return std::unexpected(value.error());
                        auto item = value->as_reference();
                        if (!item) return std::unexpected(item.error());
                        auto appended = append_item(machine, *form, *item);
                        if (!appended)
                            return std::unexpected(appended.error());
                    }
                }
                return std::optional<Value> {};
            });
    };
    form_constructor("(Ljava/lang/String;)V", false);
    form_constructor("(Ljava/lang/String;[Ljavax/microedition/lcdui/Item;)V",
                     true);
    add(registry, "javax/microedition/lcdui/Form", "append",
        "(Ljavax/microedition/lcdui/Item;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto item = reference_argument(arguments, 1U, false);
            if (!form) return std::unexpected(form.error());
            if (!item) return std::unexpected(item.error());
            auto index = append_item(machine, *form, *item);
            if (!index) return std::unexpected(index.error());
            return std::optional<Value>(Value::from_int(*index));
        });
    add(registry, "javax/microedition/lcdui/Form", "append",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto text = reference_argument(arguments, 1U, false);
            if (!form) return std::unexpected(form.error());
            if (!text) return std::unexpected(text.error());
            auto item = machine.class_states().allocate_instance(
                machine.heap(), "javax/microedition/lcdui/StringItem");
            if (!item) return std::unexpected(item.error());
            auto empty = empty_string(machine);
            if (!empty) return std::unexpected(empty.error());
            auto initialized = initialize_item(machine, *item,
                                               kTypePlainString, *empty);
            if (!initialized) return std::unexpected(initialized.error());
            auto text_stored = set_reference_field(machine, *item,
                                                   kStringItemTextField,
                                                   *text);
            auto appearance_stored = set_int_field(machine, *item,
                                                   kStringItemAppearanceField,
                                                   0);
            if (!text_stored) return std::unexpected(text_stored.error());
            if (!appearance_stored)
                return std::unexpected(appearance_stored.error());
            auto index = append_item(machine, *form, *item);
            if (!index) return std::unexpected(index.error());
            return std::optional<Value>(Value::from_int(*index));
        });
    add(registry, "javax/microedition/lcdui/Form", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            if (!form) return std::unexpected(form.error());
            auto count = int_field(machine, *form, kFormItemCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_int(*count));
        });
    add(registry, "javax/microedition/lcdui/Form", "get",
        "(I)Ljavax/microedition/lcdui/Item;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto index = integer_argument(arguments, 1U);
            if (!form) return std::unexpected(form.error());
            if (!index) return std::unexpected(index.error());
            auto item = form_item(machine, *form, *index);
            if (!item) return std::unexpected(item.error());
            return std::optional<Value>(Value::from_reference(*item));
        });
    add(registry, "javax/microedition/lcdui/Form", "insert",
        "(ILjavax/microedition/lcdui/Item;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto index = integer_argument(arguments, 1U);
            auto item = reference_argument(arguments, 2U, false);
            if (!form) return std::unexpected(form.error());
            if (!index) return std::unexpected(index.error());
            if (!item) return std::unexpected(item.error());
            auto valid = validate_item(machine, *item);
            if (!valid) return std::unexpected(valid.error());
            auto count = int_field(machine, *form, kFormItemCountField);
            if (!count) return std::unexpected(count.error());
            if (*index < 0 || *index > *count) {
                return fail_java("java/lang/IndexOutOfBoundsException",
                                 "Form insertion index is outside bounds");
            }
            auto items = ensure_reference_array(
                machine, *form, kFormItemsField,
                "[Ljavax/microedition/lcdui/Item;",
                static_cast<usize>(*count) + 1U);
            if (!items) return std::unexpected(items.error());
            for (i32 cursor = *count; cursor > *index; --cursor) {
                auto previous = machine.heap().element(
                    *items, static_cast<usize>(cursor - 1));
                if (!previous) return std::unexpected(previous.error());
                auto stored = machine.heap().set_element(
                    *items, static_cast<usize>(cursor), *previous);
                if (!stored) return std::unexpected(stored.error());
            }
            auto stored = machine.heap().set_element(
                *items, static_cast<usize>(*index),
                Value::from_reference(*item));
            if (!stored) return std::unexpected(stored.error());
            auto count_stored = set_int_field(machine, *form,
                                              kFormItemCountField,
                                              *count + 1);
            if (!count_stored)
                return std::unexpected(count_stored.error());
            auto attached = attach_item(machine, *form, *item, *index, true);
            if (!attached) return std::unexpected(attached.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Form", "delete", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto index = integer_argument(arguments, 1U);
            if (!form) return std::unexpected(form.error());
            if (!index) return std::unexpected(index.error());
            auto removed = form_item(machine, *form, *index);
            if (!removed) return std::unexpected(removed.error());
            auto items = reference_field(machine, *form, kFormItemsField);
            auto count = int_field(machine, *form, kFormItemCountField);
            if (!items) return std::unexpected(items.error());
            if (!count) return std::unexpected(count.error());
            auto removed_id = ensure_native_id(machine, *removed, kItemIdField);
            if (!removed_id) return std::unexpected(removed_id.error());
            machine.emit_ui_event(UiBridgeEvent {
                .kind = kEventItemHidden,
                .component_id = *removed_id,
            });
            machine.emit_ui_event(UiBridgeEvent {
                .kind = kEventItemDeleted,
                .component_id = *removed_id,
            });
            auto parent_cleared = set_int_field(machine, *removed,
                                                kItemParentField, 0);
            if (!parent_cleared)
                return std::unexpected(parent_cleared.error());
            for (i32 cursor = *index; cursor + 1 < *count; ++cursor) {
                auto next = machine.heap().element(
                    *items, static_cast<usize>(cursor + 1));
                if (!next) return std::unexpected(next.error());
                auto stored = machine.heap().set_element(
                    *items, static_cast<usize>(cursor), *next);
                if (!stored) return std::unexpected(stored.error());
            }
            auto cleared = machine.heap().set_element(
                *items, static_cast<usize>(*count - 1),
                Value::from_reference({}));
            auto count_stored = set_int_field(machine, *form,
                                              kFormItemCountField,
                                              *count - 1);
            if (!cleared) return std::unexpected(cleared.error());
            if (!count_stored)
                return std::unexpected(count_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Form", "deleteAll", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            if (!form) return std::unexpected(form.error());
            auto count = int_field(machine, *form, kFormItemCountField);
            if (!count) return std::unexpected(count.error());
            for (i32 index = *count - 1; index >= 0; --index) {
                auto item = form_item(machine, *form, index);
                if (!item) return std::unexpected(item.error());
                auto id = ensure_native_id(machine, *item, kItemIdField);
                if (!id) return std::unexpected(id.error());
                machine.emit_ui_event(UiBridgeEvent {
                    .kind = kEventItemDeleted,
                    .component_id = *id,
                });
                auto cleared = set_int_field(machine, *item,
                                             kItemParentField, 0);
                if (!cleared) return std::unexpected(cleared.error());
            }
            auto items = ensure_reference_array(
                machine, *form, kFormItemsField,
                "[Ljavax/microedition/lcdui/Item;", 4U);
            if (!items) return std::unexpected(items.error());
            auto length = machine.heap().array_length(*items);
            if (!length) return std::unexpected(length.error());
            for (usize index = 0; index < *length; ++index) {
                auto cleared = machine.heap().set_element(
                    *items, index, Value::from_reference({}));
                if (!cleared) return std::unexpected(cleared.error());
            }
            auto count_stored = set_int_field(machine, *form,
                                              kFormItemCountField, 0);
            if (!count_stored)
                return std::unexpected(count_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Form", "set",
        "(ILjavax/microedition/lcdui/Item;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto form = receiver(arguments);
            auto index = integer_argument(arguments, 1U);
            auto replacement = reference_argument(arguments, 2U, false);
            if (!form) return std::unexpected(form.error());
            if (!index) return std::unexpected(index.error());
            if (!replacement) return std::unexpected(replacement.error());
            auto old = form_item(machine, *form, *index);
            if (!old) return std::unexpected(old.error());
            auto valid = validate_item(machine, *replacement);
            if (!valid) return std::unexpected(valid.error());
            auto items = reference_field(machine, *form, kFormItemsField);
            if (!items) return std::unexpected(items.error());
            auto old_id = ensure_native_id(machine, *old, kItemIdField);
            if (!old_id) return std::unexpected(old_id.error());
            machine.emit_ui_event(UiBridgeEvent {
                .kind = kEventItemDeleted,
                .component_id = *old_id,
            });
            auto old_parent = set_int_field(machine, *old, kItemParentField, 0);
            if (!old_parent) return std::unexpected(old_parent.error());
            auto stored = machine.heap().set_element(
                *items, static_cast<usize>(*index),
                Value::from_reference(*replacement));
            if (!stored) return std::unexpected(stored.error());
            auto attached = attach_item(machine, *form, *replacement,
                                        *index, true);
            if (!attached) return std::unexpected(attached.error());
            return std::optional<Value> {};
        });

    const auto string_item_constructor = [&registry](const char* descriptor,
                                                      bool has_appearance) {
        add(registry, "javax/microedition/lcdui/StringItem", "<init>",
            descriptor,
            [has_appearance](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto item = receiver(arguments);
                auto label = reference_argument(arguments, 1U);
                auto text = reference_argument(arguments, 2U);
                if (!item) return std::unexpected(item.error());
                if (!label) return std::unexpected(label.error());
                if (!text) return std::unexpected(text.error());
                i32 appearance = 0;
                if (has_appearance) {
                    auto parsed = integer_argument(arguments, 3U);
                    if (!parsed) return std::unexpected(parsed.error());
                    if (*parsed < 0 || *parsed > 2) {
                        return fail_java("java/lang/IllegalArgumentException",
                                         "invalid StringItem appearance mode");
                    }
                    appearance = *parsed;
                }
                const i32 type = appearance == 1
                    ? kTypeHyperlinkString
                    : (appearance == 2 ? kTypeButtonString
                                       : kTypePlainString);
                auto initialized = initialize_item(machine, *item, type, *label);
                if (!initialized) return std::unexpected(initialized.error());
                auto normalized = normalized_string(machine, *text);
                if (!normalized) return std::unexpected(normalized.error());
                auto text_stored = set_reference_field(machine, *item,
                                                       kStringItemTextField,
                                                       *normalized);
                auto appearance_stored = set_int_field(
                    machine, *item, kStringItemAppearanceField, appearance);
                if (!text_stored) return std::unexpected(text_stored.error());
                if (!appearance_stored)
                    return std::unexpected(appearance_stored.error());
                return std::optional<Value> {};
            });
    };
    string_item_constructor("(Ljava/lang/String;Ljava/lang/String;)V", false);
    string_item_constructor("(Ljava/lang/String;Ljava/lang/String;I)V", true);
    add(registry, "javax/microedition/lcdui/StringItem", "getText",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto text = reference_field(machine, *item, kStringItemTextField);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
    add(registry, "javax/microedition/lcdui/StringItem", "setText",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto text = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!text) return std::unexpected(text.error());
            auto normalized = normalized_string(machine, *text);
            if (!normalized) return std::unexpected(normalized.error());
            auto stored = set_reference_field(machine, *item,
                                              kStringItemTextField,
                                              *normalized);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/StringItem",
        "getAppearanceMode", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto appearance = int_field(machine, *item,
                                        kStringItemAppearanceField);
            if (!appearance) return std::unexpected(appearance.error());
            return std::optional<Value>(Value::from_int(*appearance));
        });

    add(registry, "javax/microedition/lcdui/TextField", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto label = reference_argument(arguments, 1U);
            auto text = reference_argument(arguments, 2U);
            auto maximum = integer_argument(arguments, 3U);
            auto constraints = integer_argument(arguments, 4U);
            if (!item) return std::unexpected(item.error());
            if (!label) return std::unexpected(label.error());
            if (!text) return std::unexpected(text.error());
            if (!maximum) return std::unexpected(maximum.error());
            if (!constraints) return std::unexpected(constraints.error());
            if (*maximum <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextField maxSize must be positive");
            }
            auto normalized = normalized_string(machine, *text);
            if (!normalized) return std::unexpected(normalized.error());
            auto length = text_length(machine, *normalized);
            if (!length) return std::unexpected(length.error());
            if (*length > *maximum) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextField initial text exceeds maxSize");
            }
            auto initialized = initialize_item(machine, *item,
                                               kTypeTextField, *label);
            if (!initialized) return std::unexpected(initialized.error());
            auto first = set_reference_field(machine, *item,
                                             kTextFieldTextField,
                                             *normalized);
            auto second = set_int_field(machine, *item,
                                        kTextFieldMaxSizeField, *maximum);
            auto third = set_int_field(machine, *item,
                                       kTextFieldConstraintsField,
                                       *constraints);
            auto fourth = set_int_field(machine, *item,
                                        kTextFieldCaretField, *length);
            auto fifth = set_reference_field(machine, *item,
                                             kTextFieldInputModeField, {});
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            if (!third) return std::unexpected(third.error());
            if (!fourth) return std::unexpected(fourth.error());
            if (!fifth) return std::unexpected(fifth.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/TextField", "getString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto text = reference_field(machine, *item, kTextFieldTextField);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
    add(registry, "javax/microedition/lcdui/TextField", "setString",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto text = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!text) return std::unexpected(text.error());
            auto normalized = normalized_string(machine, *text);
            if (!normalized) return std::unexpected(normalized.error());
            auto maximum = int_field(machine, *item, kTextFieldMaxSizeField);
            auto length = text_length(machine, *normalized);
            if (!maximum) return std::unexpected(maximum.error());
            if (!length) return std::unexpected(length.error());
            if (*length > *maximum) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextField text exceeds maxSize");
            }
            auto first = set_reference_field(machine, *item,
                                             kTextFieldTextField,
                                             *normalized);
            auto second = set_int_field(machine, *item,
                                        kTextFieldCaretField, *length);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    const auto text_field_int_getter = [&registry](const char* name,
                                                   usize field_index) {
        add(registry, "javax/microedition/lcdui/TextField", name, "()I",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto item = receiver(arguments);
                if (!item) return std::unexpected(item.error());
                auto value = int_field(machine, *item, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    text_field_int_getter("getMaxSize", kTextFieldMaxSizeField);
    text_field_int_getter("getCaretPosition", kTextFieldCaretField);
    text_field_int_getter("getConstraints", kTextFieldConstraintsField);
    add(registry, "javax/microedition/lcdui/TextField", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto text = reference_field(machine, *item, kTextFieldTextField);
            if (!text) return std::unexpected(text.error());
            auto length = text_length(machine, *text);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(Value::from_int(*length));
        });
    add(registry, "javax/microedition/lcdui/TextField", "setMaxSize",
        "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto maximum = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!maximum) return std::unexpected(maximum.error());
            if (*maximum <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextField maxSize must be positive");
            }
            auto text = reference_field(machine, *item, kTextFieldTextField);
            if (!text) return std::unexpected(text.error());
            auto value = machine.heap().string_value(*text);
            if (!value) return std::unexpected(value.error());
            if (value->size() > static_cast<usize>(*maximum)) {
                value->resize(static_cast<usize>(*maximum));
                auto replacement = create_string(machine, std::move(*value));
                if (!replacement) return std::unexpected(replacement.error());
                auto text_stored = set_reference_field(machine, *item,
                                                       kTextFieldTextField,
                                                       *replacement);
                if (!text_stored)
                    return std::unexpected(text_stored.error());
                auto caret_stored = set_int_field(machine, *item,
                                                  kTextFieldCaretField,
                                                  *maximum);
                if (!caret_stored)
                    return std::unexpected(caret_stored.error());
            }
            auto stored = set_int_field(machine, *item,
                                        kTextFieldMaxSizeField, *maximum);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(Value::from_int(*maximum));
        });
    add(registry, "javax/microedition/lcdui/TextField", "setConstraints",
        "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto constraints = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!constraints) return std::unexpected(constraints.error());
            auto stored = set_int_field(machine, *item,
                                        kTextFieldConstraintsField,
                                        *constraints);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/TextField",
        "setInitialInputMode", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto mode = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!mode) return std::unexpected(mode.error());
            auto stored = set_reference_field(machine, *item,
                                              kTextFieldInputModeField,
                                              *mode);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    add(registry, "javax/microedition/lcdui/Gauge", "<init>",
        "(Ljava/lang/String;ZII)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto label = reference_argument(arguments, 1U);
            auto interactive = integer_argument(arguments, 2U);
            auto maximum = integer_argument(arguments, 3U);
            auto value = integer_argument(arguments, 4U);
            if (!item) return std::unexpected(item.error());
            if (!label) return std::unexpected(label.error());
            if (!interactive) return std::unexpected(interactive.error());
            if (!maximum) return std::unexpected(maximum.error());
            if (!value) return std::unexpected(value.error());
            auto normalized = normalized_gauge_value(
                *interactive != 0, *maximum, *value);
            if (!normalized) return std::unexpected(normalized.error());
            auto initialized = initialize_item(
                machine, *item,
                *interactive != 0 ? kTypeInteractiveGauge
                                  : kTypeProgressGauge,
                *label);
            if (!initialized) return std::unexpected(initialized.error());
            auto first = set_int_field(machine, *item,
                                       kGaugeInteractiveField,
                                       *interactive != 0 ? 1 : 0);
            auto second = set_int_field(machine, *item,
                                        kGaugeMaxValueField, *maximum);
            auto third = set_int_field(machine, *item,
                                       kGaugeValueField, *normalized);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            if (!third) return std::unexpected(third.error());
            return std::optional<Value> {};
        });
    const auto gauge_getter = [&registry](const char* name,
                                          usize field_index) {
        add(registry, "javax/microedition/lcdui/Gauge", name, "()I",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto item = receiver(arguments);
                if (!item) return std::unexpected(item.error());
                auto value = int_field(machine, *item, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    gauge_getter("getMaxValue", kGaugeMaxValueField);
    gauge_getter("getValue", kGaugeValueField);
    add(registry, "javax/microedition/lcdui/Gauge", "isInteractive", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto value = int_field(machine, *item, kGaugeInteractiveField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value != 0 ? 1 : 0));
        });
    add(registry, "javax/microedition/lcdui/Gauge", "setValue", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto value = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!value) return std::unexpected(value.error());
            auto interactive = int_field(machine, *item,
                                         kGaugeInteractiveField);
            auto maximum = int_field(machine, *item, kGaugeMaxValueField);
            if (!interactive) return std::unexpected(interactive.error());
            if (!maximum) return std::unexpected(maximum.error());
            auto normalized = normalized_gauge_value(
                *interactive != 0, *maximum, *value);
            if (!normalized) return std::unexpected(normalized.error());
            auto stored = set_int_field(machine, *item,
                                        kGaugeValueField, *normalized);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Gauge", "setMaxValue", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto maximum = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!maximum) return std::unexpected(maximum.error());
            auto interactive = int_field(machine, *item,
                                         kGaugeInteractiveField);
            auto value = int_field(machine, *item, kGaugeValueField);
            if (!interactive) return std::unexpected(interactive.error());
            if (!value) return std::unexpected(value.error());
            auto normalized = normalized_gauge_value(
                *interactive != 0, *maximum, *value);
            if (!normalized) return std::unexpected(normalized.error());
            auto first = set_int_field(machine, *item,
                                       kGaugeMaxValueField, *maximum);
            auto second = set_int_field(machine, *item,
                                        kGaugeValueField, *normalized);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });

    add(registry, "javax/microedition/lcdui/TextBox", "<init>",
        "(Ljava/lang/String;Ljava/lang/String;II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            auto title = reference_argument(arguments, 1U);
            auto text = reference_argument(arguments, 2U);
            auto maximum = integer_argument(arguments, 3U);
            auto constraints = integer_argument(arguments, 4U);
            if (!text_box) return std::unexpected(text_box.error());
            if (!title) return std::unexpected(title.error());
            if (!text) return std::unexpected(text.error());
            if (!maximum) return std::unexpected(maximum.error());
            if (!constraints) return std::unexpected(constraints.error());
            if (*maximum <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextBox maxSize must be positive");
            }
            auto normalized = normalized_string(machine, *text);
            if (!normalized) return std::unexpected(normalized.error());
            auto length = text_length(machine, *normalized);
            if (!length) return std::unexpected(length.error());
            if (*length > *maximum) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextBox initial text exceeds maxSize");
            }
            auto initialized = initialize_displayable(
                machine, *text_box, kTypeForm, *title);
            if (!initialized) return std::unexpected(initialized.error());
            auto peer = ensure_native_id(machine, *text_box,
                                         kTextBoxPeerIdField);
            if (!peer) return std::unexpected(peer.error());
            auto first = set_reference_field(machine, *text_box,
                                             kTextBoxTextField, *normalized);
            auto second = set_int_field(machine, *text_box,
                                        kTextBoxMaxSizeField, *maximum);
            auto third = set_int_field(machine, *text_box,
                                       kTextBoxConstraintsField,
                                       *constraints);
            auto fourth = set_int_field(machine, *text_box,
                                        kTextBoxCaretField, *length);
            auto fifth = set_reference_field(machine, *text_box,
                                             kTextBoxInputModeField, {});
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            if (!third) return std::unexpected(third.error());
            if (!fourth) return std::unexpected(fourth.error());
            if (!fifth) return std::unexpected(fifth.error());

            auto created = screen_event(machine, *text_box,
                                        kEventScreenCreated);
            if (!created) return std::unexpected(created.error());
            machine.emit_ui_event(std::move(*created));
            auto screen_id = int_field(machine, *text_box,
                                       kDisplayableIdField);
            auto screen_title = reference_field(machine, *text_box,
                                                kDisplayableTitleField);
            if (!screen_id) return std::unexpected(screen_id.error());
            if (!screen_title)
                return std::unexpected(screen_title.error());
            auto encoded_title = utf8_string(machine, *screen_title);
            if (!encoded_title)
                return std::unexpected(encoded_title.error());
            machine.emit_ui_event(UiBridgeEvent {
                .kind = kEventScreenUpdated,
                .component_id = *screen_id,
                .component_type = kTypeForm,
                .arguments = {kScreenKindTextBox, 0, 0,
                              kScreenKindMetadata},
                .text = std::move(*encoded_title),
            });
            auto item_created = emit_text_box(machine, *text_box,
                                              kEventItemCreated);
            if (!item_created) return std::unexpected(item_created.error());
            auto item_shown = emit_text_box(machine, *text_box,
                                            kEventItemShown);
            if (!item_shown) return std::unexpected(item_shown.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/TextBox", "getString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            if (!text_box) return std::unexpected(text_box.error());
            auto text = reference_field(machine, *text_box,
                                        kTextBoxTextField);
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
    add(registry, "javax/microedition/lcdui/TextBox", "setString",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            auto text = reference_argument(arguments, 1U);
            if (!text_box) return std::unexpected(text_box.error());
            if (!text) return std::unexpected(text.error());
            auto normalized = normalized_string(machine, *text);
            if (!normalized) return std::unexpected(normalized.error());
            auto maximum = int_field(machine, *text_box,
                                     kTextBoxMaxSizeField);
            auto length = text_length(machine, *normalized);
            if (!maximum) return std::unexpected(maximum.error());
            if (!length) return std::unexpected(length.error());
            if (*length > *maximum) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextBox text exceeds maxSize");
            }
            auto first = set_reference_field(machine, *text_box,
                                             kTextBoxTextField, *normalized);
            auto second = set_int_field(machine, *text_box,
                                        kTextBoxCaretField, *length);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            auto updated = emit_text_box(machine, *text_box,
                                         kEventItemUpdated);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    const auto text_box_int_getter = [&registry](const char* name,
                                                 usize field_index) {
        add(registry, "javax/microedition/lcdui/TextBox", name, "()I",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto text_box = receiver(arguments);
                if (!text_box) return std::unexpected(text_box.error());
                auto value = int_field(machine, *text_box, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    text_box_int_getter("getMaxSize", kTextBoxMaxSizeField);
    text_box_int_getter("getCaretPosition", kTextBoxCaretField);
    text_box_int_getter("getConstraints", kTextBoxConstraintsField);
    add(registry, "javax/microedition/lcdui/TextBox", "size", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            if (!text_box) return std::unexpected(text_box.error());
            auto text = reference_field(machine, *text_box,
                                        kTextBoxTextField);
            if (!text) return std::unexpected(text.error());
            auto length = text_length(machine, *text);
            if (!length) return std::unexpected(length.error());
            return std::optional<Value>(Value::from_int(*length));
        });
    add(registry, "javax/microedition/lcdui/TextBox", "setMaxSize",
        "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            auto maximum = integer_argument(arguments, 1U);
            if (!text_box) return std::unexpected(text_box.error());
            if (!maximum) return std::unexpected(maximum.error());
            if (*maximum <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "TextBox maxSize must be positive");
            }
            auto text = reference_field(machine, *text_box,
                                        kTextBoxTextField);
            if (!text) return std::unexpected(text.error());
            auto value = machine.heap().string_value(*text);
            if (!value) return std::unexpected(value.error());
            if (value->size() > static_cast<usize>(*maximum)) {
                value->resize(static_cast<usize>(*maximum));
                auto replacement = create_string(machine, std::move(*value));
                if (!replacement)
                    return std::unexpected(replacement.error());
                auto text_stored = set_reference_field(
                    machine, *text_box, kTextBoxTextField, *replacement);
                auto caret_stored = set_int_field(
                    machine, *text_box, kTextBoxCaretField, *maximum);
                if (!text_stored)
                    return std::unexpected(text_stored.error());
                if (!caret_stored)
                    return std::unexpected(caret_stored.error());
            }
            auto stored = set_int_field(machine, *text_box,
                                        kTextBoxMaxSizeField, *maximum);
            if (!stored) return std::unexpected(stored.error());
            auto updated = emit_text_box(machine, *text_box,
                                         kEventItemUpdated);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value>(Value::from_int(*maximum));
        });
    add(registry, "javax/microedition/lcdui/TextBox", "setConstraints",
        "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            auto constraints = integer_argument(arguments, 1U);
            if (!text_box) return std::unexpected(text_box.error());
            if (!constraints) return std::unexpected(constraints.error());
            auto stored = set_int_field(machine, *text_box,
                                        kTextBoxConstraintsField,
                                        *constraints);
            if (!stored) return std::unexpected(stored.error());
            auto updated = emit_text_box(machine, *text_box,
                                         kEventItemUpdated);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/TextBox",
        "setInitialInputMode", "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto text_box = receiver(arguments);
            auto mode = reference_argument(arguments, 1U);
            if (!text_box) return std::unexpected(text_box.error());
            if (!mode) return std::unexpected(mode.error());
            auto stored = set_reference_field(machine, *text_box,
                                              kTextBoxInputModeField, *mode);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });

    const auto date_field_constructor = [&registry](const char* descriptor,
                                                     bool has_zone) {
        add(registry, "javax/microedition/lcdui/DateField", "<init>",
            descriptor,
            [has_zone](Machine& machine,
                       std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto item = receiver(arguments);
                auto label = reference_argument(arguments, 1U);
                auto mode = integer_argument(arguments, 2U);
                if (!item) return std::unexpected(item.error());
                if (!label) return std::unexpected(label.error());
                if (!mode) return std::unexpected(mode.error());
                if (*mode < 1 || *mode > 3) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DateField input mode is invalid");
                }
                ObjectRef zone;
                if (has_zone) {
                    auto parsed = reference_argument(arguments, 3U, false);
                    if (!parsed) return std::unexpected(parsed.error());
                    auto valid = machine.object_is_instance(
                        *parsed, "java/util/TimeZone");
                    if (!valid) return std::unexpected(valid.error());
                    if (!*valid) {
                        return fail_java("java/lang/IllegalArgumentException",
                                         "DateField timezone is invalid");
                    }
                    zone = *parsed;
                }
                auto initialized = initialize_item(machine, *item,
                                                   kTypeDateField, *label);
                if (!initialized)
                    return std::unexpected(initialized.error());
                auto date_stored = set_reference_field(
                    machine, *item, kDateFieldDateField, {});
                auto mode_stored = set_int_field(
                    machine, *item, kDateFieldInputModeField, *mode);
                auto zone_stored = set_reference_field(
                    machine, *item, kDateFieldTimeZoneField, zone);
                if (!date_stored)
                    return std::unexpected(date_stored.error());
                if (!mode_stored)
                    return std::unexpected(mode_stored.error());
                if (!zone_stored)
                    return std::unexpected(zone_stored.error());
                return std::optional<Value> {};
            });
    };
    date_field_constructor("(Ljava/lang/String;I)V", false);
    date_field_constructor(
        "(Ljava/lang/String;ILjava/util/TimeZone;)V", true);
    add(registry, "javax/microedition/lcdui/DateField", "getDate",
        "()Ljava/util/Date;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto date = reference_field(machine, *item, kDateFieldDateField);
            if (!date) return std::unexpected(date.error());
            return std::optional<Value>(Value::from_reference(*date));
        });
    add(registry, "javax/microedition/lcdui/DateField", "setDate",
        "(Ljava/util/Date;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto date = reference_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!date) return std::unexpected(date.error());
            if (!date->is_null()) {
                auto valid = machine.object_is_instance(*date,
                                                        "java/util/Date");
                if (!valid) return std::unexpected(valid.error());
                if (!*valid) {
                    return fail_java("java/lang/IllegalArgumentException",
                                     "DateField value is not a Date");
                }
            }
            auto stored = set_reference_field(machine, *item,
                                              kDateFieldDateField, *date);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/DateField", "getInputMode",
        "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            if (!item) return std::unexpected(item.error());
            auto mode = int_field(machine, *item, kDateFieldInputModeField);
            if (!mode) return std::unexpected(mode.error());
            return std::optional<Value>(Value::from_int(*mode));
        });
    add(registry, "javax/microedition/lcdui/DateField", "setInputMode",
        "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto mode = integer_argument(arguments, 1U);
            if (!item) return std::unexpected(item.error());
            if (!mode) return std::unexpected(mode.error());
            if (*mode < 1 || *mode > 3) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "DateField input mode is invalid");
            }
            auto stored = set_int_field(machine, *item,
                                        kDateFieldInputModeField, *mode);
            if (!stored) return std::unexpected(stored.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });

    add(registry, "javax/microedition/lcdui/Spacer", "<init>", "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto width = integer_argument(arguments, 1U);
            auto height = integer_argument(arguments, 2U);
            if (!item) return std::unexpected(item.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            if (*width < 0 || *height < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Spacer size must not be negative");
            }
            auto initialized = initialize_item(machine, *item,
                                               kTypeSpacer, {});
            if (!initialized)
                return std::unexpected(initialized.error());
            auto first = set_int_field(machine, *item,
                                       kSpacerWidthField, *width);
            auto second = set_int_field(machine, *item,
                                        kSpacerHeightField, *height);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            return std::optional<Value> {};
        });
    add(registry, "javax/microedition/lcdui/Spacer", "setMinimumSize",
        "(II)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto item = receiver(arguments);
            auto width = integer_argument(arguments, 1U);
            auto height = integer_argument(arguments, 2U);
            if (!item) return std::unexpected(item.error());
            if (!width) return std::unexpected(width.error());
            if (!height) return std::unexpected(height.error());
            if (*width < 0 || *height < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "Spacer size must not be negative");
            }
            auto first = set_int_field(machine, *item,
                                       kSpacerWidthField, *width);
            auto second = set_int_field(machine, *item,
                                        kSpacerHeightField, *height);
            if (!first) return std::unexpected(first.error());
            if (!second) return std::unexpected(second.error());
            auto updated = update_item_if_attached(machine, *item);
            if (!updated) return std::unexpected(updated.error());
            return std::optional<Value> {};
        });
    const auto spacer_getter = [&registry](const char* name,
                                           usize field_index) {
        add(registry, "javax/microedition/lcdui/Spacer", name, "()I",
            [field_index](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto item = receiver(arguments);
                if (!item) return std::unexpected(item.error());
                auto value = int_field(machine, *item, field_index);
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(Value::from_int(*value));
            });
    };
    spacer_getter("getMinimumWidth", kSpacerWidthField);
    spacer_getter("getMinimumHeight", kSpacerHeightField);
}

Status handle_lcdui_action(Machine& machine,
                           i32 kind,
                           i32 component_id,
                           i32 first,
                           i64 value64,
                           std::string text) {
    if (kind == 107) return {};
    auto component = machine.ui_component(component_id);
    if (!component) return std::unexpected(component.error());

    if (kind == 101 || kind == 102) {
        machine.emit_ui_event(UiBridgeEvent {
            .kind = 16,
            .component_id = component_id,
        });
        return {};
    }

    if (kind == 103) {
        auto is_text_field = machine.object_is_instance(
            *component, "javax/microedition/lcdui/TextField");
        auto is_text_box = machine.object_is_instance(
            *component, "javax/microedition/lcdui/TextBox");
        if (!is_text_field) return std::unexpected(is_text_field.error());
        if (!is_text_box) return std::unexpected(is_text_box.error());
        if (!*is_text_field && !*is_text_box) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI text action target is not a text control");
        }
        const usize text_field = *is_text_box
            ? kTextBoxTextField : kTextFieldTextField;
        const usize maximum_field = *is_text_box
            ? kTextBoxMaxSizeField : kTextFieldMaxSizeField;
        const usize caret_field = *is_text_box
            ? kTextBoxCaretField : kTextFieldCaretField;
        auto decoded = decode_ui_utf8(text);
        if (!decoded) return std::unexpected(decoded.error());
        auto maximum = int_field(machine, *component, maximum_field);
        if (!maximum) return std::unexpected(maximum.error());
        if (decoded->size() > static_cast<usize>(*maximum)) {
            decoded->resize(static_cast<usize>(*maximum));
        }
        auto string = create_string(machine, std::move(*decoded));
        if (!string) return std::unexpected(string.error());
        auto stored = set_reference_field(machine, *component,
                                          text_field, *string);
        if (!stored) return stored;
        auto length = text_length(machine, *string);
        if (!length) return std::unexpected(length.error());
        const i32 caret = std::clamp(first, 0, *length);
        auto caret_stored = set_int_field(machine, *component,
                                          caret_field, caret);
        if (!caret_stored) return caret_stored;
        return *is_text_box
            ? emit_text_box(machine, *component, kEventItemUpdated)
            : emit_item(machine, *component, kEventItemUpdated);
    }

    if (kind == 104) {
        const bool selected = value64 != 0;
        auto changed = handle_choice_action(machine, *component,
                                            first, selected);
        if (!changed) return changed;
        if (!selected) return {};

        auto command = implicit_choice_command(machine, *component);
        if (!command) return std::unexpected(command.error());
        if (!command->has_value()) return {};
        auto current = current_displayable(machine);
        if (!current) return std::unexpected(current.error());
        if (current->is_null() || *current != *component) return {};
        return dispatch_command_listener(machine, **command, *current);
    }

    if (kind == 105) {
        auto is_gauge = machine.object_is_instance(
            *component, "javax/microedition/lcdui/Gauge");
        if (!is_gauge) return std::unexpected(is_gauge.error());
        if (!*is_gauge) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI gauge action target is not a Gauge");
        }
        auto interactive = int_field(machine, *component,
                                     kGaugeInteractiveField);
        auto maximum = int_field(machine, *component, kGaugeMaxValueField);
        if (!interactive) return std::unexpected(interactive.error());
        if (!maximum) return std::unexpected(maximum.error());
        auto normalized = normalized_gauge_value(
            *interactive != 0, *maximum, first);
        if (!normalized) return std::unexpected(normalized.error());
        auto stored = set_int_field(machine, *component,
                                    kGaugeValueField, *normalized);
        if (!stored) return stored;
        return emit_item(machine, *component, kEventItemUpdated);
    }

    if (kind == 106) {
        auto is_date_field = machine.object_is_instance(
            *component, "javax/microedition/lcdui/DateField");
        if (!is_date_field) return std::unexpected(is_date_field.error());
        if (!*is_date_field) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI date action target is not a DateField");
        }
        if (value64 > std::numeric_limits<i64>::max() / 1'000 ||
            value64 < std::numeric_limits<i64>::min() / 1'000) {
            return fail(ErrorCode::overflow,
                        "LCDUI date value overflows milliseconds");
        }
        auto date = machine.class_states().allocate_instance(
            machine.heap(), "java/util/Date");
        if (!date) return std::unexpected(date.error());
        auto time_stored = set_long_field(machine, *date, kDateTimeField,
                                          value64 * 1'000);
        if (!time_stored) return std::unexpected(time_stored.error());
        auto date_stored = set_reference_field(machine, *component,
                                               kDateFieldDateField, *date);
        if (!date_stored) return std::unexpected(date_stored.error());
        return emit_item(machine, *component, kEventItemUpdated);
    }

    if (kind == 100) {
        auto is_command = machine.object_is_instance(
            *component, "javax/microedition/lcdui/Command");
        if (!is_command) return std::unexpected(is_command.error());
        if (!*is_command) {
            return fail(ErrorCode::invalid_argument,
                        "LCDUI command action target is not a Command");
        }
        auto current = current_displayable(machine);
        if (!current) return std::unexpected(current.error());
        if (current->is_null()) return {};
        return dispatch_command_listener(machine, *component, *current);
    }

    return fail(ErrorCode::unsupported_feature,
                "LCDUI action kind is not implemented");
}

} // namespace phoneme::vm
