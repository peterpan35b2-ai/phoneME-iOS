#include "Jdk8CompatNativesParts.hpp"

#include <array>
#include <chrono>
#include <limits>
#include <string>

#include "Jdk8CompatNativeSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace jdk8compat;

constexpr usize kAtomicValueField = 0U;
constexpr usize kLatchCountField = 0U;
constexpr usize kFutureCallableField = 0U;
constexpr usize kFutureRunnableField = 1U;
constexpr usize kFutureResultField = 2U;
constexpr usize kFutureThrowableField = 3U;
constexpr usize kFutureStateField = 4U;
constexpr usize kFutureRunnerField = 5U;
constexpr i32 kFutureNew = 0;
constexpr i32 kFutureRunning = 1;
constexpr i32 kFutureDone = 2;
constexpr i32 kFutureCancelled = 3;
constexpr usize kExecutorShutdownField = 0U;
constexpr usize kExecutorWorkersField = 1U;
constexpr usize kCallableRunnableField = 0U;
constexpr usize kCallableResultField = 1U;

[[nodiscard]] Result<ObjectRef> decimal_string(Machine& machine,
                                               std::string text) {
    std::u16string utf16;
    utf16.reserve(text.size());
    for (const char character : text) {
        utf16.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    return create_string(machine, std::move(utf16));
}

[[nodiscard]] Result<i32> apply_int_unary(Machine& machine,
                                          ObjectRef operation,
                                          i32 value) {
    const Value argument = Value::from_int(value);
    auto result = invoke_checked(
        machine, operation, "java/util/function/IntUnaryOperator", "applyAsInt",
        "(I)I", std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "IntUnaryOperator.applyAsInt returned no value");
    }
    return result->value().as_int();
}

[[nodiscard]] Result<i32> apply_int_binary(Machine& machine,
                                           ObjectRef operation,
                                           i32 left,
                                           i32 right) {
    const std::array<Value, 2> arguments {
        Value::from_int(left), Value::from_int(right),
    };
    auto result = invoke_checked(
        machine, operation, "java/util/function/IntBinaryOperator", "applyAsInt",
        "(II)I", arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "IntBinaryOperator.applyAsInt returned no value");
    }
    return result->value().as_int();
}

[[nodiscard]] Result<i64> apply_long_unary(Machine& machine,
                                           ObjectRef operation,
                                           i64 value) {
    const Value argument = Value::from_long(value);
    auto result = invoke_checked(
        machine, operation, "java/util/function/LongUnaryOperator", "applyAsLong",
        "(J)J", std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "LongUnaryOperator.applyAsLong returned no value");
    }
    return result->value().as_long();
}

[[nodiscard]] Result<i64> apply_long_binary(Machine& machine,
                                            ObjectRef operation,
                                            i64 left,
                                            i64 right) {
    const std::array<Value, 2> arguments {
        Value::from_long(left), Value::from_long(right),
    };
    auto result = invoke_checked(
        machine, operation, "java/util/function/LongBinaryOperator", "applyAsLong",
        "(JJ)J", arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "LongBinaryOperator.applyAsLong returned no value");
    }
    return result->value().as_long();
}

[[nodiscard]] Result<ObjectRef> apply_reference_unary(Machine& machine,
                                                      ObjectRef operation,
                                                      ObjectRef value) {
    const Value argument = Value::from_reference(value);
    auto result = invoke_checked(
        machine, operation, "java/util/function/UnaryOperator", "apply",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "UnaryOperator.apply returned no value");
    }
    return result->value().as_reference();
}

