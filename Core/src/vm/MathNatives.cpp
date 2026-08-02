#include "MathNatives.hpp"

#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

void add(NativeMethodRegistry& registry,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method("java/lang/Math",
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) {
        std::terminate();
    }
}

template <typename Number>
[[nodiscard]] Number java_min(Number left, Number right) noexcept {
    if constexpr (std::is_floating_point_v<Number>) {
        if (std::isnan(left)) return left;
        if (std::isnan(right)) return right;
        if (left == static_cast<Number>(0) &&
            right == static_cast<Number>(0)) {
            return std::signbit(left) ? left : right;
        }
    }
    return left <= right ? left : right;
}

template <typename Number>
[[nodiscard]] Number java_max(Number left, Number right) noexcept {
    if constexpr (std::is_floating_point_v<Number>) {
        if (std::isnan(left)) return left;
        if (std::isnan(right)) return right;
        if (left == static_cast<Number>(0) &&
            right == static_cast<Number>(0)) {
            return std::signbit(left) ? right : left;
        }
    }
    return left >= right ? left : right;
}

[[nodiscard]] i32 java_round(float value) noexcept {
    if (std::isnan(value)) return 0;
    if (value <= static_cast<float>(std::numeric_limits<i32>::min()))
        return std::numeric_limits<i32>::min();
    if (value >= static_cast<float>(std::numeric_limits<i32>::max()))
        return std::numeric_limits<i32>::max();
    return static_cast<i32>(std::floor(static_cast<double>(value) + 0.5));
}

[[nodiscard]] i64 java_round(double value) noexcept {
    if (std::isnan(value)) return 0;
    if (value <= static_cast<double>(std::numeric_limits<i64>::min()))
        return std::numeric_limits<i64>::min();
    if (value >= static_cast<double>(std::numeric_limits<i64>::max()))
        return std::numeric_limits<i64>::max();
    return static_cast<i64>(std::floor(value + 0.5));
}

} // namespace

void register_math_natives(NativeMethodRegistry& registry) {
    add(registry, "abs", "(I)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_int();
            if (!value) return std::unexpected(value.error());
            const i32 result = *value < 0
                ? static_cast<i32>(0U - static_cast<u32>(*value))
                : *value;
            return std::optional<Value>(Value::from_int(result));
        });
    add(registry, "abs", "(J)J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_long();
            if (!value) return std::unexpected(value.error());
            const i64 result = *value < 0
                ? static_cast<i64>(0ULL - static_cast<u64>(*value))
                : *value;
            return std::optional<Value>(Value::from_long(result));
        });
    add(registry, "abs", "(F)F",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_float();
            if (!value) return std::unexpected(value.error());
            const u32 bits = std::bit_cast<u32>(*value) & 0x7FFFFFFFU;
            return std::optional<Value>(
                Value::from_float(std::bit_cast<float>(bits)));
        });
    add(registry, "abs", "(D)D",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_double();
            if (!value) return std::unexpected(value.error());
            const u64 bits = std::bit_cast<u64>(*value) &
                             0x7FFFFFFFFFFFFFFFULL;
            return std::optional<Value>(
                Value::from_double(std::bit_cast<double>(bits)));
        });

    const auto add_int_minmax = [&registry](const char* name, bool maximum) {
        add(registry, name, "(II)I",
            [maximum](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto left = arguments[0].as_int();
                auto right = arguments[1].as_int();
                if (!left || !right)
                    return fail(ErrorCode::invalid_argument,
                                "Math integer operands are invalid");
                return std::optional<Value>(Value::from_int(
                    maximum ? java_max(*left, *right)
                            : java_min(*left, *right)));
            });
        add(registry, name, "(JJ)J",
            [maximum](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto left = arguments[0].as_long();
                auto right = arguments[1].as_long();
                if (!left || !right)
                    return fail(ErrorCode::invalid_argument,
                                "Math long operands are invalid");
                return std::optional<Value>(Value::from_long(
                    maximum ? java_max(*left, *right)
                            : java_min(*left, *right)));
            });
        add(registry, name, "(FF)F",
            [maximum](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto left = arguments[0].as_float();
                auto right = arguments[1].as_float();
                if (!left || !right)
                    return fail(ErrorCode::invalid_argument,
                                "Math float operands are invalid");
                return std::optional<Value>(Value::from_float(
                    maximum ? java_max(*left, *right)
                            : java_min(*left, *right)));
            });
        add(registry, name, "(DD)D",
            [maximum](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto left = arguments[0].as_double();
                auto right = arguments[1].as_double();
                if (!left || !right)
                    return fail(ErrorCode::invalid_argument,
                                "Math double operands are invalid");
                return std::optional<Value>(Value::from_double(
                    maximum ? java_max(*left, *right)
                            : java_min(*left, *right)));
            });
    };
    add_int_minmax("min", false);
    add_int_minmax("max", true);

    const auto add_unary = [&registry](const char* name,
                                       double (*function)(double)) {
        add(registry, name, "(D)D",
            [function](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto value = arguments[0].as_double();
                if (!value) return std::unexpected(value.error());
                return std::optional<Value>(
                    Value::from_double(function(*value)));
            });
    };
    add_unary("sin", std::sin);
    add_unary("cos", std::cos);
    add_unary("tan", std::tan);
    add_unary("asin", std::asin);
    add_unary("acos", std::acos);
    add_unary("atan", std::atan);
    add_unary("exp", std::exp);
    add_unary("log", std::log);
    add_unary("sqrt", std::sqrt);
    add_unary("ceil", std::ceil);
    add_unary("floor", std::floor);
    add_unary("rint", std::rint);

    const auto add_binary = [&registry](const char* name,
                                        double (*function)(double, double)) {
        add(registry, name, "(DD)D",
            [function](Machine&, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto left = arguments[0].as_double();
                auto right = arguments[1].as_double();
                if (!left || !right)
                    return fail(ErrorCode::invalid_argument,
                                "Math double operands are invalid");
                return std::optional<Value>(
                    Value::from_double(function(*left, *right)));
            });
    };
    add_binary("atan2", std::atan2);
    add_binary("pow", std::pow);
    add_binary("IEEEremainder", std::remainder);

    add(registry, "round", "(F)I",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_float();
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(
                Value::from_int(java_round(*value)));
        });
    add(registry, "round", "(D)J",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_double();
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(
                Value::from_long(java_round(*value)));
        });
    add(registry, "toDegrees", "(D)D",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_double();
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(
                *value * (180.0 / 3.14159265358979323846264338327950288)));
        });
    add(registry, "toRadians", "(D)D",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto value = arguments[0].as_double();
            if (!value) return std::unexpected(value.error());
            return std::optional<Value>(Value::from_double(
                *value * (3.14159265358979323846264338327950288 / 180.0)));
        });
    add(registry, "random", "()D",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (!arguments.empty())
                return fail(ErrorCode::invalid_argument,
                            "Math.random expects no arguments");
            return std::optional<Value>(
                Value::from_double(machine.next_random_double()));
        });
}

} // namespace phoneme::vm