[[nodiscard]] Result<ObjectRef> apply_reference_binary(Machine& machine,
                                                       ObjectRef operation,
                                                       ObjectRef left,
                                                       ObjectRef right) {
    const std::array<Value, 2> arguments {
        Value::from_reference(left), Value::from_reference(right),
    };
    auto result = invoke_checked(
        machine, operation, "java/util/function/BinaryOperator", "apply",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", arguments);
    if (!result) return std::unexpected(result.error());
    if (!result->has_value()) {
        return fail(ErrorCode::internal_error,
                    "BinaryOperator.apply returned no value");
    }
    return result->value().as_reference();
}

void register_atomic_integer(NativeMethodRegistry& registry) {
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = set_int_field(machine, *object, kAtomicValueField, 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto initial = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!initial) return std::unexpected(initial.error());
            auto stored = set_int_field(machine, *object, kAtomicValueField,
                                        *initial);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    const auto get = [](Machine& machine, std::span<const Value> arguments)
        -> Result<std::optional<Value>> {
        auto object = receiver(arguments);
        if (!object) return std::unexpected(object.error());
        auto value = int_field(machine, *object, kAtomicValueField);
        if (!value) return std::unexpected(value.error());
        return std::optional<Value>(Value::from_int(*value));
    };
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "get", "()I", get);
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "intValue", "()I", get);

    const auto set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicInteger", name, "(I)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = int_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(machine, *object,
                                            kAtomicValueField, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    set("set");
    set("lazySet");

    add(registry, "java/util/concurrent/atomic/AtomicInteger", "getAndSet", "(I)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto desired = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!desired) return std::unexpected(desired.error());
            auto current = int_field(machine, *object, kAtomicValueField);
            if (!current) return std::unexpected(current.error());
            auto stored = set_int_field(machine, *object, kAtomicValueField,
                                        *desired);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(*current));
        });

    const auto compare_and_set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicInteger", name, "(II)Z",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto expected = int_argument(arguments, 1U);
                auto desired = int_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!expected) return std::unexpected(expected.error());
                if (!desired) return std::unexpected(desired.error());
                auto current = int_field(machine, *object, kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                if (*current != *expected) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto stored = set_int_field(machine, *object, kAtomicValueField,
                                            *desired);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(1));
            });
    };
    compare_and_set("compareAndSet");
    compare_and_set("weakCompareAndSet");

    const auto arithmetic = [&registry](const char* name, i32 implicit_delta,
                                        bool return_updated) {
        const bool explicit_delta = implicit_delta == 0;
        add(registry, "java/util/concurrent/atomic/AtomicInteger", name,
            explicit_delta ? "(I)I" : "()I",
            [implicit_delta, explicit_delta, return_updated](
                Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                i32 delta = implicit_delta;
                if (explicit_delta) {
                    auto argument = int_argument(arguments, 1U);
                    if (!argument) return std::unexpected(argument.error());
                    delta = *argument;
                }
                auto current = int_field(machine, *object, kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                const i32 updated = static_cast<i32>(
                    static_cast<u32>(*current) + static_cast<u32>(delta));
                auto stored = set_int_field(machine, *object, kAtomicValueField,
                                            updated);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(
                    return_updated ? updated : *current));
            });
    };
    arithmetic("getAndIncrement", 1, false);
    arithmetic("getAndDecrement", -1, false);
    arithmetic("getAndAdd", 0, false);
    arithmetic("incrementAndGet", 1, true);
    arithmetic("decrementAndGet", -1, true);
    arithmetic("addAndGet", 0, true);

    const auto update = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicInteger", name,
            "(Ljava/util/function/IntUnaryOperator;)I",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto operation = reference_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = int_field(machine, *object, kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_int_unary(machine, *operation, *current);
                    if (!next) return std::unexpected(next.error());
                    auto observed = int_field(machine, *object, kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_int_field(machine, *object,
                                                kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_int(
                        return_updated ? *next : *current));
                }
            });
    };
    update("getAndUpdate", false);
    update("updateAndGet", true);

    const auto accumulate = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicInteger", name,
            "(ILjava/util/function/IntBinaryOperator;)I",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = int_argument(arguments, 1U);
                auto operation = reference_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = int_field(machine, *object, kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_int_binary(
                        machine, *operation, *current, *value);
                    if (!next) return std::unexpected(next.error());
                    auto observed = int_field(machine, *object, kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_int_field(machine, *object,
                                                kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_int(
                        return_updated ? *next : *current));
                }
            });
    };
    accumulate("getAndAccumulate", false);
    accumulate("accumulateAndGet", true);

    add(registry, "java/util/concurrent/atomic/AtomicInteger", "longValue", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_long(*value));
        });
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "floatValue", "()F",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_float(
                static_cast<float>(*value)));
        });
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "doubleValue", "()D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(
                static_cast<double>(*value)));
        });
    add(registry, "java/util/concurrent/atomic/AtomicInteger", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            auto text = decimal_string(machine, std::to_string(*value));
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

void register_atomic_long(NativeMethodRegistry& registry) {
    add(registry, "java/util/concurrent/atomic/AtomicLong", "<init>", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto stored = set_long_field(machine, *object, kAtomicValueField, 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/concurrent/atomic/AtomicLong", "<init>", "(J)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto initial = long_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!initial) return std::unexpected(initial.error());
            auto stored = set_long_field(machine, *object, kAtomicValueField,
                                         *initial);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    const auto get = [](Machine& machine, std::span<const Value> arguments)
        -> Result<std::optional<Value>> {
        auto object = receiver(arguments);
        if (!object) return std::unexpected(object.error());
        auto value = long_field(machine, *object, kAtomicValueField);
        if (!value) return std::unexpected(value.error());
        return std::optional<Value>(Value::from_long(*value));
    };
    add(registry, "java/util/concurrent/atomic/AtomicLong", "get", "()J", get);
    add(registry, "java/util/concurrent/atomic/AtomicLong", "longValue", "()J", get);

    const auto set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicLong", name, "(J)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = long_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_long_field(machine, *object,
                                             kAtomicValueField, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    set("set");
    set("lazySet");

    add(registry, "java/util/concurrent/atomic/AtomicLong", "getAndSet", "(J)J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto desired = long_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!desired) return std::unexpected(desired.error());
            auto current = long_field(machine, *object, kAtomicValueField);
            if (!current) return std::unexpected(current.error());
            auto stored = set_long_field(machine, *object, kAtomicValueField,
                                         *desired);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_long(*current));
        });

    const auto compare_and_set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicLong", name, "(JJ)Z",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto expected = long_argument(arguments, 1U);
                auto desired = long_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!expected) return std::unexpected(expected.error());
                if (!desired) return std::unexpected(desired.error());
                auto current = long_field(machine, *object, kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                if (*current != *expected) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto stored = set_long_field(machine, *object, kAtomicValueField,
                                             *desired);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(1));
            });
    };
    compare_and_set("compareAndSet");
    compare_and_set("weakCompareAndSet");

    const auto arithmetic = [&registry](const char* name, i64 implicit_delta,
                                        bool return_updated) {
        const bool explicit_delta = implicit_delta == 0;
        add(registry, "java/util/concurrent/atomic/AtomicLong", name,
            explicit_delta ? "(J)J" : "()J",
            [implicit_delta, explicit_delta, return_updated](
                Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                i64 delta = implicit_delta;
                if (explicit_delta) {
                    auto argument = long_argument(arguments, 1U);
                    if (!argument) return std::unexpected(argument.error());
                    delta = *argument;
                }
                auto current = long_field(machine, *object, kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                const i64 updated = static_cast<i64>(
                    static_cast<u64>(*current) + static_cast<u64>(delta));
                auto stored = set_long_field(machine, *object, kAtomicValueField,
                                             updated);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_long(
                    return_updated ? updated : *current));
            });
    };
    arithmetic("getAndIncrement", 1, false);
    arithmetic("getAndDecrement", -1, false);
    arithmetic("getAndAdd", 0, false);
    arithmetic("incrementAndGet", 1, true);
    arithmetic("decrementAndGet", -1, true);
    arithmetic("addAndGet", 0, true);

    const auto update = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicLong", name,
            "(Ljava/util/function/LongUnaryOperator;)J",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto operation = reference_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = long_field(machine, *object, kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_long_unary(machine, *operation, *current);
                    if (!next) return std::unexpected(next.error());
                    auto observed = long_field(machine, *object, kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_long_field(machine, *object,
                                                 kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_long(
                        return_updated ? *next : *current));
                }
            });
    };
    update("getAndUpdate", false);
    update("updateAndGet", true);

    const auto accumulate = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicLong", name,
            "(JLjava/util/function/LongBinaryOperator;)J",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = long_argument(arguments, 1U);
                auto operation = reference_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = long_field(machine, *object, kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_long_binary(
                        machine, *operation, *current, *value);
                    if (!next) return std::unexpected(next.error());
                    auto observed = long_field(machine, *object, kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_long_field(machine, *object,
                                                 kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_long(
                        return_updated ? *next : *current));
                }
            });
    };
    accumulate("getAndAccumulate", false);
    accumulate("accumulateAndGet", true);

    add(registry, "java/util/concurrent/atomic/AtomicLong", "intValue", "()I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = long_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(static_cast<i32>(*value)));
        });
    add(registry, "java/util/concurrent/atomic/AtomicLong", "floatValue", "()F",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = long_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_float(
                static_cast<float>(*value)));
        });
    add(registry, "java/util/concurrent/atomic/AtomicLong", "doubleValue", "()D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = long_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(
                static_cast<double>(*value)));
        });
    add(registry, "java/util/concurrent/atomic/AtomicLong", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = long_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            auto text = decimal_string(machine, std::to_string(*value));
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

void register_atomic_boolean(NativeMethodRegistry& registry) {
    const auto initialize = [&registry](const char* descriptor, bool has_value) {
        add(registry, "java/util/concurrent/atomic/AtomicBoolean", "<init>",
            descriptor,
            [has_value](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                i32 initial = 0;
                if (has_value) {
                    auto value = int_argument(arguments, 1U);
                    if (!value) return std::unexpected(value.error());
                    initial = *value != 0 ? 1 : 0;
                }
                auto stored = set_int_field(machine, *object,
                                            kAtomicValueField, initial);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    initialize("()V", false);
    initialize("(Z)V", true);

    add(registry, "java/util/concurrent/atomic/AtomicBoolean", "get", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_int(*value != 0 ? 1 : 0));
        });

    const auto set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicBoolean", name, "(Z)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = int_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_int_field(machine, *object,
                                            kAtomicValueField,
                                            *value != 0 ? 1 : 0);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    set("set");
    set("lazySet");

    add(registry, "java/util/concurrent/atomic/AtomicBoolean", "getAndSet", "(Z)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto desired = int_argument(arguments, 1U);
            if (!object) return std::unexpected(object.error());
            if (!desired) return std::unexpected(desired.error());
            auto current = int_field(machine, *object, kAtomicValueField);
            if (!current) return std::unexpected(current.error());
            auto stored = set_int_field(machine, *object, kAtomicValueField,
                                        *desired != 0 ? 1 : 0);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_int(*current != 0 ? 1 : 0));
        });

    const auto compare_and_set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicBoolean", name, "(ZZ)Z",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto expected = int_argument(arguments, 1U);
                auto desired = int_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!expected) return std::unexpected(expected.error());
                if (!desired) return std::unexpected(desired.error());
                auto current = int_field(machine, *object, kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                if ((*current != 0) != (*expected != 0)) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto stored = set_int_field(machine, *object, kAtomicValueField,
                                            *desired != 0 ? 1 : 0);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(1));
            });
    };
    compare_and_set("compareAndSet");
    compare_and_set("weakCompareAndSet");

    add(registry, "java/util/concurrent/atomic/AtomicBoolean", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = int_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            auto text = create_string(machine, *value != 0
                ? std::u16string(u"true") : std::u16string(u"false"));
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

void register_atomic_reference(NativeMethodRegistry& registry) {
    const auto initialize = [&registry](const char* descriptor, bool has_value) {
        add(registry, "java/util/concurrent/atomic/AtomicReference", "<init>",
            descriptor,
            [has_value](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                if (!object) return std::unexpected(object.error());
                ObjectRef initial {};
                if (has_value) {
                    auto value = reference_argument(arguments, 1U, true);
                    if (!value) return std::unexpected(value.error());
                    initial = *value;
                }
                auto stored = set_reference_field(machine, *object,
                                                  kAtomicValueField, initial);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    initialize("()V", false);
    initialize("(Ljava/lang/Object;)V", true);

    add(registry, "java/util/concurrent/atomic/AtomicReference", "get",
        "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_reference(*value));
        });

    const auto set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicReference", name,
            "(Ljava/lang/Object;)V",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = reference_argument(arguments, 1U, true);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                auto stored = set_reference_field(machine, *object,
                                                  kAtomicValueField, *value);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value> {};
            });
    };
    set("set");
    set("lazySet");

    add(registry, "java/util/concurrent/atomic/AtomicReference", "getAndSet",
        "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            auto desired = reference_argument(arguments, 1U, true);
            if (!object) return std::unexpected(object.error());
            if (!desired) return std::unexpected(desired.error());
            auto current = reference_field(machine, *object, kAtomicValueField);
            if (!current) return std::unexpected(current.error());
            auto stored = set_reference_field(machine, *object,
                                              kAtomicValueField, *desired);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value>(Value::from_reference(*current));
        });

    const auto compare_and_set = [&registry](const char* name) {
        add(registry, "java/util/concurrent/atomic/AtomicReference", name,
            "(Ljava/lang/Object;Ljava/lang/Object;)Z",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto expected = reference_argument(arguments, 1U, true);
                auto desired = reference_argument(arguments, 2U, true);
                if (!object) return std::unexpected(object.error());
                if (!expected) return std::unexpected(expected.error());
                if (!desired) return std::unexpected(desired.error());
                auto current = reference_field(machine, *object,
                                               kAtomicValueField);
                if (!current) return std::unexpected(current.error());
                if (*current != *expected) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto stored = set_reference_field(machine, *object,
                                                  kAtomicValueField, *desired);
                if (!stored) return std::unexpected(stored.error());
                return std::optional<Value>(Value::from_int(1));
            });
    };
    compare_and_set("compareAndSet");
    compare_and_set("weakCompareAndSet");

    const auto update = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicReference", name,
            "(Ljava/util/function/UnaryOperator;)Ljava/lang/Object;",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto operation = reference_argument(arguments, 1U);
                if (!object) return std::unexpected(object.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = reference_field(machine, *object,
                                                   kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_reference_unary(
                        machine, *operation, *current);
                    if (!next) return std::unexpected(next.error());
                    auto observed = reference_field(machine, *object,
                                                    kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_reference_field(machine, *object,
                                                      kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_reference(
                        return_updated ? *next : *current));
                }
            });
    };
    update("getAndUpdate", false);
    update("updateAndGet", true);

    const auto accumulate = [&registry](const char* name, bool return_updated) {
        add(registry, "java/util/concurrent/atomic/AtomicReference", name,
            "(Ljava/lang/Object;Ljava/util/function/BinaryOperator;)Ljava/lang/Object;",
            [return_updated](Machine& machine,
                             std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto object = receiver(arguments);
                auto value = reference_argument(arguments, 1U, true);
                auto operation = reference_argument(arguments, 2U);
                if (!object) return std::unexpected(object.error());
                if (!value) return std::unexpected(value.error());
                if (!operation) return std::unexpected(operation.error());
                for (;;) {
                    auto current = reference_field(machine, *object,
                                                   kAtomicValueField);
                    if (!current) return std::unexpected(current.error());
                    auto next = apply_reference_binary(
                        machine, *operation, *current, *value);
                    if (!next) return std::unexpected(next.error());
                    auto observed = reference_field(machine, *object,
                                                    kAtomicValueField);
                    if (!observed) return std::unexpected(observed.error());
                    if (*observed != *current) continue;
                    auto stored = set_reference_field(machine, *object,
                                                      kAtomicValueField, *next);
                    if (!stored) return std::unexpected(stored.error());
                    return std::optional<Value>(Value::from_reference(
                        return_updated ? *next : *current));
                }
            });
    };
    accumulate("getAndAccumulate", false);
    accumulate("accumulateAndGet", true);

    add(registry, "java/util/concurrent/atomic/AtomicReference", "toString",
        "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = receiver(arguments);
            if (!object) return std::unexpected(object.error());
            auto value = reference_field(machine, *object, kAtomicValueField);
            if (!value) return std::unexpected(value.error());
            if (value->is_null()) {
                auto text = create_string(machine, std::u16string(u"null"));
                if (!text) return std::unexpected(text.error());
                return std::optional<Value>(Value::from_reference(*text));
            }
            auto rendered = invoke_checked(machine, *value, "java/lang/Object",
                                           "toString", "()Ljava/lang/String;");
            if (!rendered) return std::unexpected(rendered.error());
            if (!rendered->has_value()) {
                return fail(ErrorCode::internal_error,
                            "Object.toString returned no value");
            }
            return rendered->value();
        });
}

[[nodiscard]] Result<i64> time_unit_millis(Machine& machine,
                                           ObjectRef unit,
                                           i64 duration) {
    const Value argument = Value::from_long(duration);
    auto converted = invoke_checked(machine, unit, "java/util/concurrent/TimeUnit",
                                    "toMillis", "(J)J",
                                    std::span<const Value>(&argument, 1U));
    if (!converted) return std::unexpected(converted.error());
    if (!converted->has_value()) {
        return fail(ErrorCode::internal_error,
                    "TimeUnit.toMillis returned no value");
    }
    return converted->value().as_long();
}

[[nodiscard]] Status cooperative_wait_tick(Machine& machine) {
    auto waited = machine.sleep_current_thread(1);
    if (!waited) return std::unexpected(waited.error());
    if (*waited == SchedulerWaitResult::interrupted) {
        return fail_java("java/lang/InterruptedException",
                         "concurrent wait was interrupted");
    }
    return {};
}

void register_count_down_latch(NativeMethodRegistry& registry) {
    constexpr std::string_view kOwner = "java/util/concurrent/CountDownLatch";
    add(registry, std::string(kOwner), "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            auto count = int_argument(arguments, 1U);
            if (!latch) return std::unexpected(latch.error());
            if (!count) return std::unexpected(count.error());
            if (*count < 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "CountDownLatch count is negative");
            }
            auto stored = set_long_field(machine, *latch, kLatchCountField,
                                         static_cast<i64>(*count));
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "getCount", "()J",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            if (!latch) return std::unexpected(latch.error());
            auto count = long_field(machine, *latch, kLatchCountField);
            if (!count) return std::unexpected(count.error());
            return std::optional<Value>(Value::from_long(*count));
        });
    add(registry, std::string(kOwner), "countDown", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            if (!latch) return std::unexpected(latch.error());
            auto count = long_field(machine, *latch, kLatchCountField);
            if (!count) return std::unexpected(count.error());
            if (*count > 0) {
                auto stored = set_long_field(machine, *latch, kLatchCountField,
                                             *count - 1);
                if (!stored) return std::unexpected(stored.error());
            }
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "await", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            if (!latch) return std::unexpected(latch.error());
            for (;;) {
                auto count = long_field(machine, *latch, kLatchCountField);
                if (!count) return std::unexpected(count.error());
                if (*count == 0) return std::optional<Value> {};
                auto waited = cooperative_wait_tick(machine);
                if (!waited) return std::unexpected(waited.error());
            }
        });
    add(registry, std::string(kOwner), "await",
        "(JLjava/util/concurrent/TimeUnit;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            auto duration = long_argument(arguments, 1U);
            auto unit = reference_argument(arguments, 2U);
            if (!latch) return std::unexpected(latch.error());
            if (!duration) return std::unexpected(duration.error());
            if (!unit) return std::unexpected(unit.error());
            auto count = long_field(machine, *latch, kLatchCountField);
            if (!count) return std::unexpected(count.error());
            if (*count == 0) {
                return std::optional<Value>(Value::from_int(1));
            }
            if (*duration <= 0) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto millis = time_unit_millis(machine, *unit, *duration);
            if (!millis) return std::unexpected(millis.error());
            i64 wait_millis = *millis;
            if (wait_millis <= 0) wait_millis = 1;
            const auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(wait_millis);
            for (;;) {
                count = long_field(machine, *latch, kLatchCountField);
                if (!count) return std::unexpected(count.error());
                if (*count == 0) {
                    return std::optional<Value>(Value::from_int(1));
                }
                if (std::chrono::steady_clock::now() - start >= timeout) {
                    return std::optional<Value>(Value::from_int(0));
                }
                auto waited = cooperative_wait_tick(machine);
                if (!waited) return std::unexpected(waited.error());
            }
        });
    add(registry, std::string(kOwner), "toString", "()Ljava/lang/String;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto latch = receiver(arguments);
            if (!latch) return std::unexpected(latch.error());
            auto count = long_field(machine, *latch, kLatchCountField);
            if (!count) return std::unexpected(count.error());
            auto text = create_string(
                machine, std::u16string(u"CountDownLatch[Count = ") +
                    [&] {
                        std::u16string digits;
                        const std::string ascii = std::to_string(*count);
                        digits.reserve(ascii.size());
                        for (const char ch : ascii) {
                            digits.push_back(static_cast<char16_t>(
                                static_cast<unsigned char>(ch)));
                        }
                        return digits;
                    }() + u"]");
            if (!text) return std::unexpected(text.error());
            return std::optional<Value>(Value::from_reference(*text));
        });
}

[[nodiscard]] Result<std::optional<Value>> future_result(Machine& machine,
                                                         ObjectRef task) {
    auto state = int_field(machine, task, kFutureStateField);
    if (!state) return std::unexpected(state.error());
    if (*state == kFutureCancelled) {
        return fail_java("java/util/concurrent/CancellationException",
                         "FutureTask was cancelled");
    }
    if (*state != kFutureDone) {
        return fail(ErrorCode::invalid_state,
                    "FutureTask result requested before completion");
    }
    auto throwable = reference_field(machine, task, kFutureThrowableField);
    if (!throwable) return std::unexpected(throwable.error());
    if (!throwable->is_null()) {
        return fail_java("java/util/concurrent/ExecutionException",
                         "FutureTask computation failed");
    }
    auto result = reference_field(machine, task, kFutureResultField);
    if (!result) return std::unexpected(result.error());
    return std::optional<Value>(Value::from_reference(*result));
}

[[nodiscard]] Status finish_future_task(Machine& machine,
                                        ObjectRef task,
                                        ObjectRef result,
                                        ObjectRef throwable) {
    auto result_stored = set_reference_field(machine, task, kFutureResultField,
                                             result);
    auto throwable_stored = set_reference_field(
        machine, task, kFutureThrowableField, throwable);
    auto runner_cleared = set_reference_field(machine, task, kFutureRunnerField,
                                              {});
    auto state_stored = set_int_field(machine, task, kFutureStateField,
                                      kFutureDone);
    if (!result_stored) return result_stored;
    if (!throwable_stored) return throwable_stored;
    if (!runner_cleared) return runner_cleared;
    return state_stored;
}

void register_future_task(NativeMethodRegistry& registry) {
    constexpr std::string_view kOwner = "java/util/concurrent/FutureTask";
    add(registry, std::string(kOwner), "<init>",
        "(Ljava/util/concurrent/Callable;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto callable = reference_argument(arguments, 1U);
            if (!task) return std::unexpected(task.error());
            if (!callable) return std::unexpected(callable.error());
            auto callable_stored = set_reference_field(
                machine, *task, kFutureCallableField, *callable);
            auto runnable_stored = set_reference_field(
                machine, *task, kFutureRunnableField, {});
            auto result_stored = set_reference_field(
                machine, *task, kFutureResultField, {});
            auto throwable_stored = set_reference_field(
                machine, *task, kFutureThrowableField, {});
            auto state_stored = set_int_field(machine, *task,
                                              kFutureStateField, kFutureNew);
            auto runner_stored = set_reference_field(
                machine, *task, kFutureRunnerField, {});
            if (!callable_stored) return std::unexpected(callable_stored.error());
            if (!runnable_stored) return std::unexpected(runnable_stored.error());
            if (!result_stored) return std::unexpected(result_stored.error());
            if (!throwable_stored) return std::unexpected(throwable_stored.error());
            if (!state_stored) return std::unexpected(state_stored.error());
            if (!runner_stored) return std::unexpected(runner_stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "<init>",
        "(Ljava/lang/Runnable;Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto runnable = reference_argument(arguments, 1U);
            auto result = reference_argument(arguments, 2U, true);
            if (!task) return std::unexpected(task.error());
            if (!runnable) return std::unexpected(runnable.error());
            if (!result) return std::unexpected(result.error());
            auto callable_stored = set_reference_field(
                machine, *task, kFutureCallableField, {});
            auto runnable_stored = set_reference_field(
                machine, *task, kFutureRunnableField, *runnable);
            auto result_stored = set_reference_field(
                machine, *task, kFutureResultField, *result);
            auto throwable_stored = set_reference_field(
                machine, *task, kFutureThrowableField, {});
            auto state_stored = set_int_field(machine, *task,
                                              kFutureStateField, kFutureNew);
            auto runner_stored = set_reference_field(
                machine, *task, kFutureRunnerField, {});
            if (!callable_stored) return std::unexpected(callable_stored.error());
            if (!runnable_stored) return std::unexpected(runnable_stored.error());
            if (!result_stored) return std::unexpected(result_stored.error());
            if (!throwable_stored) return std::unexpected(throwable_stored.error());
            if (!state_stored) return std::unexpected(state_stored.error());
            if (!runner_stored) return std::unexpected(runner_stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "isCancelled", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            return std::optional<Value>(Value::from_int(
                *state == kFutureCancelled ? 1 : 0));
        });
    add(registry, std::string(kOwner), "isDone", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            return std::optional<Value>(Value::from_int(
                *state == kFutureDone || *state == kFutureCancelled ? 1 : 0));
        });
    add(registry, std::string(kOwner), "cancel", "(Z)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto interrupt = int_argument(arguments, 1U);
            if (!task) return std::unexpected(task.error());
            if (!interrupt) return std::unexpected(interrupt.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            if (*state == kFutureDone || *state == kFutureCancelled) {
                return std::optional<Value>(Value::from_int(0));
            }
            if (*interrupt != 0) {
                auto runner = reference_field(machine, *task, kFutureRunnerField);
                if (!runner) return std::unexpected(runner.error());
                if (!runner->is_null()) {
                    const Value receiver_value = Value::from_reference(*runner);
                    auto interrupted = invoke_native(
                        machine, "java/lang/Thread", "interrupt", "()V",
                        std::span<const Value>(&receiver_value, 1U));
                    if (!interrupted) {
                        return std::unexpected(interrupted.error());
                    }
                }
            }
            auto runner_cleared = set_reference_field(
                machine, *task, kFutureRunnerField, {});
            auto state_stored = set_int_field(machine, *task, kFutureStateField,
                                              kFutureCancelled);
            if (!runner_cleared) return std::unexpected(runner_cleared.error());
            if (!state_stored) return std::unexpected(state_stored.error());
            return std::optional<Value>(Value::from_int(1));
        });
    add(registry, std::string(kOwner), "get", "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            for (;;) {
                auto state = int_field(machine, *task, kFutureStateField);
                if (!state) return std::unexpected(state.error());
                if (*state == kFutureDone || *state == kFutureCancelled) {
                    return future_result(machine, *task);
                }
                auto waited = cooperative_wait_tick(machine);
                if (!waited) return std::unexpected(waited.error());
            }
        });
    add(registry, std::string(kOwner), "get",
        "(JLjava/util/concurrent/TimeUnit;)Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto duration = long_argument(arguments, 1U);
            auto unit = reference_argument(arguments, 2U);
            if (!task) return std::unexpected(task.error());
            if (!duration) return std::unexpected(duration.error());
            if (!unit) return std::unexpected(unit.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            if (*state == kFutureDone || *state == kFutureCancelled) {
                return future_result(machine, *task);
            }
            if (*duration <= 0) {
                return fail_java("java/util/concurrent/TimeoutException",
                                 "FutureTask timed out");
            }
            auto millis = time_unit_millis(machine, *unit, *duration);
            if (!millis) return std::unexpected(millis.error());
            i64 wait_millis = *millis;
            if (wait_millis <= 0) wait_millis = 1;
            const auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(wait_millis);
            for (;;) {
                state = int_field(machine, *task, kFutureStateField);
                if (!state) return std::unexpected(state.error());
                if (*state == kFutureDone || *state == kFutureCancelled) {
                    return future_result(machine, *task);
                }
                if (std::chrono::steady_clock::now() - start >= timeout) {
                    return fail_java("java/util/concurrent/TimeoutException",
                                     "FutureTask timed out");
                }
                auto waited = cooperative_wait_tick(machine);
                if (!waited) return std::unexpected(waited.error());
            }
        });
    add(registry, std::string(kOwner), "done", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "set", "(Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto value = reference_argument(arguments, 1U, true);
            if (!task) return std::unexpected(task.error());
            if (!value) return std::unexpected(value.error());
            auto finished = finish_future_task(machine, *task, *value, {});
            if (!finished) return std::unexpected(finished.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "setException",
        "(Ljava/lang/Throwable;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            auto throwable = reference_argument(arguments, 1U);
            if (!task) return std::unexpected(task.error());
            if (!throwable) return std::unexpected(throwable.error());
            auto finished = finish_future_task(machine, *task, {}, *throwable);
            if (!finished) return std::unexpected(finished.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "run", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            if (*state != kFutureNew) return std::optional<Value> {};
            auto running = set_int_field(machine, *task, kFutureStateField,
                                         kFutureRunning);
            if (!running) return std::unexpected(running.error());
            auto current_thread = machine.current_java_thread();
            if (!current_thread) return std::unexpected(current_thread.error());
            auto runner_stored = set_reference_field(
                machine, *task, kFutureRunnerField, *current_thread);
            if (!runner_stored) return std::unexpected(runner_stored.error());

            ObjectRef result {};
            ObjectRef thrown {};
            auto callable = reference_field(machine, *task, kFutureCallableField);
            auto runnable = reference_field(machine, *task, kFutureRunnableField);
            if (!callable) return std::unexpected(callable.error());
            if (!runnable) return std::unexpected(runnable.error());
            if (!callable->is_null()) {
                auto invoked = machine.invoke_instance(
                    *callable, "java/util/concurrent/Callable", "call",
                    "()Ljava/lang/Object;", {});
                if (!invoked) return std::unexpected(invoked.error());
                if (invoked->throwable.has_value()) {
                    thrown = *invoked->throwable;
                } else if (invoked->return_value.has_value()) {
                    auto returned = invoked->return_value->as_reference();
                    if (!returned) return std::unexpected(returned.error());
                    result = *returned;
                }
            } else if (!runnable->is_null()) {
                auto invoked = machine.invoke_instance(
                    *runnable, "java/lang/Runnable", "run", "()V", {});
                if (!invoked) return std::unexpected(invoked.error());
                if (invoked->throwable.has_value()) {
                    thrown = *invoked->throwable;
                } else {
                    auto configured = reference_field(
                        machine, *task, kFutureResultField);
                    if (!configured) return std::unexpected(configured.error());
                    result = *configured;
                }
            }
            state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            if (*state == kFutureCancelled) return std::optional<Value> {};
            auto finished = finish_future_task(machine, *task, result, thrown);
            if (!finished) return std::unexpected(finished.error());
            auto callback = machine.invoke_instance(
                *task, "java/util/concurrent/FutureTask", "done", "()V", {});
            if (!callback) return std::unexpected(callback.error());
            if (callback->throwable.has_value()) {
                auto throwable_class = machine.heap().class_name(
                    *callback->throwable);
                if (!throwable_class) {
                    return std::unexpected(throwable_class.error());
                }
                return fail_java(*throwable_class,
                                 "FutureTask.done threw");
            }
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "runAndReset", "()Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto task = receiver(arguments);
            if (!task) return std::unexpected(task.error());
            const Value task_value = Value::from_reference(*task);
            auto ran = invoke_native(machine, "java/util/concurrent/FutureTask",
                                     "run", "()V",
                                     std::span<const Value>(&task_value, 1U));
            if (!ran) return std::unexpected(ran.error());
            auto state = int_field(machine, *task, kFutureStateField);
            if (!state) return std::unexpected(state.error());
            auto throwable = reference_field(machine, *task,
                                             kFutureThrowableField);
            if (!throwable) return std::unexpected(throwable.error());
            if (*state != kFutureDone || !throwable->is_null()) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto reset = set_int_field(machine, *task, kFutureStateField,
                                       kFutureNew);
            auto result_cleared = set_reference_field(
                machine, *task, kFutureResultField, {});
            if (!reset) return std::unexpected(reset.error());
            if (!result_cleared) return std::unexpected(result_cleared.error());
            return std::optional<Value>(Value::from_int(1));
        });
}

[[nodiscard]] Result<ObjectRef> start_runnable_thread(Machine& machine,
                                                      ObjectRef runnable) {
    auto thread = new_instance(machine, "java/lang/Thread");
    if (!thread) return std::unexpected(thread.error());
    auto root = machine.pin_native_root(*thread);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> init_arguments {
        Value::from_reference(*thread), Value::from_reference(runnable),
    };
    auto initialized = invoke_native(machine, "java/lang/Thread", "<init>",
                                     "(Ljava/lang/Runnable;)V",
                                     init_arguments);
    if (!initialized) return std::unexpected(initialized.error());
    const Value thread_value = Value::from_reference(*thread);
    auto started = invoke_native(machine, "java/lang/Thread", "start", "()V",
                                 std::span<const Value>(&thread_value, 1U));
    if (!started) return std::unexpected(started.error());
    return *thread;
}

[[nodiscard]] Result<ObjectRef> create_future_task(Machine& machine,
                                                   ObjectRef callable) {
    auto task = new_instance(machine, "java/util/concurrent/FutureTask");
    if (!task) return std::unexpected(task.error());
    auto root = machine.pin_native_root(*task);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> init_arguments {
        Value::from_reference(*task), Value::from_reference(callable),
    };
    auto initialized = invoke_native(
        machine, "java/util/concurrent/FutureTask", "<init>",
        "(Ljava/util/concurrent/Callable;)V", init_arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *task;
}

[[nodiscard]] Result<ObjectRef> create_future_task(Machine& machine,
                                                   ObjectRef runnable,
                                                   ObjectRef result) {
    auto task = new_instance(machine, "java/util/concurrent/FutureTask");
    if (!task) return std::unexpected(task.error());
    auto root = machine.pin_native_root(*task);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 3> init_arguments {
        Value::from_reference(*task), Value::from_reference(runnable),
        Value::from_reference(result),
    };
    auto initialized = invoke_native(
        machine, "java/util/concurrent/FutureTask", "<init>",
        "(Ljava/lang/Runnable;Ljava/lang/Object;)V", init_arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *task;
}

[[nodiscard]] Result<ObjectRef> create_executor_service(Machine& machine,
                                                        i32 workers) {
    auto service = new_instance(machine,
                                "java/util/concurrent/NativeExecutorService");
    if (!service) return std::unexpected(service.error());
    auto root = machine.pin_native_root(*service);
    if (!root) return std::unexpected(root.error());
    const std::array<Value, 2> init_arguments {
        Value::from_reference(*service), Value::from_int(workers),
    };
    auto initialized = invoke_native(
        machine, "java/util/concurrent/NativeExecutorService", "<init>",
        "(I)V", init_arguments);
    if (!initialized) return std::unexpected(initialized.error());
    return *service;
}

void register_executor_service(NativeMethodRegistry& registry) {
    constexpr std::string_view kOwner =
        "java/util/concurrent/NativeExecutorService";
    add(registry, std::string(kOwner), "<init>", "(I)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto workers = int_argument(arguments, 1U);
            if (!service) return std::unexpected(service.error());
            if (!workers) return std::unexpected(workers.error());
            auto shutdown = set_int_field(machine, *service,
                                          kExecutorShutdownField, 0);
            auto count = set_int_field(machine, *service,
                                       kExecutorWorkersField, *workers);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (!count) return std::unexpected(count.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "execute", "(Ljava/lang/Runnable;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto runnable = reference_argument(arguments, 1U);
            if (!service) return std::unexpected(service.error());
            if (!runnable) return std::unexpected(runnable.error());
            auto shutdown = int_field(machine, *service,
                                      kExecutorShutdownField);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (*shutdown != 0) {
                return fail_java("java/util/concurrent/RejectedExecutionException",
                                 "executor is shut down");
            }
            auto thread = start_runnable_thread(machine, *runnable);
            if (!thread) return std::unexpected(thread.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "shutdown", "()V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            if (!service) return std::unexpected(service.error());
            auto stored = set_int_field(machine, *service,
                                        kExecutorShutdownField, 1);
            if (!stored) return std::unexpected(stored.error());
            return std::optional<Value> {};
        });
    add(registry, std::string(kOwner), "shutdownNow", "()Ljava/util/List;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            if (!service) return std::unexpected(service.error());
            auto stored = set_int_field(machine, *service,
                                        kExecutorShutdownField, 1);
            if (!stored) return std::unexpected(stored.error());
            auto empty = new_array_list(machine, 0);
            if (!empty) return std::unexpected(empty.error());
            return std::optional<Value>(Value::from_reference(*empty));
        });
    const auto status = [&registry, kOwner](const char* name) {
        add(registry, std::string(kOwner), name, "()Z",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto service = receiver(arguments);
                if (!service) return std::unexpected(service.error());
                auto shutdown = int_field(machine, *service,
                                          kExecutorShutdownField);
                if (!shutdown) return std::unexpected(shutdown.error());
                return std::optional<Value>(Value::from_int(
                    *shutdown != 0 ? 1 : 0));
            });
    };
    status("isShutdown");
    status("isTerminated");
    add(registry, std::string(kOwner), "awaitTermination",
        "(JLjava/util/concurrent/TimeUnit;)Z",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto duration = long_argument(arguments, 1U);
            auto unit = reference_argument(arguments, 2U);
            if (!service) return std::unexpected(service.error());
            if (!duration) return std::unexpected(duration.error());
            if (!unit) return std::unexpected(unit.error());
            auto shutdown = int_field(machine, *service,
                                      kExecutorShutdownField);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (*shutdown != 0) {
                return std::optional<Value>(Value::from_int(1));
            }
            if (*duration <= 0) {
                return std::optional<Value>(Value::from_int(0));
            }
            auto millis = time_unit_millis(machine, *unit, *duration);
            if (!millis) return std::unexpected(millis.error());
            i64 wait_millis = *millis;
            if (wait_millis <= 0) wait_millis = 1;
            const auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(wait_millis);
            while (std::chrono::steady_clock::now() - start < timeout) {
                shutdown = int_field(machine, *service,
                                     kExecutorShutdownField);
                if (!shutdown) return std::unexpected(shutdown.error());
                if (*shutdown != 0) {
                    return std::optional<Value>(Value::from_int(1));
                }
                auto waited = cooperative_wait_tick(machine);
                if (!waited) return std::unexpected(waited.error());
            }
            return std::optional<Value>(Value::from_int(0));
        });
    add(registry, std::string(kOwner), "submit",
        "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto callable = reference_argument(arguments, 1U);
            if (!service) return std::unexpected(service.error());
            if (!callable) return std::unexpected(callable.error());
            auto shutdown = int_field(machine, *service,
                                      kExecutorShutdownField);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (*shutdown != 0) {
                return fail_java("java/util/concurrent/RejectedExecutionException",
                                 "executor is shut down");
            }
            auto task = create_future_task(machine, *callable);
            if (!task) return std::unexpected(task.error());
            auto task_root = machine.pin_native_root(*task);
            if (!task_root) return std::unexpected(task_root.error());
            auto thread = start_runnable_thread(machine, *task);
            if (!thread) return std::unexpected(thread.error());
            return std::optional<Value>(Value::from_reference(*task));
        });
    add(registry, std::string(kOwner), "submit",
        "(Ljava/lang/Runnable;Ljava/lang/Object;)Ljava/util/concurrent/Future;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto runnable = reference_argument(arguments, 1U);
            auto result = reference_argument(arguments, 2U, true);
            if (!service) return std::unexpected(service.error());
            if (!runnable) return std::unexpected(runnable.error());
            if (!result) return std::unexpected(result.error());
            auto shutdown = int_field(machine, *service,
                                      kExecutorShutdownField);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (*shutdown != 0) {
                return fail_java("java/util/concurrent/RejectedExecutionException",
                                 "executor is shut down");
            }
            auto task = create_future_task(machine, *runnable, *result);
            if (!task) return std::unexpected(task.error());
            auto task_root = machine.pin_native_root(*task);
            if (!task_root) return std::unexpected(task_root.error());
            auto thread = start_runnable_thread(machine, *task);
            if (!thread) return std::unexpected(thread.error());
            return std::optional<Value>(Value::from_reference(*task));
        });
    add(registry, std::string(kOwner), "submit",
        "(Ljava/lang/Runnable;)Ljava/util/concurrent/Future;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto service = receiver(arguments);
            auto runnable = reference_argument(arguments, 1U);
            if (!service) return std::unexpected(service.error());
            if (!runnable) return std::unexpected(runnable.error());
            auto shutdown = int_field(machine, *service,
                                      kExecutorShutdownField);
            if (!shutdown) return std::unexpected(shutdown.error());
            if (*shutdown != 0) {
                return fail_java("java/util/concurrent/RejectedExecutionException",
                                 "executor is shut down");
            }
            auto task = create_future_task(machine, *runnable, {});
            if (!task) return std::unexpected(task.error());
            auto task_root = machine.pin_native_root(*task);
            if (!task_root) return std::unexpected(task_root.error());
            auto thread = start_runnable_thread(machine, *task);
            if (!thread) return std::unexpected(thread.error());
            return std::optional<Value>(Value::from_reference(*task));
        });
}

void register_executors(NativeMethodRegistry& registry) {
    constexpr std::string_view kOwner = "java/util/concurrent/Executors";
    add(registry, std::string(kOwner), "newFixedThreadPool",
        "(I)Ljava/util/concurrent/ExecutorService;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto workers = int_argument(arguments, 0U);
            if (!workers) return std::unexpected(workers.error());
            if (*workers <= 0) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "thread pool size must be positive");
            }
            auto service = create_executor_service(machine, *workers);
            if (!service) return std::unexpected(service.error());
            return std::optional<Value>(Value::from_reference(*service));
        });
    add(registry, std::string(kOwner), "newSingleThreadExecutor",
        "()Ljava/util/concurrent/ExecutorService;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto service = create_executor_service(machine, 1);
            if (!service) return std::unexpected(service.error());
            return std::optional<Value>(Value::from_reference(*service));
        });
    add(registry, std::string(kOwner), "newCachedThreadPool",
        "()Ljava/util/concurrent/ExecutorService;",
        [](Machine& machine, std::span<const Value>)
            -> Result<std::optional<Value>> {
            auto service = create_executor_service(
                machine, std::numeric_limits<i32>::max());
            if (!service) return std::unexpected(service.error());
            return std::optional<Value>(Value::from_reference(*service));
        });
    const auto callable = [&registry, kOwner](const char* descriptor,
                                              bool with_result) {
        add(registry, std::string(kOwner), "callable", descriptor,
            [with_result](Machine& machine,
                          std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto runnable = reference_argument(arguments, 0U);
                if (!runnable) return std::unexpected(runnable.error());
                ObjectRef result {};
                if (with_result) {
                    auto requested = reference_argument(arguments, 1U, true);
                    if (!requested) return std::unexpected(requested.error());
                    result = *requested;
                }
                auto wrapper = new_instance(
                    machine, "java/util/concurrent/NativeRunnableCallable");
                if (!wrapper) return std::unexpected(wrapper.error());
                auto runnable_stored = set_reference_field(
                    machine, *wrapper, kCallableRunnableField, *runnable);
                auto result_stored = set_reference_field(
                    machine, *wrapper, kCallableResultField, result);
                if (!runnable_stored) {
                    return std::unexpected(runnable_stored.error());
                }
                if (!result_stored) return std::unexpected(result_stored.error());
                return std::optional<Value>(Value::from_reference(*wrapper));
            });
    };
    callable("(Ljava/lang/Runnable;Ljava/lang/Object;)Ljava/util/concurrent/Callable;",
             true);
    callable("(Ljava/lang/Runnable;)Ljava/util/concurrent/Callable;", false);

    add(registry, "java/util/concurrent/NativeRunnableCallable", "<init>",
        "(Ljava/lang/Runnable;Ljava/lang/Object;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto wrapper = receiver(arguments);
            auto runnable = reference_argument(arguments, 1U);
            auto result = reference_argument(arguments, 2U, true);
            if (!wrapper) return std::unexpected(wrapper.error());
            if (!runnable) return std::unexpected(runnable.error());
            if (!result) return std::unexpected(result.error());
            auto runnable_stored = set_reference_field(
                machine, *wrapper, kCallableRunnableField, *runnable);
            auto result_stored = set_reference_field(
                machine, *wrapper, kCallableResultField, *result);
            if (!runnable_stored) return std::unexpected(runnable_stored.error());
            if (!result_stored) return std::unexpected(result_stored.error());
            return std::optional<Value> {};
        });
    add(registry, "java/util/concurrent/NativeRunnableCallable", "call",
        "()Ljava/lang/Object;",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto wrapper = receiver(arguments);
            if (!wrapper) return std::unexpected(wrapper.error());
            auto runnable = reference_field(machine, *wrapper,
                                            kCallableRunnableField);
            auto result = reference_field(machine, *wrapper,
                                          kCallableResultField);
            if (!runnable) return std::unexpected(runnable.error());
            if (!result) return std::unexpected(result.error());
            auto invoked = machine.invoke_instance(
                *runnable, "java/lang/Runnable", "run", "()V", {});
            if (!invoked) return std::unexpected(invoked.error());
            if (invoked->throwable.has_value()) {
                auto throwable_class = machine.heap().class_name(
                    *invoked->throwable);
                if (!throwable_class) {
                    return std::unexpected(throwable_class.error());
                }
                return fail_java(*throwable_class,
                                 "Executors.callable runnable threw");
            }
            return std::optional<Value>(Value::from_reference(*result));
        });
}

} // namespace

void register_jdk8_concurrent_natives(NativeMethodRegistry& registry) {
    register_atomic_integer(registry);
    register_atomic_long(registry);
    register_atomic_boolean(registry);
    register_atomic_reference(registry);
    register_count_down_latch(registry);
    register_future_task(registry);
    register_executor_service(registry);
    register_executors(registry);
}

} // namespace phoneme::vm
