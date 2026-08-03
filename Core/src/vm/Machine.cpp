#include "phoneme/vm/Machine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "phoneme/base/Checked.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/ModifiedUtf8.hpp"
#include "phoneme/vm/SlotStorage.hpp"

namespace phoneme::vm
{
  namespace
  {

    constexpr u16 kAccPublic = 0x0001U;
    constexpr u16 kAccPrivate = 0x0002U;
    constexpr u16 kAccStatic = 0x0008U;
    constexpr u16 kAccSynchronized = 0x0020U;
    constexpr u16 kAccNative = 0x0100U;
    constexpr u16 kAccInterface = 0x0200U;
    constexpr u16 kAccAbstract = 0x0400U;
    constexpr usize kMaximumCallDepth = 1'024;
    constexpr u64 kSchedulerQuantum = 10'000U;

    [[nodiscard]] constexpr usize heap_capacity_with_emergency_reserve(
        usize requested) noexcept
    {
      const usize hard_limit =
          static_cast<usize>(std::numeric_limits<u32>::max()) - 1U;
      return requested < hard_limit ? requested + 1U : hard_limit;
    }

    thread_local Machine* g_execution_machine = nullptr;
    thread_local u32 g_execution_lock_depth = 0U;

    [[nodiscard]] bool value_matches(const Value &value,
                                     const TypeDescriptor &descriptor) noexcept
    {
      switch (descriptor.kind)
      {
      case JavaTypeKind::boolean:
      case JavaTypeKind::byte:
      case JavaTypeKind::character:
      case JavaTypeKind::short_integer:
      case JavaTypeKind::integer:
        return value.kind() == ValueKind::int32;
      case JavaTypeKind::float32:
        return value.kind() == ValueKind::float32;
      case JavaTypeKind::long_integer:
        return value.kind() == ValueKind::int64;
      case JavaTypeKind::float64:
        return value.kind() == ValueKind::float64;
      case JavaTypeKind::reference:
      case JavaTypeKind::array:
        return value.kind() == ValueKind::reference;
      case JavaTypeKind::void_type:
        return false;
      }
      return false;
    }

    class ExecutionFrame final
    {
    public:
      [[nodiscard]] static Result<ExecutionFrame> make(
          ResolvedMethod resolved,
          MethodDescriptor descriptor,
          std::span<const Value> arguments,
          bool has_receiver)
      {
        if (resolved.method == nullptr || !resolved.method->code.has_value())
        {
          return fail(ErrorCode::unsupported_feature,
                      "native or abstract method execution is not ported yet");
        }

        ExecutionFrame frame(std::move(resolved),
                             std::move(descriptor),
                             has_receiver);
        usize local_index = 0;
        for (const Value value : arguments)
        {
          auto stored = frame.locals_.set(local_index, value);
          if (!stored)
          {
            return std::unexpected(stored.error());
          }
          local_index += value.category_two() ? 2 : 1;
        }
        return frame;
      }

      [[nodiscard]] const classfile::ClassFile &owner() const noexcept
      {
        return *resolved_.owner;
      }

      [[nodiscard]] const classfile::Method &method() const noexcept
      {
        return *resolved_.method;
      }

      [[nodiscard]] const MethodDescriptor &descriptor() const noexcept
      {
        return descriptor_;
      }

      [[nodiscard]] bool has_receiver() const noexcept { return has_receiver_; }
      [[nodiscard]] usize pc() const noexcept { return pc_; }
      [[nodiscard]] usize code_size() const noexcept { return code_.size(); }
      [[nodiscard]] usize current_instruction_pc() const noexcept
      {
        return current_instruction_pc_;
      }
      [[nodiscard]] const std::vector<classfile::ExceptionHandler> &
      exception_table() const noexcept
      {
        return method().code->exception_table;
      }

      void begin_instruction(usize opcode_pc) noexcept
      {
        current_instruction_pc_ = opcode_pc;
      }

      [[nodiscard]] Status enter_exception_handler(usize handler_pc,
                                                   ObjectRef throwable)
      {
        if (handler_pc >= code_.size())
        {
          return fail(ErrorCode::malformed_class,
                      "exception handler target is outside method bytecode");
        }
        stack_.clear();
        auto pushed = stack_.push(Value::from_reference(throwable));
        if (!pushed)
        {
          return std::unexpected(pushed.error());
        }
        pc_ = handler_pc;
        current_instruction_pc_ = handler_pc;
        return {};
      }

      [[nodiscard]] Result<u8> read_u8()
      {
        if (pc_ >= code_.size())
        {
          return fail(ErrorCode::malformed_class,
                      "bytecode read exceeds method body");
        }
        return code_[pc_++];
      }

      [[nodiscard]] Result<i8> read_i8()
      {
        auto value = read_u8();
        if (!value)
        {
          return std::unexpected(value.error());
        }
        return static_cast<i8>(*value);
      }

      [[nodiscard]] Result<u16> read_u16()
      {
        auto high = read_u8();
        auto low = read_u8();
        if (!high || !low)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated 16-bit bytecode operand");
        }
        return static_cast<u16>((static_cast<u16>(*high) << 8U) |
                                static_cast<u16>(*low));
      }

      [[nodiscard]] Result<i16> read_i16()
      {
        auto value = read_u16();
        if (!value)
        {
          return std::unexpected(value.error());
        }
        return static_cast<i16>(*value);
      }

      [[nodiscard]] Status read_switch_padding()
      {
        while ((pc_ & 3U) != 0U)
        {
          auto padding = read_u8();
          if (!padding)
          {
            return std::unexpected(padding.error());
          }
          if (*padding != 0)
          {
            return fail(ErrorCode::malformed_class,
                        "switch padding byte is not zero");
          }
        }
        return {};
      }

      [[nodiscard]] Result<i32> read_i32()
      {
        auto first = read_u8();
        auto second = read_u8();
        auto third = read_u8();
        auto fourth = read_u8();
        if (!first || !second || !third || !fourth)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated 32-bit bytecode operand");
        }
        const u32 bits = (static_cast<u32>(*first) << 24U) |
                         (static_cast<u32>(*second) << 16U) |
                         (static_cast<u32>(*third) << 8U) |
                         static_cast<u32>(*fourth);
        return static_cast<i32>(bits);
      }

      [[nodiscard]] Status jump_absolute(usize target)
      {
        if (target >= code_.size())
        {
          return fail(ErrorCode::malformed_class,
                      "bytecode absolute target is out of range");
        }
        pc_ = target;
        return {};
      }

      [[nodiscard]] Status branch(usize opcode_pc, i64 relative_offset)
      {
        const i64 origin = static_cast<i64>(opcode_pc);
        if ((relative_offset > 0 &&
             origin > std::numeric_limits<i64>::max() - relative_offset) ||
            (relative_offset < 0 &&
             origin < std::numeric_limits<i64>::min() - relative_offset))
        {
          return fail(ErrorCode::malformed_class,
                      "bytecode branch calculation overflowed");
        }
        const i64 target = origin + relative_offset;
        if (target < 0 ||
            static_cast<u64>(target) >= static_cast<u64>(code_.size()))
        {
          return fail(ErrorCode::malformed_class,
                      "bytecode branch target is out of range");
        }
        pc_ = static_cast<usize>(target);
        return {};
      }

      [[nodiscard]] Status push(Value value) { return stack_.push(value); }
      [[nodiscard]] Result<Value> pop() { return stack_.pop(); }
      [[nodiscard]] Result<Value> local(usize index) const
      {
        return locals_.get(index);
      }
      [[nodiscard]] Status set_local(usize index, Value value)
      {
        return locals_.set(index, value);
      }
      void set_synchronized_monitor(ObjectRef monitor) noexcept
      {
        synchronized_monitor_ = monitor;
      }
      [[nodiscard]] std::optional<ObjectRef> synchronized_monitor() const noexcept
      {
        return synchronized_monitor_;
      }
      void set_return_override(Value value) noexcept
      {
        return_override_ = value;
      }
      [[nodiscard]] std::optional<Value> return_override() const noexcept
      {
        return return_override_;
      }
      void append_reference_roots(std::vector<ObjectRef> &roots) const
      {
        locals_.append_reference_roots(roots);
        stack_.append_reference_roots(roots);
        if (synchronized_monitor_.has_value() &&
            !synchronized_monitor_->is_null())
        {
          roots.push_back(*synchronized_monitor_);
        }
        if (return_override_.has_value() &&
            return_override_->kind() == ValueKind::reference)
        {
          auto reference = return_override_->as_reference();
          if (reference && !reference->is_null())
            roots.push_back(*reference);
        }
      }

    private:
      ExecutionFrame(ResolvedMethod resolved,
                     MethodDescriptor descriptor,
                     bool has_receiver)
          : resolved_(std::move(resolved)),
            descriptor_(std::move(descriptor)),
            has_receiver_(has_receiver),
            code_(resolved_.method->code->bytecode),
            locals_(resolved_.method->code->max_locals),
            stack_(resolved_.method->code->max_stack) {}

      ResolvedMethod resolved_;
      MethodDescriptor descriptor_;
      bool has_receiver_{false};
      std::span<const u8> code_;
      LocalVariables locals_;
      OperandStack stack_;
      usize pc_{0};
      usize current_instruction_pc_{0};
      std::optional<ObjectRef> synchronized_monitor_;
      std::optional<Value> return_override_;
    };

    [[nodiscard]] Status push_values(ExecutionFrame &frame,
                                     std::initializer_list<Value> values)
    {
      for (const Value value : values)
      {
        auto pushed = frame.push(value);
        if (!pushed)
        {
          return std::unexpected(pushed.error());
        }
      }
      return {};
    }

    [[nodiscard]] Result<i32> pop_int(ExecutionFrame &frame)
    {
      auto value = frame.pop();
      if (!value)
      {
        return std::unexpected(value.error());
      }
      return value->as_int();
    }

    [[nodiscard]] Result<i64> pop_long(ExecutionFrame &frame)
    {
      auto value = frame.pop();
      if (!value)
      {
        return std::unexpected(value.error());
      }
      return value->as_long();
    }

    [[nodiscard]] Result<float> pop_float(ExecutionFrame &frame)
    {
      auto value = frame.pop();
      if (!value)
      {
        return std::unexpected(value.error());
      }
      return value->as_float();
    }

    [[nodiscard]] Result<double> pop_double(ExecutionFrame &frame)
    {
      auto value = frame.pop();
      if (!value)
      {
        return std::unexpected(value.error());
      }
      return value->as_double();
    }

    [[nodiscard]] Result<ObjectRef> pop_reference(ExecutionFrame &frame)
    {
      auto value = frame.pop();
      if (!value)
      {
        return std::unexpected(value.error());
      }
      return value->as_reference();
    }

    [[nodiscard]] Result<std::vector<Value>> pop_arguments(
        ExecutionFrame &caller,
        const MethodDescriptor &descriptor,
        bool include_receiver)
    {
      const usize value_count = descriptor.parameters.size() +
                                (include_receiver ? 1U : 0U);
      std::vector<Value> arguments(value_count);

      for (usize reverse = descriptor.parameters.size(); reverse > 0; --reverse)
      {
        const usize parameter_index = reverse - 1;
        auto value = caller.pop();
        if (!value)
        {
          return std::unexpected(value.error());
        }
        if (!value_matches(*value, descriptor.parameters[parameter_index]))
        {
          return fail(ErrorCode::malformed_class,
                      "method argument does not match its descriptor");
        }
        arguments[parameter_index + (include_receiver ? 1U : 0U)] = *value;
      }

      if (include_receiver)
      {
        auto receiver = caller.pop();
        if (!receiver)
        {
          return std::unexpected(receiver.error());
        }
        auto reference = receiver->as_reference();
        if (!reference)
        {
          return std::unexpected(reference.error());
        }
        arguments[0] = *receiver;
      }
      return arguments;
    }

    [[nodiscard]] Result<Value> constant_value(const classfile::ClassFile &owner,
                                               u16 index,
                                               bool category_two_only)
    {
      auto constant = owner.constant(index);
      if (!constant)
      {
        return std::unexpected(constant.error());
      }

      switch ((*constant)->kind)
      {
      case classfile::ConstantKind::integer:
        if (category_two_only)
        {
          break;
        }
        return Value::from_int(
            static_cast<i32>(static_cast<u32>((*constant)->bits)));
      case classfile::ConstantKind::float32:
        if (category_two_only)
        {
          break;
        }
        return Value::from_float(std::bit_cast<float>(
            static_cast<u32>((*constant)->bits)));
      case classfile::ConstantKind::long64:
        if (!category_two_only)
        {
          break;
        }
        return Value::from_long(static_cast<i64>((*constant)->bits));
      case classfile::ConstantKind::float64:
        if (!category_two_only)
        {
          break;
        }
        return Value::from_double(std::bit_cast<double>((*constant)->bits));
      default:
        break;
      }

      return fail(ErrorCode::unsupported_feature,
                  "ldc constant kind is not ported yet");
    }

    [[nodiscard]] Result<std::optional<i32>> integer_binary(ExecutionFrame &frame,
                                                            u8 opcode)
    {
      auto right = pop_int(frame);
      auto left = pop_int(frame);
      if (!right || !left)
      {
        return fail(ErrorCode::malformed_class,
                    "integer operation requires two int operands");
      }

      switch (opcode)
      {
      case 0x60:
        return static_cast<i32>(static_cast<u32>(*left) +
                                static_cast<u32>(*right));
      case 0x64:
        return static_cast<i32>(static_cast<u32>(*left) -
                                static_cast<u32>(*right));
      case 0x68:
        return static_cast<i32>(static_cast<u32>(*left) *
                                static_cast<u32>(*right));
      case 0x6C:
        if (*right == 0)
        {
          return std::optional<i32>{};
        }
        if (*left == std::numeric_limits<i32>::min() && *right == -1)
        {
          return std::numeric_limits<i32>::min();
        }
        return *left / *right;
      case 0x70:
        if (*right == 0)
        {
          return std::optional<i32>{};
        }
        if (*left == std::numeric_limits<i32>::min() && *right == -1)
        {
          return 0;
        }
        return *left % *right;
      case 0x7E:
        return *left & *right;
      case 0x80:
        return *left | *right;
      case 0x82:
        return *left ^ *right;
      default:
        return fail(ErrorCode::internal_error,
                    "unknown integer binary opcode");
      }
    }

    [[nodiscard]] Result<std::optional<i64>> long_binary(ExecutionFrame &frame,
                                                         u8 opcode)
    {
      auto right = pop_long(frame);
      auto left = pop_long(frame);
      if (!right || !left)
      {
        return fail(ErrorCode::malformed_class,
                    "long operation requires two long operands");
      }

      switch (opcode)
      {
      case 0x61:
        return static_cast<i64>(static_cast<u64>(*left) +
                                static_cast<u64>(*right));
      case 0x65:
        return static_cast<i64>(static_cast<u64>(*left) -
                                static_cast<u64>(*right));
      case 0x69:
        return static_cast<i64>(static_cast<u64>(*left) *
                                static_cast<u64>(*right));
      case 0x6D:
        if (*right == 0)
        {
          return std::optional<i64>{};
        }
        if (*left == std::numeric_limits<i64>::min() && *right == -1)
        {
          return std::numeric_limits<i64>::min();
        }
        return *left / *right;
      case 0x71:
        if (*right == 0)
        {
          return std::optional<i64>{};
        }
        if (*left == std::numeric_limits<i64>::min() && *right == -1)
        {
          return 0;
        }
        return *left % *right;
      case 0x7F:
        return *left & *right;
      case 0x81:
        return *left | *right;
      case 0x83:
        return *left ^ *right;
      default:
        return fail(ErrorCode::internal_error,
                    "unknown long binary opcode");
      }
    }

    [[nodiscard]] Result<float> float_binary(ExecutionFrame &frame, u8 opcode)
    {
      auto right = pop_float(frame);
      auto left = pop_float(frame);
      if (!right || !left)
      {
        return fail(ErrorCode::malformed_class,
                    "float operation requires two float operands");
      }
      switch (opcode)
      {
      case 0x62:
        return *left + *right;
      case 0x66:
        return *left - *right;
      case 0x6A:
        return *left * *right;
      case 0x6E:
        return *left / *right;
      case 0x72:
        return std::fmod(*left, *right);
      default:
        return fail(ErrorCode::internal_error,
                    "unknown float binary opcode");
      }
    }

    [[nodiscard]] Result<double> double_binary(ExecutionFrame &frame, u8 opcode)
    {
      auto right = pop_double(frame);
      auto left = pop_double(frame);
      if (!right || !left)
      {
        return fail(ErrorCode::malformed_class,
                    "double operation requires two double operands");
      }
      switch (opcode)
      {
      case 0x63:
        return *left + *right;
      case 0x67:
        return *left - *right;
      case 0x6B:
        return *left * *right;
      case 0x6F:
        return *left / *right;
      case 0x73:
        return std::fmod(*left, *right);
      default:
        return fail(ErrorCode::internal_error,
                    "unknown double binary opcode");
      }
    }

    template <typename Integer, typename Floating>
    [[nodiscard]] Integer java_fp_to_integral(Floating value) noexcept
    {
      if (std::isnan(value))
      {
        return 0;
      }
      const long double extended = static_cast<long double>(value);
      const long double minimum =
          static_cast<long double>(std::numeric_limits<Integer>::min());
      const long double maximum =
          static_cast<long double>(std::numeric_limits<Integer>::max());
      if (extended <= minimum)
      {
        return std::numeric_limits<Integer>::min();
      }
      if (extended >= maximum)
      {
        return std::numeric_limits<Integer>::max();
      }
      return static_cast<Integer>(std::trunc(value));
    }

    [[nodiscard]] bool test_zero(u8 opcode, i32 value) noexcept
    {
      switch (opcode)
      {
      case 0x99:
        return value == 0;
      case 0x9A:
        return value != 0;
      case 0x9B:
        return value < 0;
      case 0x9C:
        return value >= 0;
      case 0x9D:
        return value > 0;
      case 0x9E:
        return value <= 0;
      default:
        return false;
      }
    }

    [[nodiscard]] bool test_int_compare(u8 opcode,
                                        i32 left,
                                        i32 right) noexcept
    {
      switch (opcode)
      {
      case 0x9F:
        return left == right;
      case 0xA0:
        return left != right;
      case 0xA1:
        return left < right;
      case 0xA2:
        return left >= right;
      case 0xA3:
        return left > right;
      case 0xA4:
        return left <= right;
      default:
        return false;
      }
    }

    [[nodiscard]] usize local_index_for_load(u8 opcode) noexcept
    {
      if (opcode >= 0x1A && opcode <= 0x1D)
        return opcode - 0x1A;
      if (opcode >= 0x1E && opcode <= 0x21)
        return opcode - 0x1E;
      if (opcode >= 0x22 && opcode <= 0x25)
        return opcode - 0x22;
      if (opcode >= 0x26 && opcode <= 0x29)
        return opcode - 0x26;
      return opcode - 0x2A;
    }

    [[nodiscard]] usize local_index_for_store(u8 opcode) noexcept
    {
      if (opcode >= 0x3B && opcode <= 0x3E)
        return opcode - 0x3B;
      if (opcode >= 0x3F && opcode <= 0x42)
        return opcode - 0x3F;
      if (opcode >= 0x43 && opcode <= 0x46)
        return opcode - 0x43;
      if (opcode >= 0x47 && opcode <= 0x4A)
        return opcode - 0x47;
      return opcode - 0x4B;
    }

    [[nodiscard]] bool load_kind_matches(u8 opcode, ValueKind kind) noexcept
    {
      if (opcode == 0x15 || (opcode >= 0x1A && opcode <= 0x1D))
      {
        return kind == ValueKind::int32;
      }
      if (opcode == 0x16 || (opcode >= 0x1E && opcode <= 0x21))
      {
        return kind == ValueKind::int64;
      }
      if (opcode == 0x17 || (opcode >= 0x22 && opcode <= 0x25))
      {
        return kind == ValueKind::float32;
      }
      if (opcode == 0x18 || (opcode >= 0x26 && opcode <= 0x29))
      {
        return kind == ValueKind::float64;
      }
      return kind == ValueKind::reference;
    }

    [[nodiscard]] bool store_kind_matches(u8 opcode, ValueKind kind) noexcept
    {
      if (opcode == 0x36 || (opcode >= 0x3B && opcode <= 0x3E))
      {
        return kind == ValueKind::int32;
      }
      if (opcode == 0x37 || (opcode >= 0x3F && opcode <= 0x42))
      {
        return kind == ValueKind::int64;
      }
      if (opcode == 0x38 || (opcode >= 0x43 && opcode <= 0x46))
      {
        return kind == ValueKind::float32;
      }
      if (opcode == 0x39 || (opcode >= 0x47 && opcode <= 0x4A))
      {
        return kind == ValueKind::float64;
      }
      return kind == ValueKind::reference;
    }

    [[nodiscard]] bool reference_array_descriptor(
        std::string_view descriptor) noexcept
    {
      return descriptor.starts_with("[L") || descriptor.starts_with("[[");
    }

    [[nodiscard]] bool array_load_class_matches(
        u8 opcode,
        std::string_view descriptor) noexcept
    {
      switch (opcode)
      {
      case 0x2E: return descriptor == "[I";
      case 0x2F: return descriptor == "[J";
      case 0x30: return descriptor == "[F";
      case 0x31: return descriptor == "[D";
      case 0x32: return reference_array_descriptor(descriptor);
      case 0x33: return descriptor == "[B" || descriptor == "[Z";
      case 0x34: return descriptor == "[C";
      case 0x35: return descriptor == "[S";
      default: return false;
      }
    }

    [[nodiscard]] bool array_store_class_matches(
        u8 opcode,
        std::string_view descriptor) noexcept
    {
      switch (opcode)
      {
      case 0x4F: return descriptor == "[I";
      case 0x50: return descriptor == "[J";
      case 0x51: return descriptor == "[F";
      case 0x52: return descriptor == "[D";
      case 0x53: return reference_array_descriptor(descriptor);
      case 0x54: return descriptor == "[B" || descriptor == "[Z";
      case 0x55: return descriptor == "[C";
      case 0x56: return descriptor == "[S";
      default: return false;
      }
    }

    [[nodiscard]] Result<Value> array_default_value(
        std::string_view descriptor)
    {
      if (descriptor.size() < 2U || descriptor.front() != '[')
      {
        return fail(ErrorCode::malformed_class,
                    "array descriptor has no component type");
      }
      switch (descriptor[1])
      {
      case 'Z':
      case 'B':
      case 'C':
      case 'S':
      case 'I':
        return Value::from_int(0);
      case 'J':
        return Value::from_long(0);
      case 'F':
        return Value::from_float(0.0F);
      case 'D':
        return Value::from_double(0.0);
      case 'L':
      case '[':
        return Value::from_reference({});
      default:
        return fail(ErrorCode::malformed_class,
                    "array descriptor has an invalid component type");
      }
    }

    [[nodiscard]] Result<std::pair<std::string, Value>> primitive_array_type(
        u8 atype)
    {
      switch (atype)
      {
      case 4:
        return std::pair<std::string, Value>("[Z", Value::from_int(0));
      case 5:
        return std::pair<std::string, Value>("[C", Value::from_int(0));
      case 6:
        return std::pair<std::string, Value>("[F", Value::from_float(0.0F));
      case 7:
        return std::pair<std::string, Value>("[D", Value::from_double(0.0));
      case 8:
        return std::pair<std::string, Value>("[B", Value::from_int(0));
      case 9:
        return std::pair<std::string, Value>("[S", Value::from_int(0));
      case 10:
        return std::pair<std::string, Value>("[I", Value::from_int(0));
      case 11:
        return std::pair<std::string, Value>("[J", Value::from_long(0));
      default:
        return fail(ErrorCode::malformed_class,
                    "newarray uses an invalid primitive type code");
      }
    }

  } // namespace

  Machine::Machine(ClassRepository &classes, usize maximum_heap_objects)
      : classes_(classes), states_(classes),
        heap_(heap_capacity_with_emergency_reserve(maximum_heap_objects)),
        permission_policy_(std::make_shared<security::PermissionPolicy>()),
        timers_(*this), media_events_(*this)
  {
    auto security_configured = permission_policy_->configure(
        security::PermissionPolicyConfig {
            .suite_id = SuiteId {1},
            .trust = security::SuiteTrust::trusted,
            .trusted_default_allow = true,
        });
    if (!security_configured)
      std::abort();

    connections_.set_blocking_hooks(network::NetworkBlockingHooks {
        .before_block = [this] {
          scheduler_.set_current_state(JavaThreadState::blocked_io);
          return suspend_execution_for_blocking();
        },
        .after_block = [this](u32 depth) {
          resume_execution_after_blocking(depth);
          scheduler_.set_current_state(JavaThreadState::running);
        },
        .poll_cancellation = [this]() -> std::optional<Error> {
          if (!scheduler_.consume_current_interrupt())
            return std::nullopt;
          return Error::make_java(
              "java/io/InterruptedIOException",
              "network operation was interrupted");
        },
    });

    register_core_natives(natives_);

    // CLDC bootstrap classes are initialized by the VM before application
    // class initialization begins. Treating them as ordinary game classes
    // would incorrectly re-run ROM/bootstrap initialization bytecode.
    initialized_classes_.insert("java/lang/Object");
    initialized_classes_.insert("java/lang/Class");
    initialized_classes_.insert("java/lang/String");
    initialized_classes_.insert("java/lang/System");
    initialized_classes_.insert("java/lang/Thread");
    initialized_classes_.insert("java/lang/Runtime");
    initialized_classes_.insert("java/lang/Float");
    initialized_classes_.insert("java/lang/Double");
    initialized_classes_.insert("java/lang/Math");

    auto emergency = states_.allocate_instance(
        heap_, "java/lang/OutOfMemoryError");
    if (!emergency)
      std::abort();
    emergency_out_of_memory_error_ = *emergency;
  }

  Machine::~Machine()
  {
    shutdown();
  }

  void Machine::shutdown() noexcept
  {
    if (shutdown_started_.exchange(true, std::memory_order_acq_rel))
      return;
    media_events_.shutdown();
    timers_.shutdown();
    scheduler_.shutdown(&monitors_);
  }

  Result<ObjectRef> Machine::current_java_thread()
  {
    auto current = scheduler_.current_thread_object();
    if (current)
      return *current;
    auto allocated = states_.allocate_instance(heap_, "java/lang/Thread");
    if (!allocated)
      return std::unexpected(allocated.error());
    auto bound = scheduler_.bind_current_thread_object(*allocated);
    if (!bound)
      return std::unexpected(bound.error());
    return *allocated;
  }

  Status Machine::initialize_java_thread(ObjectRef thread, ObjectRef target)
  {
    return scheduler_.register_thread(thread, target);
  }

  Status Machine::start_java_thread(ObjectRef thread)
  {
    return scheduler_.start_thread(*this, thread);
  }

  Result<std::optional<Value>> Machine::run_java_thread_target(ObjectRef thread)
  {
    auto target = scheduler_.runnable_target(thread);
    if (!target)
      return std::unexpected(target.error());
    if (target->is_null())
      return std::optional<Value>{};
    auto result = invoke_instance(*target,
                                  "java/lang/Runnable",
                                  "run",
                                  "()V");
    if (!result)
      return std::unexpected(result.error());
    if (result->throwable.has_value())
    {
      auto class_name = heap_.class_name(*result->throwable);
      if (!class_name)
        return std::unexpected(class_name.error());
      return fail_java(*class_name,
                       "uncaught throwable from Runnable.run");
    }
    return std::optional<Value>{};
  }

  Result<SchedulerWaitResult> Machine::sleep_current_thread(i64 millis)
  {
    if (millis < 0)
      return fail_java("java/lang/IllegalArgumentException",
                       "Thread.sleep timeout must not be negative");
    return scheduler_.sleep_current(*this,
                                    std::chrono::milliseconds(millis));
  }

  Result<SchedulerWaitResult> Machine::join_java_thread(
      ObjectRef thread,
      std::optional<i64> millis)
  {
    if (millis.has_value() && *millis < 0)
      return fail_java("java/lang/IllegalArgumentException",
                       "Thread.join timeout must not be negative");
    std::optional<std::chrono::milliseconds> timeout;
    if (millis.has_value() && *millis > 0)
      timeout = std::chrono::milliseconds(*millis);
    return scheduler_.join_current(*this, thread, timeout);
  }

  Status Machine::interrupt_java_thread(ObjectRef thread)
  {
    auto interrupted = scheduler_.interrupt(thread);
    if (interrupted)
      monitors_.wake_all();
    return interrupted;
  }

  Result<MonitorWaitResult> Machine::wait_on_object(
      ObjectRef object,
      std::optional<i64> millis)
  {
    if (millis.has_value() && *millis < 0)
      return fail_java("java/lang/IllegalArgumentException",
                       "Object.wait timeout must not be negative");
    std::optional<std::chrono::milliseconds> timeout;
    if (millis.has_value() && *millis > 0)
      timeout = std::chrono::milliseconds(*millis);

    u32 released_depth = 0U;
    auto result = monitors_.wait(
        object,
        scheduler_.current_thread_id(),
        timeout,
        [this, &released_depth] {
          scheduler_.set_current_state(JavaThreadState::waiting);
          released_depth = suspend_execution_for_blocking();
        },
        [this, &released_depth] {
          resume_execution_after_blocking(released_depth);
          scheduler_.set_current_state(JavaThreadState::running);
        },
        [this] { return scheduler_.current_is_interrupted(); });
    if (result && *result == MonitorWaitResult::interrupted)
      (void)scheduler_.consume_current_interrupt();
    return result;
  }

  Status Machine::notify_object(ObjectRef object, bool all)
  {
    return all ? monitors_.notify_all(object,
                                      scheduler_.current_thread_id())
               : monitors_.notify_one(object,
                                      scheduler_.current_thread_id());
  }

  void Machine::cooperative_yield()
  {
    scheduler_.cooperative_yield(*this);
  }

  bool Machine::executing_on_current_thread() const noexcept
  {
    return g_execution_machine == this && g_execution_lock_depth != 0U;
  }

  void Machine::request_garbage_collection() noexcept
  {
    gc_requested_.store(true, std::memory_order_release);
  }

  u32 Machine::suspend_execution_for_blocking() noexcept
  {
    if (g_execution_machine != this || g_execution_lock_depth == 0U)
      return 0U;
    const u32 depth = g_execution_lock_depth;
    for (u32 index = 0U; index < depth; ++index)
      execution_mutex_.unlock();
    return depth;
  }

  void Machine::resume_execution_after_blocking(u32 depth) noexcept
  {
    for (u32 index = 0U; index < depth; ++index)
      execution_mutex_.lock();
  }

  void Machine::publish_execution_roots(
      u32 invocation_depth,
      const std::vector<ObjectRef>& roots)
  {
    scheduler_.publish_current_roots(invocation_depth, roots);
  }

  void Machine::clear_execution_roots(u32 invocation_depth) noexcept
  {
    scheduler_.clear_current_roots(invocation_depth);
  }

  Status Machine::enter_monitor(ObjectRef monitor)
  {
    auto entered = monitors_.enter(monitor,
                                   scheduler_.current_thread_id());
    if (!entered)
      return std::unexpected(entered.error());
    if (*entered == MonitorEnterResult::acquired)
      return {};

    u32 released_depth = 0U;
    return monitors_.enter_blocking(
        monitor,
        scheduler_.current_thread_id(),
        [this, &released_depth] {
          scheduler_.set_current_state(JavaThreadState::blocked_monitor);
          released_depth = suspend_execution_for_blocking();
        },
        [this, &released_depth] {
          resume_execution_after_blocking(released_depth);
          scheduler_.set_current_state(JavaThreadState::running);
        });
  }

  Result<NativeRootScope> Machine::allocate_pinned_instance(
      std::string_view class_name)
  {
    std::scoped_lock execution_lock(execution_mutex_);
    auto object = states_.allocate_instance(heap_, class_name);
    if (!object && object.error().code == ErrorCode::overflow)
    {
      auto collected = collect_garbage();
      if (!collected)
        return std::unexpected(collected.error());
      object = states_.allocate_instance(heap_, class_name);
    }
    if (!object)
      return std::unexpected(object.error());
    return pin_native_root(*object);
  }

  Status Machine::collect_garbage()
  {
    std::scoped_lock execution_lock(execution_mutex_);
    std::vector<ObjectRef> roots;
    roots.reserve(interned_strings_.size() + class_mirrors_.size() +
                  ui_components_.size() + 16U);
    states_.append_reference_roots(roots);
    if (!emergency_out_of_memory_error_.is_null())
      roots.push_back(emergency_out_of_memory_error_);
    for (const auto &[value, reference] : interned_strings_)
    {
      (void)value;
      if (!reference.is_null())
        roots.push_back(reference);
    }
    for (const auto &[class_name, reference] : class_mirrors_)
    {
      (void)class_name;
      if (!reference.is_null())
        roots.push_back(reference);
    }
    for (const auto &[component_id, reference] : ui_components_)
    {
      (void)component_id;
      if (!reference.is_null())
        roots.push_back(reference);
    }
    if (canvas_bridge_ != nullptr)
      canvas_bridge_->append_reference_roots(roots);
    monitors_.append_reference_roots(roots);
    timers_.append_reference_roots(roots);
    scheduler_.append_reference_roots(roots);
    native_roots_.append_reference_roots(roots);
    auto collected = heap_.collect(roots);
    if (collected)
    {
      prune_lambda_bindings();
      graphics_.prune([this](u64 object_key) {
        return heap_.class_name(ObjectRef{object_key}).has_value();
      });
    }
    return collected;
  }

  Status Machine::initialize_system_streams()
  {
    if (system_streams_initialized_)
      return {};
    auto empty_buffer = heap_.allocate_array("[B", 0, Value::from_int(0));
    if (!empty_buffer)
      return std::unexpected(empty_buffer.error());
    auto input = states_.allocate_instance(heap_,
                                           "java/io/ByteArrayInputStream");
    if (!input)
      return std::unexpected(input.error());
    auto input_buffer = heap_.set_field(*input, 0,
                                        Value::from_reference(*empty_buffer));
    auto input_position = heap_.set_field(*input, 1, Value::from_int(0));
    auto input_mark = heap_.set_field(*input, 2, Value::from_int(0));
    auto input_count = heap_.set_field(*input, 3, Value::from_int(0));
    if (!input_buffer)
      return input_buffer;
    if (!input_position)
      return input_position;
    if (!input_mark)
      return input_mark;
    if (!input_count)
      return input_count;

    const auto make_console_stream = [this]() -> Result<ObjectRef> {
      auto stream = states_.allocate_instance(heap_, "java/io/PrintStream");
      if (!stream)
        return std::unexpected(stream.error());
      auto output = heap_.set_field(*stream, 0, Value::from_reference({}));
      auto error = heap_.set_field(*stream, 1, Value::from_int(0));
      auto auto_flush = heap_.set_field(*stream, 2, Value::from_int(1));
      auto console = heap_.set_field(*stream, 3, Value::from_int(1));
      if (!output)
        return std::unexpected(output.error());
      if (!error)
        return std::unexpected(error.error());
      if (!auto_flush)
        return std::unexpected(auto_flush.error());
      if (!console)
        return std::unexpected(console.error());
      return *stream;
    };
    auto output = make_console_stream();
    auto error = make_console_stream();
    if (!output)
      return std::unexpected(output.error());
    if (!error)
      return std::unexpected(error.error());

    auto input_field = states_.resolve_field("java/lang/System", "in",
                                             "Ljava/io/InputStream;", true);
    auto output_field = states_.resolve_field("java/lang/System", "out",
                                              "Ljava/io/PrintStream;", true);
    auto error_field = states_.resolve_field("java/lang/System", "err",
                                             "Ljava/io/PrintStream;", true);
    if (!input_field)
      return std::unexpected(input_field.error());
    if (!output_field)
      return std::unexpected(output_field.error());
    if (!error_field)
      return std::unexpected(error_field.error());
    auto input_stored = states_.set_static_field(
        *input_field, Value::from_reference(*input));
    auto output_stored = states_.set_static_field(
        *output_field, Value::from_reference(*output));
    auto error_stored = states_.set_static_field(
        *error_field, Value::from_reference(*error));
    if (!input_stored)
      return input_stored;
    if (!output_stored)
      return output_stored;
    if (!error_stored)
      return error_stored;
    system_streams_initialized_ = true;
    return {};
  }

  void Machine::append_console(std::u16string_view text)
  {
    constexpr usize maximum_units = 65'536U;
    if (text.size() >= maximum_units)
    {
      console_output_.assign(text.end() -
                                 static_cast<std::ptrdiff_t>(maximum_units),
                             text.end());
      return;
    }
    if (console_output_.size() > maximum_units - text.size())
    {
      console_output_.erase(
          0, console_output_.size() - (maximum_units - text.size()));
    }
    console_output_.append(text);
  }

  void Machine::configure_ui_bridge(i32 app_namespace, UiEventSink sink)
  {
    ui_event_sink_ = std::move(sink);
    const i64 normalized_namespace =
        app_namespace > 0 ? static_cast<i64>(app_namespace) : 0LL;
    constexpr i64 kNamespaceWidth = 100'000LL;
    constexpr i64 kMaximumBase =
        static_cast<i64>(std::numeric_limits<i32>::max()) -
        kNamespaceWidth;
    const i64 base = normalized_namespace <= kMaximumBase / kNamespaceWidth
        ? normalized_namespace * kNamespaceWidth
        : 0LL;
    next_ui_component_id_ = static_cast<i32>(base + 1LL);
    ui_generation_ = 0;
  }

  i32 Machine::allocate_ui_component_id() noexcept
  {
    if (next_ui_component_id_ == std::numeric_limits<i32>::max())
      return 0;
    return next_ui_component_id_++;
  }

  void Machine::emit_ui_event(UiBridgeEvent event)
  {
    if (!ui_event_sink_)
      return;
    event.generation = ++ui_generation_;
    ui_event_sink_(std::move(event));
  }

  Status Machine::enqueue_serial_callback(ObjectRef runnable)
  {
    if (runnable.is_null())
    {
      return fail(ErrorCode::invalid_argument,
                  "LCDUI serial callback is null");
    }
    auto root = pin_native_root(runnable);
    if (!root)
      return std::unexpected(root.error());

    constexpr usize kMaximumQueuedCallbacks = 4'096U;
    std::scoped_lock lock(serial_callbacks_mutex_);
    if (serial_callbacks_.size() >= kMaximumQueuedCallbacks)
    {
      return fail(ErrorCode::overflow,
                  "LCDUI serial callback queue is full");
    }
    serial_callbacks_.push_back(std::move(*root));
    return {};
  }

  Status Machine::pump_serial_callbacks(usize maximum_callbacks)
  {
    for (usize delivered = 0U; delivered < maximum_callbacks; ++delivered)
    {
      NativeRootScope callback;
      {
        std::scoped_lock lock(serial_callbacks_mutex_);
        if (serial_callbacks_.empty())
          return {};
        callback = std::move(serial_callbacks_.front());
        serial_callbacks_.pop_front();
      }

      auto runnable = callback.get();
      if (!runnable)
        return std::unexpected(runnable.error());
      auto result = invoke_instance(*runnable,
                                    "java/lang/Runnable",
                                    "run",
                                    "()V");
      if (!result)
        return std::unexpected(result.error());
      if (result->completed_normally())
        continue;
      if (!result->throwable.has_value())
      {
        return fail(ErrorCode::internal_error,
                    "LCDUI serial callback failed without throwable");
      }
      auto throwable = heap_.class_name(*result->throwable);
      if (!throwable)
        return std::unexpected(throwable.error());
      return fail(ErrorCode::java_exception,
                  "LCDUI serial callback threw " + *throwable);
    }
    return {};
  }

  usize Machine::pending_serial_callbacks() const noexcept
  {
    std::scoped_lock lock(serial_callbacks_mutex_);
    return serial_callbacks_.size();
  }

  Status Machine::register_ui_component(i32 component_id,
                                        ObjectRef object)
  {
    if (component_id <= 0 || object.is_null())
    {
      return fail(ErrorCode::invalid_argument,
                  "LCDUI component registration is invalid");
    }
    ui_components_.insert_or_assign(component_id, object);
    return {};
  }

  void Machine::unregister_ui_component(i32 component_id) noexcept
  {
    ui_components_.erase(component_id);
  }

  Result<ObjectRef> Machine::ui_component(i32 component_id) const
  {
    const auto found = ui_components_.find(component_id);
    if (found == ui_components_.end())
    {
      return fail(ErrorCode::out_of_range,
                  "LCDUI component ID is not registered");
    }
    return found->second;
  }

  double Machine::next_random_double() noexcept
  {
    random_state_ ^= random_state_ >> 12U;
    random_state_ ^= random_state_ << 25U;
    random_state_ ^= random_state_ >> 27U;
    const u64 value = random_state_ * 0x2545F4914F6CDD1DULL;
    return static_cast<double>(value >> 11U) *
           (1.0 / 9007199254740992.0);
  }

  Result<std::string> Machine::mirrored_class_name(ObjectRef mirror) const
  {
    if (mirror.is_null())
      return fail(ErrorCode::invalid_argument,
                  "class mirror reference is null");
    for (const auto &[class_name, reference] : class_mirrors_)
    {
      if (reference == mirror)
        return class_name;
    }
    return fail(ErrorCode::invalid_argument,
                "object is not a registered java/lang/Class mirror");
  }

  Result<bool> Machine::object_is_instance(
      ObjectRef object,
      std::string_view target_class)
  {
    if (object.is_null())
      return false;
    auto source_class = heap_.class_name(object);
    if (!source_class)
      return std::unexpected(source_class.error());
    auto assignable = classes_.is_assignable(*source_class, target_class);
    if (!assignable)
      return std::unexpected(assignable.error());
    if (*assignable)
      return true;
    const auto lambda = lambda_bindings_.find(object.bits);
    if (lambda == lambda_bindings_.end())
      return false;
    return std::find(lambda->second.marker_interfaces.begin(),
                     lambda->second.marker_interfaces.end(),
                     target_class) !=
           lambda->second.marker_interfaces.end();
  }

  void Machine::set_app_property(std::u16string key,
                                 std::u16string value)
  {
    if (key.empty())
      return;
    app_properties_.insert_or_assign(std::move(key), std::move(value));
  }

  Result<std::optional<std::u16string>> Machine::app_property(
      ObjectRef key) const
  {
    if (key.is_null())
      return fail_java("java/lang/NullPointerException",
                       "MIDlet property key is null");
    auto text = heap_.string_value(key);
    if (!text)
      return std::unexpected(text.error());
    const auto property = app_properties_.find(*text);
    if (property == app_properties_.end())
      return std::optional<std::u16string>{};
    return std::optional<std::u16string>(property->second);
  }

  Status Machine::configure_record_store_root(std::string root)
  {
    return record_stores_.configure(std::move(root));
  }

  void Machine::set_permission_policy(
      security::SharedPermissionPolicy policy)
  {
    if (policy)
      permission_policy_ = std::move(policy);
  }

  Status Machine::configure_push_registry(std::string root,
                                          SuiteId suite_id)
  {
    return push_registry_.configure(std::move(root), suite_id);
  }

  Status Machine::configure_filesystem(std::string sandbox_root,
                                       std::string temporary_root)
  {
    return filesystem_.configure(std::move(sandbox_root),
                                 std::move(temporary_root));
  }

  Status Machine::configure_network_owner(i32 owner) noexcept
  {
    if (owner <= 0)
      return fail(ErrorCode::invalid_argument,
                  "network owner must be a positive MIDlet ID");
    connections_.set_owner(owner);
    return {};
  }

  Status Machine::configure_network_adapter(
      std::shared_ptr<network::AsyncNetworkAdapter> adapter)
  {
    return connections_.set_adapter(std::move(adapter));
  }

  void Machine::close_connections() noexcept
  {
    connections_.close_all();
  }

  void Machine::signal_midlet(MidletSignal signal) noexcept
  {
    if (signal == MidletSignal::destroyed ||
        midlet_signal_ != MidletSignal::destroyed)
    {
      midlet_signal_ = signal;
    }
  }

  MidletSignal Machine::consume_midlet_signal() noexcept
  {
    const MidletSignal signal = midlet_signal_;
    midlet_signal_ = MidletSignal::none;
    return signal;
  }

  Result<ExecutionResult> Machine::invoke_static(
      std::string_view class_name,
      std::string_view method_name,
      std::string_view descriptor,
      std::span<const Value> arguments,
      u64 instruction_budget)
  {
    if (method_name != "<clinit>")
    {
      auto initialized = ensure_initialized(class_name, instruction_budget);
      if (!initialized)
      {
        return std::unexpected(initialized.error());
      }
      if (initialized->has_value())
      {
        return ExecutionResult{
            .return_value = std::nullopt,
            .throwable = **initialized,
            .executed_instructions = 0,
        };
      }
    }
    auto resolved = classes_.resolve_method(class_name, method_name, descriptor);
    if (!resolved)
    {
      return std::unexpected(resolved.error());
    }
    if ((resolved->method->access_flags & kAccStatic) == 0)
    {
      return fail(ErrorCode::invalid_state,
                  "invoke_static target is not static");
    }
    auto invocation = prepare_invocation(std::move(*resolved), arguments, false);
    if (!invocation)
    {
      return std::unexpected(invocation.error());
    }
    return execute(std::move(*invocation), instruction_budget);
  }

  Result<ExecutionResult> Machine::invoke_instance(
      ObjectRef receiver,
      std::string_view declared_class,
      std::string_view method_name,
      std::string_view descriptor,
      std::span<const Value> arguments,
      u64 instruction_budget)
  {
    if (receiver.is_null())
    {
      return fail(ErrorCode::invalid_argument,
                  "invoke_instance receiver is null");
    }
    auto runtime_class = heap_.class_name(receiver);
    if (!runtime_class)
    {
      return std::unexpected(runtime_class.error());
    }
    auto receiver_compatible = classes_.is_assignable(*runtime_class,
                                                      declared_class);
    if (!receiver_compatible)
    {
      return std::unexpected(receiver_compatible.error());
    }
    if (!*receiver_compatible)
    {
      return fail(ErrorCode::invalid_argument,
                  "invoke_instance receiver is not assignable to declared class");
    }
    Result<ResolvedMethod> resolved = method_name == "<init>"
                                          ? classes_.resolve_declared_method(declared_class,
                                                                             method_name,
                                                                             descriptor)
                                          : classes_.resolve_method(*runtime_class,
                                                                    method_name,
                                                                    descriptor);
    if (!resolved)
    {
      return std::unexpected(resolved.error());
    }
    if ((resolved->method->access_flags & kAccStatic) != 0)
    {
      return fail(ErrorCode::invalid_state,
                  "invoke_instance target is static");
    }

    std::vector<Value> invocation_arguments;
    invocation_arguments.reserve(arguments.size() + 1);
    invocation_arguments.push_back(Value::from_reference(receiver));
    invocation_arguments.insert(invocation_arguments.end(),
                                arguments.begin(),
                                arguments.end());
    auto invocation = prepare_invocation(std::move(*resolved),
                                         invocation_arguments,
                                         true);
    if (!invocation)
    {
      return std::unexpected(invocation.error());
    }
    return execute(std::move(*invocation), instruction_budget);
  }

  Result<Machine::Invocation> Machine::prepare_invocation(
      ResolvedMethod method,
      std::span<const Value> arguments,
      bool has_receiver)
  {
    if (method.method == nullptr)
    {
      return fail(ErrorCode::method_not_found,
                  "resolved method is null");
    }
    auto descriptor = parse_method_descriptor(method.method->descriptor);
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }

    const usize expected_values = descriptor->parameters.size() +
                                  (has_receiver ? 1U : 0U);
    if (arguments.size() != expected_values)
    {
      return fail(ErrorCode::invalid_argument,
                  "method argument count does not match its descriptor");
    }
    if (has_receiver)
    {
      auto receiver = arguments.front().as_reference();
      if (!receiver || receiver->is_null())
      {
        return fail(ErrorCode::invalid_argument,
                    "instance method receiver is invalid");
      }
    }
    for (usize index = 0; index < descriptor->parameters.size(); ++index)
    {
      const Value &value = arguments[index + (has_receiver ? 1U : 0U)];
      if (!value_matches(value, descriptor->parameters[index]))
      {
        return fail(ErrorCode::invalid_argument,
                    "method argument does not match its descriptor");
      }
    }

    return Invocation{
        .method = std::move(method),
        .arguments = std::vector<Value>(arguments.begin(), arguments.end()),
        .has_receiver = has_receiver,
    };
  }

  Result<std::optional<ObjectRef>>
  Machine::ensure_default_interfaces_initialized(
      const classfile::ClassFile &owner,
      u64 instruction_budget)
  {
    for (const std::string &interface_name : owner.interfaces())
    {
      auto interface_class = classes_.load(interface_name);
      if (!interface_class)
        return std::unexpected(interface_class.error());
      auto parent_result = ensure_default_interfaces_initialized(
          **interface_class,
          instruction_budget);
      if (!parent_result)
        return std::unexpected(parent_result.error());
      if (parent_result->has_value())
        return *parent_result;

      const bool declares_default_method = std::any_of(
          (*interface_class)->methods().begin(),
          (*interface_class)->methods().end(),
          [](const classfile::Method &method)
          {
            const u16 disallowed = kAccPrivate | kAccStatic | kAccAbstract;
            return method.name != "<init>" &&
                   method.name != "<clinit>" &&
                   (method.access_flags & disallowed) == 0U;
          });
      if (!declares_default_method)
        continue;
      auto initialized = ensure_initialized(interface_name,
                                            instruction_budget);
      if (!initialized)
        return std::unexpected(initialized.error());
      if (initialized->has_value())
        return *initialized;
    }
    return std::optional<ObjectRef>{};
  }

  Result<std::optional<ObjectRef>> Machine::ensure_initialized(
      std::string_view class_name,
      u64 instruction_budget)
  {
    auto loaded = classes_.load(class_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }
    const std::string canonical_name = (*loaded)->name();
    static constexpr std::array<std::pair<std::string_view,
                                          std::string_view>, 9>
        primitive_type_owners {{
            {"java/lang/Boolean", "Z"},
            {"java/lang/Byte", "B"},
            {"java/lang/Character", "C"},
            {"java/lang/Short", "S"},
            {"java/lang/Integer", "I"},
            {"java/lang/Long", "J"},
            {"java/lang/Float", "F"},
            {"java/lang/Double", "D"},
            {"java/lang/Void", "V"},
        }};
    const auto primitive_owner = std::find_if(
        primitive_type_owners.begin(), primitive_type_owners.end(),
        [&canonical_name](const auto& entry) {
          return entry.first == canonical_name;
        });
    if (primitive_owner != primitive_type_owners.end())
    {
      auto type_field = states_.resolve_field(
          canonical_name, "TYPE", "Ljava/lang/Class;", true);
      if (!type_field)
        return std::unexpected(type_field.error());
      auto current_type = states_.static_field(*type_field);
      if (!current_type)
        return std::unexpected(current_type.error());
      auto current_mirror = current_type->as_reference();
      if (!current_mirror)
        return std::unexpected(current_mirror.error());
      if (current_mirror->is_null())
      {
        auto mirror = class_mirror(primitive_owner->second);
        if (!mirror)
          return std::unexpected(mirror.error());
        auto stored = states_.set_static_field(
            *type_field, Value::from_reference(*mirror));
        if (!stored)
          return std::unexpected(stored.error());
      }
    }
    if (canonical_name == "java/lang/Boolean")
    {
      const auto initialize_boolean_singleton =
          [this, &canonical_name](std::string_view field_name,
                                  bool value) -> Status
      {
        auto field = states_.resolve_field(
            canonical_name, field_name, "Ljava/lang/Boolean;", true);
        if (!field)
          return std::unexpected(field.error());
        auto current = states_.static_field(*field);
        if (!current)
          return std::unexpected(current.error());
        auto reference = current->as_reference();
        if (!reference)
          return std::unexpected(reference.error());
        if (!reference->is_null())
          return {};

        auto singleton = states_.allocate_instance(heap_, canonical_name);
        if (!singleton)
          return std::unexpected(singleton.error());
        auto stored_value = heap_.set_field(
            *singleton, 0U, Value::from_int(value ? 1 : 0));
        if (!stored_value)
          return std::unexpected(stored_value.error());
        return states_.set_static_field(
            *field, Value::from_reference(*singleton));
      };
      auto true_value = initialize_boolean_singleton("TRUE", true);
      if (!true_value)
        return std::unexpected(true_value.error());
      auto false_value = initialize_boolean_singleton("FALSE", false);
      if (!false_value)
        return std::unexpected(false_value.error());
    }
    if (canonical_name == "java/lang/System" &&
        !system_streams_initialized_)
    {
      auto streams = initialize_system_streams();
      if (!streams)
        return std::unexpected(streams.error());
    }
    if (erroneous_classes_.contains(canonical_name))
    {
      auto throwable = create_throwable("java/lang/NoClassDefFoundError");
      if (!throwable)
      {
        return std::unexpected(throwable.error());
      }
      return std::optional<ObjectRef>(*throwable);
    }
    if (initialized_classes_.contains(canonical_name) ||
        initializing_classes_.contains(canonical_name))
    {
      return std::optional<ObjectRef>{};
    }

    initializing_classes_.insert(canonical_name);
    const auto rollback = [this, &canonical_name]()
    {
      initializing_classes_.erase(canonical_name);
    };

    const bool is_interface =
        ((*loaded)->access_flags() & kAccInterface) != 0U;
    if (!is_interface && !(*loaded)->super_name().empty())
    {
      auto parent = ensure_initialized((*loaded)->super_name(),
                                       instruction_budget);
      if (!parent)
      {
        rollback();
        return std::unexpected(parent.error());
      }
      if (parent->has_value())
      {
        rollback();
        erroneous_classes_.insert(canonical_name);
        return *parent;
      }
    }
    if (!is_interface)
    {
      auto interfaces = ensure_default_interfaces_initialized(
          **loaded,
          instruction_budget);
      if (!interfaces)
      {
        rollback();
        return std::unexpected(interfaces.error());
      }
      if (interfaces->has_value())
      {
        rollback();
        erroneous_classes_.insert(canonical_name);
        return *interfaces;
      }
    }

    if (const classfile::Method *initializer =
            (*loaded)->find_method("<clinit>", "()V");
        initializer != nullptr)
    {
      if ((initializer->access_flags & kAccStatic) == 0)
      {
        rollback();
        return fail(ErrorCode::malformed_class,
                    "class initializer is not static");
      }
      auto result = invoke_static(canonical_name,
                                  "<clinit>",
                                  "()V",
                                  {},
                                  instruction_budget);
      if (!result)
      {
        rollback();
        return std::unexpected(result.error());
      }
      if (result->throwable.has_value())
      {
        auto throwable_class = heap_.class_name(*result->throwable);
        if (!throwable_class)
        {
          rollback();
          return std::unexpected(throwable_class.error());
        }
        auto is_error = classes_.is_assignable(*throwable_class,
                                               "java/lang/Error");
        if (!is_error)
        {
          rollback();
          return std::unexpected(is_error.error());
        }

        rollback();
        erroneous_classes_.insert(canonical_name);
        if (*is_error)
        {
          return result->throwable;
        }
        auto wrapper = create_throwable(
            "java/lang/ExceptionInInitializerError");
        if (!wrapper)
        {
          return std::unexpected(wrapper.error());
        }
        return std::optional<ObjectRef>(*wrapper);
      }
    }

    initializing_classes_.erase(canonical_name);
    initialized_classes_.insert(canonical_name);
    return std::optional<ObjectRef>{};
  }

  Result<std::optional<Value>> Machine::invoke_native(
      const Invocation &invocation)
  {
    if (invocation.method.method == nullptr ||
        invocation.method.owner == nullptr)
    {
      return fail(ErrorCode::internal_error,
                  "native invocation has no resolved method");
    }
    auto descriptor = parse_method_descriptor(
        invocation.method.method->descriptor);
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }
    const std::string &owner_name = invocation.method.owner->name();
    const std::string &method_name = invocation.method.method->name;
    const std::string &method_descriptor =
        invocation.method.method->descriptor;
    if (!natives_.contains(owner_name, method_name, method_descriptor) &&
        is_builtin_class(owner_name) && method_name == "<init>" &&
        method_descriptor == "()V")
    {
      return std::optional<Value>{};
    }

    auto result = natives_.invoke(*this,
                                  owner_name,
                                  method_name,
                                  method_descriptor,
                                  invocation.arguments);
    if (!result)
    {
      return std::unexpected(result.error());
    }
    if (descriptor->return_type.kind == JavaTypeKind::void_type)
    {
      if (result->has_value())
      {
        return fail(ErrorCode::invalid_state,
                    "void native method returned a value");
      }
    }
    else
    {
      if (!result->has_value() ||
          !value_matches(**result, descriptor->return_type))
      {
        return fail(ErrorCode::invalid_state,
                    "native method result does not match its descriptor");
      }
    }
    return *result;
  }

  Result<ObjectRef> Machine::intern_string(std::string_view modified_utf8)
  {
    auto decoded = decode_modified_utf8(modified_utf8);
    if (!decoded)
    {
      return std::unexpected(decoded.error());
    }
    if (const auto iterator = interned_strings_.find(*decoded);
        iterator != interned_strings_.end())
    {
      auto existing = heap_.string_value(iterator->second);
      if (existing)
      {
        return iterator->second;
      }
      interned_strings_.erase(iterator);
    }

    auto initialized = ensure_initialized("java/lang/String", 1'000'000);
    if (!initialized)
    {
      return std::unexpected(initialized.error());
    }
    if (initialized->has_value())
    {
      return fail(ErrorCode::invalid_state,
                  "java/lang/String initialization threw an exception");
    }
    auto reference = states_.allocate_instance(heap_, "java/lang/String");
    if (!reference)
    {
      return std::unexpected(reference.error());
    }
    auto attached = heap_.attach_string(*reference, *decoded);
    if (!attached)
    {
      return std::unexpected(attached.error());
    }
    interned_strings_.emplace(std::move(*decoded), *reference);
    return *reference;
  }

  Result<ObjectRef> Machine::class_mirror(std::string_view class_name)
  {
    if (class_name.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "class mirror name must not be empty");
    }
    const std::string key(class_name);
    const auto existing = class_mirrors_.find(key);
    if (existing != class_mirrors_.end())
    {
      return existing->second;
    }
    auto mirror = states_.allocate_instance(heap_, "java/lang/Class");
    if (!mirror)
      return std::unexpected(mirror.error());
    class_mirrors_.emplace(key, *mirror);
    return *mirror;
  }

  Result<std::optional<ObjectRef>> Machine::acquire_synchronized_monitor(
      const Invocation &invocation)
  {
    if (invocation.method.method == nullptr ||
        (invocation.method.method->access_flags & kAccSynchronized) == 0U)
    {
      return std::optional<ObjectRef>{};
    }

    ObjectRef monitor;
    if ((invocation.method.method->access_flags & kAccStatic) != 0U)
    {
      if (invocation.method.owner == nullptr)
      {
        return fail(ErrorCode::internal_error,
                    "static synchronized invocation has no owner class");
      }
      auto mirror = class_mirror(invocation.method.owner->name());
      if (!mirror)
        return std::unexpected(mirror.error());
      monitor = *mirror;
    }
    else
    {
      if (invocation.arguments.empty())
      {
        return fail(ErrorCode::internal_error,
                    "instance synchronized invocation has no receiver");
      }
      auto receiver = invocation.arguments.front().as_reference();
      if (!receiver)
        return std::unexpected(receiver.error());
      if (receiver->is_null())
      {
        return fail(ErrorCode::invalid_argument,
                    "instance synchronized receiver is null");
      }
      monitor = *receiver;
    }

    auto entered = enter_monitor(monitor);
    if (!entered)
      return std::unexpected(entered.error());
    return std::optional<ObjectRef>(monitor);
  }

  Status Machine::release_synchronized_monitor(
      std::optional<ObjectRef> monitor)
  {
    if (!monitor.has_value())
      return {};
    return monitors_.exit(*monitor, scheduler_.current_thread_id());
  }

  Result<Machine::LambdaBinding> Machine::resolve_lambda_binding(
      const classfile::ClassFile &owner,
      u16 invoke_dynamic_index)
  {
    auto dynamic = owner.invoke_dynamic_reference(invoke_dynamic_index);
    if (!dynamic)
      return std::unexpected(dynamic.error());
    auto call_site = parse_method_descriptor(dynamic->descriptor);
    if (!call_site)
      return std::unexpected(call_site.error());
    if (call_site->return_type.kind != JavaTypeKind::reference)
    {
      return fail(ErrorCode::unsupported_feature,
                  "LambdaMetafactory call site must return an interface");
    }

    auto interface_class = classes_.load(call_site->return_type.class_name);
    if (!interface_class)
      return std::unexpected(interface_class.error());
    if (((*interface_class)->access_flags() & kAccInterface) == 0U)
    {
      return fail(ErrorCode::unsupported_feature,
                  "LambdaMetafactory return type is not an interface");
    }

    auto bootstrap = owner.bootstrap_method(dynamic->bootstrap_method_index);
    if (!bootstrap)
      return std::unexpected(bootstrap.error());
    auto bootstrap_handle = owner.method_handle_reference(
        (*bootstrap)->method_handle_index);
    if (!bootstrap_handle)
      return std::unexpected(bootstrap_handle.error());
    const bool is_metafactory =
        bootstrap_handle->member.owner ==
            "java/lang/invoke/LambdaMetafactory" &&
        (bootstrap_handle->member.name == "metafactory" ||
         bootstrap_handle->member.name == "altMetafactory");
    if (!is_metafactory || bootstrap_handle->reference_kind != 6U)
    {
      return fail(ErrorCode::unsupported_feature,
                  "unsupported invokedynamic bootstrap method");
    }
    if ((*bootstrap)->arguments.size() < 3U)
    {
      return fail(ErrorCode::malformed_class,
                  "LambdaMetafactory bootstrap arguments are incomplete");
    }

    auto sam_descriptor = owner.method_type_descriptor(
        (*bootstrap)->arguments[0]);
    auto implementation = owner.method_handle_reference(
        (*bootstrap)->arguments[1]);
    auto instantiated_descriptor = owner.method_type_descriptor(
        (*bootstrap)->arguments[2]);
    if (!sam_descriptor)
      return std::unexpected(sam_descriptor.error());
    if (!implementation)
      return std::unexpected(implementation.error());
    if (!instantiated_descriptor)
      return std::unexpected(instantiated_descriptor.error());

    std::vector<std::string> marker_interfaces;
    std::vector<std::string> bridge_descriptors;
    if (bootstrap_handle->member.name == "altMetafactory")
    {
      if ((*bootstrap)->arguments.size() < 4U)
      {
        return fail(ErrorCode::malformed_class,
                    "altMetafactory bootstrap arguments are incomplete");
      }
      const auto integer_argument = [&owner](u16 index) -> Result<u32>
      {
        auto constant = owner.constant(index);
        if (!constant)
          return std::unexpected(constant.error());
        if ((*constant)->kind != classfile::ConstantKind::integer)
        {
          return fail(ErrorCode::malformed_class,
                      "altMetafactory count or flags argument is not an integer");
        }
        return static_cast<u32>((*constant)->bits);
      };

      auto flags = integer_argument((*bootstrap)->arguments[3]);
      if (!flags)
        return std::unexpected(flags.error());
      constexpr u32 kSerializable = 0x01U;
      constexpr u32 kMarkers = 0x02U;
      constexpr u32 kBridges = 0x04U;
      if ((*flags & ~(kSerializable | kMarkers | kBridges)) != 0U)
      {
        return fail(ErrorCode::unsupported_feature,
                    "altMetafactory contains unsupported flags");
      }
      usize cursor = 4U;
      if ((*flags & kSerializable) != 0U)
      {
        marker_interfaces.push_back("java/io/Serializable");
      }
      if ((*flags & kMarkers) != 0U)
      {
        if (cursor >= (*bootstrap)->arguments.size())
        {
          return fail(ErrorCode::malformed_class,
                      "altMetafactory marker count is missing");
        }
        auto marker_count = integer_argument((*bootstrap)->arguments[cursor++]);
        if (!marker_count)
          return std::unexpected(marker_count.error());
        if (static_cast<usize>(*marker_count) >
            (*bootstrap)->arguments.size() - cursor)
        {
          return fail(ErrorCode::malformed_class,
                      "altMetafactory marker list is truncated");
        }
        marker_interfaces.reserve(marker_interfaces.size() +
                                  static_cast<usize>(*marker_count));
        for (u32 marker_index = 0; marker_index < *marker_count;
             ++marker_index)
        {
          auto marker = owner.class_name_constant(
              (*bootstrap)->arguments[cursor++]);
          if (!marker)
            return std::unexpected(marker.error());
          marker_interfaces.push_back(std::move(*marker));
        }
      }
      if ((*flags & kBridges) != 0U)
      {
        if (cursor >= (*bootstrap)->arguments.size())
        {
          return fail(ErrorCode::malformed_class,
                      "altMetafactory bridge count is missing");
        }
        auto bridge_count = integer_argument((*bootstrap)->arguments[cursor++]);
        if (!bridge_count)
          return std::unexpected(bridge_count.error());
        if (static_cast<usize>(*bridge_count) >
            (*bootstrap)->arguments.size() - cursor)
        {
          return fail(ErrorCode::malformed_class,
                      "altMetafactory bridge list is truncated");
        }
        bridge_descriptors.reserve(static_cast<usize>(*bridge_count));
        for (u32 bridge_index = 0; bridge_index < *bridge_count;
             ++bridge_index)
        {
          auto bridge = owner.method_type_descriptor(
              (*bootstrap)->arguments[cursor++]);
          if (!bridge)
            return std::unexpected(bridge.error());
          bridge_descriptors.push_back(std::move(*bridge));
        }
      }
      if (cursor != (*bootstrap)->arguments.size())
      {
        return fail(ErrorCode::malformed_class,
                    "altMetafactory has unexpected trailing arguments");
      }

      std::sort(marker_interfaces.begin(), marker_interfaces.end());
      marker_interfaces.erase(
          std::unique(marker_interfaces.begin(), marker_interfaces.end()),
          marker_interfaces.end());
      for (const std::string &marker : marker_interfaces)
      {
        auto marker_class = classes_.load(marker);
        if (!marker_class)
          return std::unexpected(marker_class.error());
        if (((*marker_class)->access_flags() & kAccInterface) == 0U)
        {
          return fail(ErrorCode::unsupported_feature,
                      "altMetafactory marker is not an interface");
        }
      }
    }

    auto sam_type = parse_method_descriptor(*sam_descriptor);
    auto instantiated_type = parse_method_descriptor(*instantiated_descriptor);
    auto implementation_type = parse_method_descriptor(
        implementation->member.descriptor);
    if (!sam_type)
      return std::unexpected(sam_type.error());
    if (!instantiated_type)
      return std::unexpected(instantiated_type.error());
    if (!implementation_type)
      return std::unexpected(implementation_type.error());
    if (sam_type->parameters.size() != instantiated_type->parameters.size())
    {
      return fail(ErrorCode::unsupported_feature,
                  "LambdaMetafactory parameter adaptation is unsupported");
    }

    auto sam_method = classes_.resolve_method(call_site->return_type.class_name,
                                              dynamic->name,
                                              *sam_descriptor);
    if (!sam_method)
      return std::unexpected(sam_method.error());
    if ((sam_method->method->access_flags & kAccStatic) != 0U)
    {
      return fail(ErrorCode::malformed_class,
                  "lambda target method is static on its interface");
    }

    const usize combined_values = call_site->parameters.size() +
                                  instantiated_type->parameters.size();
    usize implementation_values = implementation_type->parameters.size();
    switch (implementation->reference_kind)
    {
    case 5:
    case 7:
    case 9:
      ++implementation_values;
      break;
    case 6:
    case 8:
      break;
    default:
      return fail(ErrorCode::unsupported_feature,
                  "unsupported lambda implementation method-handle kind");
    }
    if (combined_values != implementation_values)
    {
      return fail(ErrorCode::unsupported_feature,
                  "LambdaMetafactory argument adaptation is unsupported");
    }
    if (implementation->reference_kind == 8U)
    {
      if (instantiated_type->return_type.kind != JavaTypeKind::reference)
      {
        return fail(ErrorCode::unsupported_feature,
                    "constructor method reference must return an object");
      }
      auto compatible = classes_.is_assignable(
          implementation->member.owner,
          instantiated_type->return_type.class_name);
      if (!compatible)
        return std::unexpected(compatible.error());
      if (!*compatible)
      {
        return fail(ErrorCode::unsupported_feature,
                    "constructor result is incompatible with lambda return type");
      }
    }

    return LambdaBinding{
        .interface_name = call_site->return_type.class_name,
        .sam_name = dynamic->name,
        .sam_descriptor = std::move(*sam_descriptor),
        .instantiated_descriptor = std::move(*instantiated_descriptor),
        .implementation_kind = implementation->reference_kind,
        .implementation = std::move(implementation->member),
        .marker_interfaces = std::move(marker_interfaces),
        .bridge_descriptors = std::move(bridge_descriptors),
        .captured_count = call_site->parameters.size(),
    };
  }

  Result<Machine::Invocation> Machine::prepare_lambda_invocation(
      ObjectRef receiver,
      const LambdaBinding &binding,
      std::span<const Value> invocation_arguments,
      std::optional<ObjectRef> constructor_receiver)
  {
    auto instantiated = parse_method_descriptor(
        binding.instantiated_descriptor);
    if (!instantiated)
      return std::unexpected(instantiated.error());
    if (invocation_arguments.size() != instantiated->parameters.size())
    {
      return fail(ErrorCode::invalid_argument,
                  "lambda invocation argument count is invalid");
    }

    std::vector<Value> combined;
    combined.reserve(binding.captured_count + invocation_arguments.size());
    for (usize index = 0; index < binding.captured_count; ++index)
    {
      auto captured = heap_.field(receiver, index);
      if (!captured)
        return std::unexpected(captured.error());
      combined.push_back(*captured);
    }
    combined.insert(combined.end(),
                    invocation_arguments.begin(),
                    invocation_arguments.end());

    if (binding.implementation_kind == 8U)
    {
      if (!constructor_receiver.has_value() ||
          constructor_receiver->is_null())
      {
        return fail(ErrorCode::invalid_argument,
                    "constructor lambda has no allocated receiver");
      }
      auto target = classes_.resolve_declared_method(
          binding.implementation.owner,
          binding.implementation.name,
          binding.implementation.descriptor);
      if (!target)
        return std::unexpected(target.error());
      std::vector<Value> constructor_arguments;
      constructor_arguments.reserve(combined.size() + 1U);
      constructor_arguments.push_back(
          Value::from_reference(*constructor_receiver));
      constructor_arguments.insert(constructor_arguments.end(),
                                   combined.begin(),
                                   combined.end());
      auto invocation = prepare_invocation(std::move(*target),
                                           constructor_arguments,
                                           true);
      if (!invocation)
        return std::unexpected(invocation.error());
      invocation->return_override = Value::from_reference(
          *constructor_receiver);
      return invocation;
    }

    Result<ResolvedMethod> target = fail(ErrorCode::internal_error,
                                         "lambda target was not resolved");
    bool has_receiver = false;
    if (binding.implementation_kind == 6U)
    {
      target = classes_.resolve_method(binding.implementation.owner,
                                       binding.implementation.name,
                                       binding.implementation.descriptor);
    }
    else
    {
      if (combined.empty())
      {
        return fail(ErrorCode::invalid_argument,
                    "instance lambda target has no receiver");
      }
      auto target_receiver = combined.front().as_reference();
      if (!target_receiver || target_receiver->is_null())
      {
        return fail(ErrorCode::invalid_argument,
                    "instance lambda target receiver is null");
      }
      if (binding.implementation_kind == 7U)
      {
        target = classes_.resolve_declared_method(
            binding.implementation.owner,
            binding.implementation.name,
            binding.implementation.descriptor);
      }
      else
      {
        auto runtime_class = heap_.class_name(*target_receiver);
        if (!runtime_class)
          return std::unexpected(runtime_class.error());
        if (binding.implementation_kind == 9U)
        {
          auto compatible = classes_.is_assignable(
              *runtime_class,
              binding.implementation.owner);
          if (!compatible)
            return std::unexpected(compatible.error());
          if (!*compatible)
          {
            return fail(ErrorCode::invalid_argument,
                        "lambda target receiver does not implement interface");
          }
        }
        target = classes_.resolve_method(*runtime_class,
                                         binding.implementation.name,
                                         binding.implementation.descriptor);
      }
      has_receiver = true;
    }
    if (!target)
      return std::unexpected(target.error());
    return prepare_invocation(std::move(*target), combined, has_receiver);
  }

  void Machine::prune_lambda_bindings()
  {
    for (auto iterator = lambda_bindings_.begin();
         iterator != lambda_bindings_.end();)
    {
      if (!heap_.class_name(ObjectRef{iterator->first}))
      {
        iterator = lambda_bindings_.erase(iterator);
      }
      else
      {
        ++iterator;
      }
    }
  }

  Result<ObjectRef> Machine::create_throwable(std::string_view class_name)
  {
    if (class_name == "java/lang/OutOfMemoryError" &&
        !emergency_out_of_memory_error_.is_null())
    {
      auto emergency_class = heap_.class_name(emergency_out_of_memory_error_);
      if (emergency_class && *emergency_class == class_name)
        return emergency_out_of_memory_error_;
    }
    auto throwable_class = classes_.load(class_name);
    if (!throwable_class)
    {
      return std::unexpected(throwable_class.error());
    }
    auto assignable = classes_.is_assignable((*throwable_class)->name(),
                                             "java/lang/Throwable");
    if (!assignable)
    {
      return std::unexpected(assignable.error());
    }
    if (!*assignable)
    {
      return fail(ErrorCode::invalid_argument,
                  "requested implicit exception class is not Throwable");
    }
    return states_.allocate_instance(heap_, (*throwable_class)->name());
  }

  Result<Value> Machine::load_constant(const classfile::ClassFile &owner,
                                       u16 index,
                                       bool category_two_only)
  {
    auto constant = owner.constant(index);
    if (!constant)
    {
      return std::unexpected(constant.error());
    }
    if ((*constant)->kind == classfile::ConstantKind::class_ref)
    {
      if (category_two_only)
      {
        return fail(ErrorCode::malformed_class,
                    "ldc2_w cannot load a Java Class");
      }
      auto class_name = owner.class_name_constant(index);
      if (!class_name)
      {
        return std::unexpected(class_name.error());
      }
      auto mirror = class_mirror(*class_name);
      if (!mirror)
        return std::unexpected(mirror.error());
      return Value::from_reference(*mirror);
    }
    if ((*constant)->kind == classfile::ConstantKind::string_ref)
    {
      if (category_two_only)
      {
        return fail(ErrorCode::malformed_class,
                    "ldc2_w cannot load a Java String");
      }
      auto encoded = owner.string_constant(index);
      if (!encoded)
      {
        return std::unexpected(encoded.error());
      }
      auto reference = intern_string(*encoded);
      if (!reference)
      {
        return std::unexpected(reference.error());
      }
      return Value::from_reference(*reference);
    }
    return constant_value(owner, index, category_two_only);
  }

  Result<ExecutionResult> Machine::execute(Invocation invocation,
                                           u64 instruction_budget)
  {
    if (g_execution_machine != nullptr && g_execution_machine != this)
    {
      return fail(ErrorCode::invalid_state,
                  "a host thread cannot execute two Machine instances recursively");
    }
    execution_mutex_.lock();
    g_execution_machine = this;
    ++g_execution_lock_depth;
    scheduler_.set_current_state(JavaThreadState::running);
    const u32 invocation_depth = g_execution_lock_depth;
    const JavaThreadId invocation_thread = scheduler_.current_thread_id();
    const HeapAccessContext previous_heap_context =
        current_heap_access_context();
    u64 accounted_instructions = 0U;
    auto cleanup = [invocation_depth,
                    invocation_thread,
                    previous_heap_context,
                    &accounted_instructions](Machine* machine) {
      machine->scheduler_.add_current_executed_instructions(
          accounted_instructions);
      machine->clear_execution_roots(invocation_depth);
      set_heap_access_context(previous_heap_context);
      if (g_execution_lock_depth == 1U)
        machine->monitors_.release_all(invocation_thread);
      --g_execution_lock_depth;
      machine->execution_mutex_.unlock();
      if (g_execution_lock_depth == 0U)
        g_execution_machine = nullptr;
    };
    std::unique_ptr<Machine, decltype(cleanup)> execution_scope(this, cleanup);

    scheduler_.set_current_pending_exception(std::nullopt);
    std::vector<ObjectRef> invocation_roots;
    invocation_roots.reserve(invocation.arguments.size());
    for (const Value argument : invocation.arguments)
    {
      if (argument.kind() != ValueKind::reference)
        continue;
      auto reference = argument.as_reference();
      if (reference && !reference->is_null())
        invocation_roots.push_back(*reference);
    }
    publish_execution_roots(invocation_depth, invocation_roots);

    if (invocation.method.method == nullptr)
    {
      return fail(ErrorCode::internal_error,
                  "VM invocation has no resolved method");
    }
    set_heap_access_context(HeapAccessContext {
        .owner = invocation.method.owner != nullptr
            ? invocation.method.owner->name() : std::string_view {},
        .method = invocation.method.method->name,
        .descriptor = invocation.method.method->descriptor,
        .bytecode_pc = 0U,
    });
    if ((invocation.method.method->access_flags & kAccAbstract) != 0U)
    {
      auto throwable = create_throwable("java/lang/AbstractMethodError");
      if (!throwable)
      {
        return std::unexpected(throwable.error());
      }
      scheduler_.set_current_pending_exception(*throwable);
      return ExecutionResult{
          .return_value = std::nullopt,
          .throwable = *throwable,
          .executed_instructions = 0,
      };
    }
    auto root_monitor = acquire_synchronized_monitor(invocation);
    if (!root_monitor)
    {
      return std::unexpected(root_monitor.error());
    }

    const bool has_native_override = invocation.method.owner != nullptr &&
                                     natives_.contains(invocation.method.owner->name(),
                                                       invocation.method.method->name,
                                                       invocation.method.method->descriptor);
    if (has_native_override || !invocation.method.method->code.has_value())
    {
      auto native_result = invoke_native(invocation);
      auto released = release_synchronized_monitor(*root_monitor);
      if (!released)
        return std::unexpected(released.error());
      if (!native_result)
      {
        if (native_result.error().code == ErrorCode::java_exception)
        {
          if (native_result.error().java_exception_class.empty())
          {
            return fail(ErrorCode::internal_error,
                        "native Java exception has no class name");
          }
          auto throwable = create_throwable(
              native_result.error().java_exception_class);
          if (!throwable)
            return std::unexpected(throwable.error());
          scheduler_.set_current_pending_exception(*throwable);
          return ExecutionResult{
              .return_value = std::nullopt,
              .throwable = *throwable,
              .executed_instructions = 0,
          };
        }
        if (native_result.error().code == ErrorCode::unsupported_feature &&
            (invocation.method.method->access_flags & kAccNative) != 0U)
        {
          auto throwable = create_throwable(
              "java/lang/UnsatisfiedLinkError");
          if (!throwable)
          {
            return std::unexpected(throwable.error());
          }
          scheduler_.set_current_pending_exception(*throwable);
          return ExecutionResult{
              .return_value = std::nullopt,
              .throwable = *throwable,
              .executed_instructions = 0,
          };
        }
        return std::unexpected(native_result.error());
      }
      return ExecutionResult{
          .return_value = *native_result,
          .throwable = std::nullopt,
          .executed_instructions = 0,
      };
    }

    auto root_descriptor = parse_method_descriptor(
        invocation.method.method->descriptor);
    if (!root_descriptor)
    {
      return std::unexpected(root_descriptor.error());
    }
    auto root_frame = ExecutionFrame::make(std::move(invocation.method),
                                           std::move(*root_descriptor),
                                           invocation.arguments,
                                           invocation.has_receiver);
    if (!root_frame)
    {
      auto released = release_synchronized_monitor(*root_monitor);
      if (!released)
        return std::unexpected(released.error());
      return std::unexpected(root_frame.error());
    }
    if (root_monitor->has_value())
    {
      root_frame->set_synchronized_monitor(**root_monitor);
    }

    std::vector<ExecutionFrame> frames;
    frames.reserve(32);
    frames.push_back(std::move(*root_frame));
    u64 executed = 0;

    const auto collect_active_garbage =
        [this, &frames, invocation_depth](
            std::optional<ObjectRef> extra_root = std::nullopt)
        -> Status
    {
      std::vector<ObjectRef> roots;
      roots.reserve(frames.size() * 8U + interned_strings_.size() +
                    class_mirrors_.size() + ui_components_.size() + 16U);
      for (const ExecutionFrame &active_frame : frames)
      {
        active_frame.append_reference_roots(roots);
      }
      publish_execution_roots(invocation_depth, roots);
      states_.append_reference_roots(roots);
      if (!emergency_out_of_memory_error_.is_null())
        roots.push_back(emergency_out_of_memory_error_);
      for (const auto &[value, reference] : interned_strings_)
      {
        (void)value;
        if (!reference.is_null())
          roots.push_back(reference);
      }
      for (const auto &[class_name, reference] : class_mirrors_)
      {
        (void)class_name;
        if (!reference.is_null())
          roots.push_back(reference);
      }
      for (const auto &[component_id, reference] : ui_components_)
      {
        (void)component_id;
        if (!reference.is_null())
          roots.push_back(reference);
      }
      if (canvas_bridge_ != nullptr)
        canvas_bridge_->append_reference_roots(roots);
      monitors_.append_reference_roots(roots);
      timers_.append_reference_roots(roots);
      scheduler_.append_reference_roots(roots);
      native_roots_.append_reference_roots(roots);
      if (extra_root.has_value() && !extra_root->is_null())
      {
        roots.push_back(*extra_root);
      }
      auto collected = heap_.collect(roots);
      if (collected)
        prune_lambda_bindings();
      return collected;
    };

    const auto allocate_instance_with_gc =
        [this, &collect_active_garbage](std::string_view class_name)
        -> Result<ObjectRef>
    {
      auto object = states_.allocate_instance(heap_, class_name);
      if (object || object.error().code != ErrorCode::overflow)
      {
        return object;
      }
      auto collected = collect_active_garbage();
      if (!collected)
        return std::unexpected(collected.error());
      return states_.allocate_instance(heap_, class_name);
    };

    const auto allocate_raw_object_with_gc =
        [this, &collect_active_garbage](std::string_view class_name,
                                        usize field_count)
        -> Result<ObjectRef>
    {
      auto object = heap_.allocate_object(std::string(class_name), field_count);
      if (object || object.error().code != ErrorCode::overflow)
      {
        return object;
      }
      auto collected = collect_active_garbage();
      if (!collected)
        return std::unexpected(collected.error());
      return heap_.allocate_object(std::string(class_name), field_count);
    };

    const auto allocate_array_with_gc =
        [this, &collect_active_garbage](std::string_view class_name,
                                        usize length,
                                        Value initial_value,
                                        std::optional<ObjectRef> extra_root =
                                            std::nullopt)
        -> Result<ObjectRef>
    {
      auto array = heap_.allocate_array(std::string(class_name),
                                        length,
                                        initial_value);
      if (array || array.error().code != ErrorCode::overflow)
      {
        return array;
      }
      auto collected = collect_active_garbage(extra_root);
      if (!collected)
        return std::unexpected(collected.error());
      return heap_.allocate_array(std::string(class_name),
                                  length,
                                  initial_value);
    };

    const auto complete_return = [this, &frames, &executed](
                                     std::optional<Value> value)
        -> Result<std::optional<ExecutionResult>>
    {
      if (frames.back().return_override().has_value())
        value = frames.back().return_override();
      auto released = release_synchronized_monitor(
          frames.back().synchronized_monitor());
      if (!released)
        return std::unexpected(released.error());
      frames.pop_back();
      if (frames.empty())
      {
        return std::optional<ExecutionResult>(ExecutionResult{
            .return_value = value,
            .throwable = std::nullopt,
            .executed_instructions = executed,
        });
      }
      if (value.has_value())
      {
        auto pushed = frames.back().push(*value);
        if (!pushed)
        {
          return std::unexpected(pushed.error());
        }
      }
      return std::optional<ExecutionResult>{};
    };

    const auto dispatch_exception =
        [this, &frames, &executed](ObjectRef throwable, usize throw_pc)
        -> Result<std::optional<ExecutionResult>>
    {
      if (throwable.is_null())
      {
        return fail(ErrorCode::internal_error,
                    "cannot dispatch a null Java throwable");
      }
      scheduler_.set_current_pending_exception(throwable);
      auto throwable_class = heap_.class_name(throwable);
      if (!throwable_class)
      {
        return std::unexpected(throwable_class.error());
      }
      auto is_throwable = classes_.is_assignable(*throwable_class,
                                                 "java/lang/Throwable");
      if (!is_throwable)
      {
        return std::unexpected(is_throwable.error());
      }
      if (!*is_throwable)
      {
        return fail(ErrorCode::malformed_class,
                    "athrow operand is not assignable to Throwable");
      }

      const std::string exception_context = frames.empty()
          ? std::string {}
          : frames.back().owner().name() + "." +
                frames.back().method().name +
                frames.back().method().descriptor + " at bytecode " +
                std::to_string(throw_pc);
      usize current_throw_pc = throw_pc;
      while (!frames.empty())
      {
        ExecutionFrame &current = frames.back();
        for (const classfile::ExceptionHandler &handler :
             current.exception_table())
        {
          if (current_throw_pc < static_cast<usize>(handler.start_pc) ||
              current_throw_pc >= static_cast<usize>(handler.end_pc))
          {
            continue;
          }

          bool catches = handler.catch_type.empty();
          if (!catches)
          {
            auto assignable = classes_.is_assignable(*throwable_class,
                                                     handler.catch_type);
            if (!assignable)
            {
              return std::unexpected(assignable.error());
            }
            catches = *assignable;
          }
          if (!catches)
          {
            continue;
          }

          auto entered = current.enter_exception_handler(
              static_cast<usize>(handler.handler_pc), throwable);
          if (!entered)
          {
            return std::unexpected(entered.error());
          }
          scheduler_.set_current_pending_exception(std::nullopt);
          return std::optional<ExecutionResult>{};
        }

        auto released = release_synchronized_monitor(
            current.synchronized_monitor());
        if (!released)
          return std::unexpected(released.error());
        frames.pop_back();
        if (!frames.empty())
        {
          current_throw_pc = frames.back().current_instruction_pc();
        }
      }

      return std::optional<ExecutionResult>(ExecutionResult{
          .return_value = std::nullopt,
          .throwable = throwable,
          .executed_instructions = executed,
          .exception_context = exception_context,
      });
    };

    const auto raise_implicit =
        [this, &dispatch_exception](std::string_view class_name,
                                    usize throw_pc)
        -> Result<std::optional<ExecutionResult>>
    {
      auto throwable = create_throwable(class_name);
      if (!throwable)
      {
        return std::unexpected(throwable.error());
      }
      return dispatch_exception(*throwable, throw_pc);
    };

    while (!frames.empty())
    {
      if (scheduler_.current_stop_requested())
      {
        return fail(ErrorCode::invalid_state,
                    "VM execution was cancelled by scheduler shutdown");
      }

      const bool quantum_boundary =
          executed != 0U && (executed % kSchedulerQuantum) == 0U;
      const bool collect_requested =
          gc_requested_.exchange(false, std::memory_order_acq_rel);
      if (quantum_boundary || collect_requested)
      {
        std::vector<ObjectRef> published_roots;
        published_roots.reserve(frames.size() * 8U + 8U);
        for (const ExecutionFrame& active_frame : frames)
          active_frame.append_reference_roots(published_roots);
        publish_execution_roots(invocation_depth, published_roots);
        if (collect_requested)
        {
          auto collected = collect_active_garbage();
          if (!collected)
            return std::unexpected(collected.error());
        }
        if (quantum_boundary)
          cooperative_yield();
      }

      if (executed >= instruction_budget)
      {
        return fail(ErrorCode::invalid_state,
                    "VM instruction budget was exhausted");
      }
      if (frames.size() > kMaximumCallDepth)
      {
        return fail(ErrorCode::invalid_state,
                    "VM call stack exceeded its maximum depth");
      }
      ++executed;
      accounted_instructions = executed;

      ExecutionFrame &frame = frames.back();
      const usize opcode_pc = frame.pc();
      frame.begin_instruction(opcode_pc);
      set_heap_access_context(HeapAccessContext {
          .owner = frame.owner().name(),
          .method = frame.method().name,
          .descriptor = frame.method().descriptor,
          .bytecode_pc = opcode_pc,
      });
      auto opcode_result = frame.read_u8();
      if (!opcode_result)
      {
        return std::unexpected(opcode_result.error());
      }
      const u8 opcode = *opcode_result;

      switch (opcode)
      {
      case 0x00:
        break;
      case 0x01:
      {
        auto pushed = frame.push(Value::from_reference({}));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x02:
      case 0x03:
      case 0x04:
      case 0x05:
      case 0x06:
      case 0x07:
      case 0x08:
      {
        auto pushed = frame.push(
            Value::from_int(static_cast<i32>(opcode) - 3));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x09:
      case 0x0A:
      {
        auto pushed = frame.push(Value::from_long(opcode - 0x09));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x0B:
      case 0x0C:
      case 0x0D:
      {
        auto pushed = frame.push(
            Value::from_float(static_cast<float>(opcode - 0x0B)));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x0E:
      case 0x0F:
      {
        auto pushed = frame.push(
            Value::from_double(static_cast<double>(opcode - 0x0E)));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x10:
      {
        auto immediate = frame.read_i8();
        if (!immediate)
          return std::unexpected(immediate.error());
        auto pushed = frame.push(Value::from_int(*immediate));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x11:
      {
        auto immediate = frame.read_i16();
        if (!immediate)
          return std::unexpected(immediate.error());
        auto pushed = frame.push(Value::from_int(*immediate));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x12:
      case 0x13:
      case 0x14:
      {
        Result<u16> index = opcode == 0x12
                                ? frame.read_u8().transform([](u8 value)
                                                            { return static_cast<u16>(value); })
                                : frame.read_u16();
        if (!index)
          return std::unexpected(index.error());
        auto value = load_constant(frame.owner(), *index, opcode == 0x14);
        if (!value)
          return std::unexpected(value.error());
        auto pushed = frame.push(*value);
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x15:
      case 0x16:
      case 0x17:
      case 0x18:
      case 0x19:
      {
        auto index = frame.read_u8();
        if (!index)
          return std::unexpected(index.error());
        auto value = frame.local(*index);
        if (!value)
          return std::unexpected(value.error());
        if (!load_kind_matches(opcode, value->kind()))
        {
          return fail(ErrorCode::malformed_class,
                      "local load opcode does not match value kind");
        }
        auto pushed = frame.push(*value);
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x1A:
      case 0x1B:
      case 0x1C:
      case 0x1D:
      case 0x1E:
      case 0x1F:
      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23:
      case 0x24:
      case 0x25:
      case 0x26:
      case 0x27:
      case 0x28:
      case 0x29:
      case 0x2A:
      case 0x2B:
      case 0x2C:
      case 0x2D:
      {
        auto value = frame.local(local_index_for_load(opcode));
        if (!value)
          return std::unexpected(value.error());
        if (!load_kind_matches(opcode, value->kind()))
        {
          return fail(ErrorCode::malformed_class,
                      "fixed local load opcode does not match value kind");
        }
        auto pushed = frame.push(*value);
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x2E:
      case 0x2F:
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33:
      case 0x34:
      case 0x35:
      {
        auto index = pop_int(frame);
        auto array = pop_reference(frame);
        if (!index || !array)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid array load operands");
        }
        if (array->is_null())
        {
          auto raised = raise_implicit("java/lang/NullPointerException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto length = heap_.array_length(*array);
        if (!length)
          return std::unexpected(length.error());
        if (*index < 0 || static_cast<usize>(*index) >= *length)
        {
          if (std::getenv("PHONEME_TRACE_ARRAY_BOUNDS") != nullptr)
          {
            auto traced_class = heap_.class_name(*array);
            const std::string descriptor = traced_class
                ? *traced_class
                : std::string("<unknown>");
            std::fprintf(stderr,
                         "[array-bounds] method=%s.%s%s pc=%zu array=%s "
                         "index=%d length=%zu\n",
                         frame.owner().name().c_str(),
                         frame.method().name.c_str(),
                         frame.method().descriptor.c_str(),
                         opcode_pc, descriptor.c_str(), *index, *length);
          }
          auto raised = raise_implicit(
              "java/lang/ArrayIndexOutOfBoundsException", opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto array_class = heap_.class_name(*array);
        if (!array_class)
          return std::unexpected(array_class.error());
        if (!array_load_class_matches(opcode, *array_class))
        {
          return fail(ErrorCode::malformed_class,
                      "array load opcode does not match array descriptor: " +
                          *array_class);
        }
        auto value = heap_.element(*array, static_cast<usize>(*index));
        if (!value)
          return std::unexpected(value.error());
        if (opcode == 0x33 || opcode == 0x34 || opcode == 0x35)
        {
          auto integer = value->as_int();
          if (!integer)
            return std::unexpected(integer.error());
          if (opcode == 0x33)
          {
            if (*array_class == "[Z")
            {
              value = Value::from_int(*integer == 0 ? 0 : 1);
            }
            else if (*array_class == "[B")
            {
              value = Value::from_int(static_cast<i32>(
                  static_cast<i8>(*integer)));
            }
            else
            {
              return fail(ErrorCode::malformed_class,
                          "baload target is not byte[] or boolean[]");
            }
          }
          else if (opcode == 0x34)
          {
            value = Value::from_int(static_cast<i32>(
                static_cast<u16>(*integer)));
          }
          else
          {
            value = Value::from_int(static_cast<i32>(
                static_cast<i16>(*integer)));
          }
        }
        auto pushed = frame.push(*value);
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x36:
      case 0x37:
      case 0x38:
      case 0x39:
      case 0x3A:
      {
        auto index = frame.read_u8();
        auto value = frame.pop();
        if (!index || !value)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid local store instruction");
        }
        if (!store_kind_matches(opcode, value->kind()))
        {
          return fail(ErrorCode::malformed_class,
                      "local store opcode does not match value kind");
        }
        auto stored = frame.set_local(*index, *value);
        if (!stored)
          return std::unexpected(stored.error());
        break;
      }
      case 0x3B:
      case 0x3C:
      case 0x3D:
      case 0x3E:
      case 0x3F:
      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43:
      case 0x44:
      case 0x45:
      case 0x46:
      case 0x47:
      case 0x48:
      case 0x49:
      case 0x4A:
      case 0x4B:
      case 0x4C:
      case 0x4D:
      case 0x4E:
      {
        auto value = frame.pop();
        if (!value)
          return std::unexpected(value.error());
        if (!store_kind_matches(opcode, value->kind()))
        {
          return fail(ErrorCode::malformed_class,
                      "fixed local store opcode does not match value kind");
        }
        auto stored = frame.set_local(local_index_for_store(opcode), *value);
        if (!stored)
          return std::unexpected(stored.error());
        break;
      }
      case 0x4F:
      case 0x50:
      case 0x51:
      case 0x52:
      case 0x53:
      case 0x54:
      case 0x55:
      case 0x56:
      {
        auto value = frame.pop();
        auto index = pop_int(frame);
        auto array = pop_reference(frame);
        if (!value || !index || !array)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid array store operands");
        }
        if (array->is_null())
        {
          auto raised = raise_implicit("java/lang/NullPointerException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto length = heap_.array_length(*array);
        if (!length)
          return std::unexpected(length.error());
        if (*index < 0 || static_cast<usize>(*index) >= *length)
        {
          auto raised = raise_implicit(
              "java/lang/ArrayIndexOutOfBoundsException", opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto array_class = heap_.class_name(*array);
        if (!array_class)
          return std::unexpected(array_class.error());
        if (!array_store_class_matches(opcode, *array_class))
        {
          return fail(ErrorCode::malformed_class,
                      "array store opcode does not match array descriptor: " +
                          *array_class);
        }
        if (opcode == 0x54 || opcode == 0x55 || opcode == 0x56)
        {
          auto integer = value->as_int();
          if (!integer)
            return std::unexpected(integer.error());
          if (opcode == 0x54)
          {
            if (*array_class == "[Z")
            {
              value = Value::from_int((*integer & 1) == 0 ? 0 : 1);
            }
            else if (*array_class == "[B")
            {
              value = Value::from_int(static_cast<i32>(
                  static_cast<i8>(*integer)));
            }
            else
            {
              return fail(ErrorCode::malformed_class,
                          "bastore target is not byte[] or boolean[]");
            }
          }
          else if (opcode == 0x55)
          {
            value = Value::from_int(static_cast<i32>(
                static_cast<u16>(*integer)));
          }
          else
          {
            value = Value::from_int(static_cast<i32>(
                static_cast<i16>(*integer)));
          }
        }
        if (opcode == 0x53)
        {
          auto stored_reference = value->as_reference();
          if (!stored_reference)
          {
            return std::unexpected(stored_reference.error());
          }
          if (!stored_reference->is_null())
          {
            std::string component_class;
            if (array_class->starts_with("[L") &&
                array_class->ends_with(';'))
            {
              component_class = array_class->substr(
                  2, array_class->size() - 3);
            }
            else if (array_class->starts_with("[["))
            {
              component_class = array_class->substr(1);
            }
            else
            {
              return fail(ErrorCode::malformed_class,
                          "aastore target is not a reference array");
            }
            auto source_class = heap_.class_name(*stored_reference);
            if (!source_class)
            {
              return std::unexpected(source_class.error());
            }
            auto assignable = classes_.is_assignable(*source_class,
                                                     component_class);
            if (!assignable)
            {
              return std::unexpected(assignable.error());
            }
            if (!*assignable)
            {
              auto raised = raise_implicit(
                  "java/lang/ArrayStoreException", opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
          }
        }
        auto stored = heap_.set_element(*array,
                                        static_cast<usize>(*index),
                                        *value);
        if (!stored)
          return std::unexpected(stored.error());
        break;
      }
      case 0x57:
      {
        auto value = frame.pop();
        if (!value)
          return std::unexpected(value.error());
        if (value->category_two())
        {
          return fail(ErrorCode::malformed_class,
                      "pop cannot consume a category-2 value");
        }
        break;
      }
      case 0x58:
      {
        auto first = frame.pop();
        if (!first)
          return std::unexpected(first.error());
        if (!first->category_two())
        {
          auto second = frame.pop();
          if (!second)
            return std::unexpected(second.error());
          if (second->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "pop2 has an invalid category layout");
          }
        }
        break;
      }
      case 0x59:
      {
        auto value = frame.pop();
        if (!value)
          return std::unexpected(value.error());
        if (value->category_two())
        {
          return fail(ErrorCode::malformed_class,
                      "dup cannot duplicate a category-2 value");
        }
        auto first = frame.push(*value);
        auto second = frame.push(*value);
        if (!first || !second)
        {
          return fail(ErrorCode::malformed_class,
                      "dup exceeds max_stack");
        }
        break;
      }
      case 0x5A:
      {
        auto value1 = frame.pop();
        auto value2 = frame.pop();
        if (!value1 || !value2 || value1->category_two() ||
            value2->category_two())
        {
          return fail(ErrorCode::malformed_class,
                      "dup_x1 requires two category-1 values");
        }
        if (!frame.push(*value1) || !frame.push(*value2) ||
            !frame.push(*value1))
        {
          return fail(ErrorCode::malformed_class,
                      "dup_x1 exceeds max_stack");
        }
        break;
      }
      case 0x5B:
      {
        auto value1 = frame.pop();
        auto value2 = frame.pop();
        if (!value1 || !value2 || value1->category_two())
        {
          return fail(ErrorCode::malformed_class,
                      "dup_x2 has an invalid category layout");
        }
        if (value2->category_two())
        {
          if (!frame.push(*value1) || !frame.push(*value2) ||
              !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup_x2 exceeds max_stack");
          }
        }
        else
        {
          auto value3 = frame.pop();
          if (!value3 || value3->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "dup_x2 has an invalid category layout");
          }
          if (!frame.push(*value1) || !frame.push(*value3) ||
              !frame.push(*value2) || !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup_x2 exceeds max_stack");
          }
        }
        break;
      }
      case 0x5C:
      {
        auto value1 = frame.pop();
        if (!value1)
          return std::unexpected(value1.error());
        if (value1->category_two())
        {
          if (!frame.push(*value1) || !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup2 exceeds max_stack");
          }
        }
        else
        {
          auto value2 = frame.pop();
          if (!value2 || value2->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "dup2 has an invalid category layout");
          }
          if (!frame.push(*value2) || !frame.push(*value1) ||
              !frame.push(*value2) || !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup2 exceeds max_stack");
          }
        }
        break;
      }
      case 0x5D:
      {
        auto value1 = frame.pop();
        if (!value1)
          return std::unexpected(value1.error());
        if (value1->category_two())
        {
          auto value2 = frame.pop();
          if (!value2 || value2->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "dup2_x1 has an invalid category layout");
          }
          if (!frame.push(*value1) || !frame.push(*value2) ||
              !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup2_x1 exceeds max_stack");
          }
        }
        else
        {
          auto value2 = frame.pop();
          auto value3 = frame.pop();
          if (!value2 || !value3 || value2->category_two() ||
              value3->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "dup2_x1 has an invalid category layout");
          }
          if (!frame.push(*value2) || !frame.push(*value1) ||
              !frame.push(*value3) || !frame.push(*value2) ||
              !frame.push(*value1))
          {
            return fail(ErrorCode::malformed_class,
                        "dup2_x1 exceeds max_stack");
          }
        }
        break;
      }
      case 0x5E:
      {
        auto value1 = frame.pop();
        if (!value1)
          return std::unexpected(value1.error());
        if (value1->category_two())
        {
          auto value2 = frame.pop();
          if (!value2)
            return std::unexpected(value2.error());
          if (value2->category_two())
          {
            auto pushed = push_values(frame, {*value1, *value2, *value1});
            if (!pushed)
              return std::unexpected(pushed.error());
          }
          else
          {
            auto value3 = frame.pop();
            if (!value3 || value3->category_two())
            {
              return fail(ErrorCode::malformed_class,
                          "dup2_x2 has an invalid category layout");
            }
            auto pushed = push_values(
                frame, {*value1, *value3, *value2, *value1});
            if (!pushed)
              return std::unexpected(pushed.error());
          }
        }
        else
        {
          auto value2 = frame.pop();
          auto value3 = frame.pop();
          if (!value2 || !value3 || value2->category_two())
          {
            return fail(ErrorCode::malformed_class,
                        "dup2_x2 has an invalid category layout");
          }
          if (value3->category_two())
          {
            auto pushed = push_values(
                frame, {*value2, *value1, *value3, *value2, *value1});
            if (!pushed)
              return std::unexpected(pushed.error());
          }
          else
          {
            auto value4 = frame.pop();
            if (!value4 || value4->category_two())
            {
              return fail(ErrorCode::malformed_class,
                          "dup2_x2 has an invalid category layout");
            }
            auto pushed = push_values(frame,
                                      {*value2, *value1, *value4, *value3, *value2, *value1});
            if (!pushed)
              return std::unexpected(pushed.error());
          }
        }
        break;
      }
      case 0x5F:
      {
        auto value1 = frame.pop();
        auto value2 = frame.pop();
        if (!value1 || !value2 || value1->category_two() ||
            value2->category_two())
        {
          return fail(ErrorCode::malformed_class,
                      "swap requires two category-1 values");
        }
        if (!frame.push(*value1) || !frame.push(*value2))
        {
          return fail(ErrorCode::malformed_class,
                      "swap exceeds max_stack");
        }
        break;
      }
      case 0x60:
      case 0x64:
      case 0x68:
      case 0x6C:
      case 0x70:
      case 0x7E:
      case 0x80:
      case 0x82:
      {
        auto result = integer_binary(frame, opcode);
        if (!result)
          return std::unexpected(result.error());
        if (!result->has_value())
        {
          auto raised = raise_implicit("java/lang/ArithmeticException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto pushed = frame.push(Value::from_int(**result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x61:
      case 0x65:
      case 0x69:
      case 0x6D:
      case 0x71:
      case 0x7F:
      case 0x81:
      case 0x83:
      {
        auto result = long_binary(frame, opcode);
        if (!result)
          return std::unexpected(result.error());
        if (!result->has_value())
        {
          auto raised = raise_implicit("java/lang/ArithmeticException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto pushed = frame.push(Value::from_long(**result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x62:
      case 0x66:
      case 0x6A:
      case 0x6E:
      case 0x72:
      {
        auto result = float_binary(frame, opcode);
        if (!result)
          return std::unexpected(result.error());
        auto pushed = frame.push(Value::from_float(*result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x63:
      case 0x67:
      case 0x6B:
      case 0x6F:
      case 0x73:
      {
        auto result = double_binary(frame, opcode);
        if (!result)
          return std::unexpected(result.error());
        auto pushed = frame.push(Value::from_double(*result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x74:
      {
        auto value = pop_int(frame);
        if (!value)
          return std::unexpected(value.error());
        auto pushed = frame.push(Value::from_int(
            static_cast<i32>(0U - static_cast<u32>(*value))));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x75:
      {
        auto value = pop_long(frame);
        if (!value)
          return std::unexpected(value.error());
        auto pushed = frame.push(Value::from_long(
            static_cast<i64>(0ULL - static_cast<u64>(*value))));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x76:
      {
        auto value = pop_float(frame);
        if (!value)
          return std::unexpected(value.error());
        auto pushed = frame.push(Value::from_float(-*value));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x77:
      {
        auto value = pop_double(frame);
        if (!value)
          return std::unexpected(value.error());
        auto pushed = frame.push(Value::from_double(-*value));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x78:
      case 0x7A:
      case 0x7C:
      {
        auto distance = pop_int(frame);
        auto value = pop_int(frame);
        if (!distance || !value)
        {
          return fail(ErrorCode::malformed_class,
                      "integer shift requires two int operands");
        }
        const u32 shift = static_cast<u32>(*distance) & 0x1FU;
        const i32 result = opcode == 0x78
                               ? static_cast<i32>(static_cast<u32>(*value) << shift)
                           : opcode == 0x7A
                               ? (*value >> shift)
                               : static_cast<i32>(static_cast<u32>(*value) >> shift);
        auto pushed = frame.push(Value::from_int(result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x79:
      case 0x7B:
      case 0x7D:
      {
        auto distance = pop_int(frame);
        auto value = pop_long(frame);
        if (!distance || !value)
        {
          return fail(ErrorCode::malformed_class,
                      "long shift requires long and int operands");
        }
        const u32 shift = static_cast<u32>(*distance) & 0x3FU;
        const i64 result = opcode == 0x79
                               ? static_cast<i64>(static_cast<u64>(*value) << shift)
                           : opcode == 0x7B
                               ? (*value >> shift)
                               : static_cast<i64>(static_cast<u64>(*value) >> shift);
        auto pushed = frame.push(Value::from_long(result));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x84:
      {
        auto index = frame.read_u8();
        auto increment = frame.read_i8();
        if (!index || !increment)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated iinc instruction");
        }
        auto current = frame.local(*index);
        if (!current)
          return std::unexpected(current.error());
        auto integer = current->as_int();
        if (!integer)
          return std::unexpected(integer.error());
        const i32 updated = static_cast<i32>(
            static_cast<u32>(*integer) +
            static_cast<u32>(static_cast<i32>(*increment)));
        auto stored = frame.set_local(*index, Value::from_int(updated));
        if (!stored)
          return std::unexpected(stored.error());
        break;
      }
      case 0x85:
      case 0x86:
      case 0x87:
      {
        auto value = pop_int(frame);
        if (!value)
          return std::unexpected(value.error());
        Status pushed;
        if (opcode == 0x85)
        {
          pushed = frame.push(Value::from_long(static_cast<i64>(*value)));
        }
        else if (opcode == 0x86)
        {
          pushed = frame.push(Value::from_float(static_cast<float>(*value)));
        }
        else
        {
          pushed = frame.push(Value::from_double(static_cast<double>(*value)));
        }
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x88:
      case 0x89:
      case 0x8A:
      {
        auto value = pop_long(frame);
        if (!value)
          return std::unexpected(value.error());
        Status pushed;
        if (opcode == 0x88)
        {
          const i32 narrowed = static_cast<i32>(
              static_cast<u32>(static_cast<u64>(*value)));
          pushed = frame.push(Value::from_int(narrowed));
        }
        else if (opcode == 0x89)
        {
          pushed = frame.push(Value::from_float(static_cast<float>(*value)));
        }
        else
        {
          pushed = frame.push(Value::from_double(static_cast<double>(*value)));
        }
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x8B:
      case 0x8C:
      case 0x8D:
      {
        auto value = pop_float(frame);
        if (!value)
          return std::unexpected(value.error());
        Status pushed;
        if (opcode == 0x8B)
        {
          pushed = frame.push(Value::from_int(
              java_fp_to_integral<i32>(*value)));
        }
        else if (opcode == 0x8C)
        {
          pushed = frame.push(Value::from_long(
              java_fp_to_integral<i64>(*value)));
        }
        else
        {
          pushed = frame.push(Value::from_double(
              static_cast<double>(*value)));
        }
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x8E:
      case 0x8F:
      case 0x90:
      {
        auto value = pop_double(frame);
        if (!value)
          return std::unexpected(value.error());
        Status pushed;
        if (opcode == 0x8E)
        {
          pushed = frame.push(Value::from_int(
              java_fp_to_integral<i32>(*value)));
        }
        else if (opcode == 0x8F)
        {
          pushed = frame.push(Value::from_long(
              java_fp_to_integral<i64>(*value)));
        }
        else
        {
          pushed = frame.push(Value::from_float(
              static_cast<float>(*value)));
        }
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x91:
      case 0x92:
      case 0x93:
      {
        auto value = pop_int(frame);
        if (!value)
          return std::unexpected(value.error());
        i32 converted = 0;
        if (opcode == 0x91)
        {
          converted = static_cast<i32>(static_cast<i8>(*value));
        }
        else if (opcode == 0x92)
        {
          converted = static_cast<i32>(static_cast<u16>(*value));
        }
        else
        {
          converted = static_cast<i32>(static_cast<i16>(*value));
        }
        auto pushed = frame.push(Value::from_int(converted));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x94:
      {
        auto right = pop_long(frame);
        auto left = pop_long(frame);
        if (!right || !left)
        {
          return fail(ErrorCode::malformed_class,
                      "lcmp requires two long operands");
        }
        const i32 comparison = *left < *right ? -1 : (*left > *right ? 1 : 0);
        auto pushed = frame.push(Value::from_int(comparison));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x95:
      case 0x96:
      {
        auto right = pop_float(frame);
        auto left = pop_float(frame);
        if (!right || !left)
        {
          return fail(ErrorCode::malformed_class,
                      "float compare requires two float operands");
        }
        i32 comparison = 0;
        if (std::isnan(*left) || std::isnan(*right))
        {
          comparison = opcode == 0x95 ? -1 : 1;
        }
        else
        {
          comparison = *left < *right ? -1 : (*left > *right ? 1 : 0);
        }
        auto pushed = frame.push(Value::from_int(comparison));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x97:
      case 0x98:
      {
        auto right = pop_double(frame);
        auto left = pop_double(frame);
        if (!right || !left)
        {
          return fail(ErrorCode::malformed_class,
                      "double compare requires two double operands");
        }
        i32 comparison = 0;
        if (std::isnan(*left) || std::isnan(*right))
        {
          comparison = opcode == 0x97 ? -1 : 1;
        }
        else
        {
          comparison = *left < *right ? -1 : (*left > *right ? 1 : 0);
        }
        auto pushed = frame.push(Value::from_int(comparison));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x99:
      case 0x9A:
      case 0x9B:
      case 0x9C:
      case 0x9D:
      case 0x9E:
      {
        auto offset = frame.read_i16();
        auto value = pop_int(frame);
        if (!offset || !value)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid zero-comparison branch");
        }
        if (test_zero(opcode, *value))
        {
          auto branched = frame.branch(opcode_pc, *offset);
          if (!branched)
            return std::unexpected(branched.error());
        }
        break;
      }
      case 0x9F:
      case 0xA0:
      case 0xA1:
      case 0xA2:
      case 0xA3:
      case 0xA4:
      {
        auto offset = frame.read_i16();
        auto right = pop_int(frame);
        auto left = pop_int(frame);
        if (!offset || !right || !left)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid integer comparison branch");
        }
        if (test_int_compare(opcode, *left, *right))
        {
          auto branched = frame.branch(opcode_pc, *offset);
          if (!branched)
            return std::unexpected(branched.error());
        }
        break;
      }
      case 0xA5:
      case 0xA6:
      {
        auto offset = frame.read_i16();
        auto right = pop_reference(frame);
        auto left = pop_reference(frame);
        if (!offset || !right || !left)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid reference comparison branch");
        }
        const bool equal = *left == *right;
        if ((opcode == 0xA5 && equal) || (opcode == 0xA6 && !equal))
        {
          auto branched = frame.branch(opcode_pc, *offset);
          if (!branched)
            return std::unexpected(branched.error());
        }
        break;
      }
      case 0xA7:
      {
        auto offset = frame.read_i16();
        if (!offset)
          return std::unexpected(offset.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xA8:
      {
        auto offset = frame.read_i16();
        if (!offset)
          return std::unexpected(offset.error());
        const usize return_pc = frame.pc();
        auto pushed = frame.push(Value::return_address(return_pc));
        if (!pushed)
          return std::unexpected(pushed.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xA9:
      {
        auto index = frame.read_u8();
        if (!index)
          return std::unexpected(index.error());
        auto address_value = frame.local(*index);
        if (!address_value)
          return std::unexpected(address_value.error());
        auto address = address_value->as_return_address();
        if (!address)
          return std::unexpected(address.error());
        auto jumped = frame.jump_absolute(*address);
        if (!jumped)
          return std::unexpected(jumped.error());
        break;
      }
      case 0xAA:
      {
        auto key = pop_int(frame);
        if (!key)
          return std::unexpected(key.error());
        auto padding = frame.read_switch_padding();
        if (!padding)
          return std::unexpected(padding.error());
        auto default_offset = frame.read_i32();
        auto low = frame.read_i32();
        auto high = frame.read_i32();
        if (!default_offset || !low || !high)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated tableswitch header");
        }
        if (*high < *low)
        {
          return fail(ErrorCode::malformed_class,
                      "tableswitch high key is below low key");
        }
        const u64 entry_count = static_cast<u64>(
            static_cast<i64>(*high) - static_cast<i64>(*low) + 1);
        if (entry_count > static_cast<u64>(frame.code_size() / 4U))
        {
          return fail(ErrorCode::malformed_class,
                      "tableswitch entry count exceeds method bounds");
        }
        i32 selected_offset = *default_offset;
        for (u64 entry = 0; entry < entry_count; ++entry)
        {
          auto offset = frame.read_i32();
          if (!offset)
            return std::unexpected(offset.error());
          const i64 match_key = static_cast<i64>(*low) +
                                static_cast<i64>(entry);
          if (static_cast<i64>(*key) == match_key)
          {
            selected_offset = *offset;
          }
        }
        auto branched = frame.branch(opcode_pc, selected_offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xAB:
      {
        auto key = pop_int(frame);
        if (!key)
          return std::unexpected(key.error());
        auto padding = frame.read_switch_padding();
        if (!padding)
          return std::unexpected(padding.error());
        auto default_offset = frame.read_i32();
        auto pair_count = frame.read_i32();
        if (!default_offset || !pair_count || *pair_count < 0)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid lookupswitch header");
        }
        const u64 count = static_cast<u64>(*pair_count);
        if (count > static_cast<u64>(frame.code_size() / 8U))
        {
          return fail(ErrorCode::malformed_class,
                      "lookupswitch pair count exceeds method bounds");
        }
        i32 selected_offset = *default_offset;
        std::optional<i32> previous_match;
        for (u64 pair = 0; pair < count; ++pair)
        {
          auto match = frame.read_i32();
          auto offset = frame.read_i32();
          if (!match || !offset)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated lookupswitch pair");
          }
          if (previous_match.has_value() &&
              *match <= *previous_match)
          {
            return fail(ErrorCode::malformed_class,
                        "lookupswitch keys are not strictly ordered");
          }
          previous_match = *match;
          if (*key == *match)
          {
            selected_offset = *offset;
          }
        }
        auto branched = frame.branch(opcode_pc, selected_offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xAC:
      case 0xAD:
      case 0xAE:
      case 0xAF:
      case 0xB0:
      {
        auto value = frame.pop();
        if (!value)
          return std::unexpected(value.error());
        const JavaTypeKind return_kind = frame.descriptor().return_type.kind;
        const bool matches =
            (opcode == 0xAC && value->kind() == ValueKind::int32 &&
             return_kind != JavaTypeKind::void_type) ||
            (opcode == 0xAD && value->kind() == ValueKind::int64 &&
             return_kind == JavaTypeKind::long_integer) ||
            (opcode == 0xAE && value->kind() == ValueKind::float32 &&
             return_kind == JavaTypeKind::float32) ||
            (opcode == 0xAF && value->kind() == ValueKind::float64 &&
             return_kind == JavaTypeKind::float64) ||
            (opcode == 0xB0 && value->kind() == ValueKind::reference &&
             frame.descriptor().return_type.reference_like());
        if (!matches)
        {
          return fail(ErrorCode::malformed_class,
                      "return opcode does not match method descriptor");
        }
        auto completed = complete_return(*value);
        if (!completed)
          return std::unexpected(completed.error());
        if (completed->has_value())
          return std::move(**completed);
        break;
      }
      case 0xB1:
      {
        if (frame.descriptor().return_type.kind != JavaTypeKind::void_type)
        {
          return fail(ErrorCode::malformed_class,
                      "void return used by a non-void method");
        }
        auto completed = complete_return(std::nullopt);
        if (!completed)
          return std::unexpected(completed.error());
        if (completed->has_value())
          return std::move(**completed);
        break;
      }
      case 0xB2:
      case 0xB3:
      case 0xB4:
      case 0xB5:
      {
        auto index = frame.read_u16();
        if (!index)
          return std::unexpected(index.error());
        auto reference = frame.owner().member_reference(*index);
        if (!reference)
          return std::unexpected(reference.error());
        if (reference->kind != classfile::ConstantKind::field_ref)
        {
          return fail(ErrorCode::malformed_class,
                      "field opcode references a non-field constant");
        }
        const bool is_static = opcode == 0xB2 || opcode == 0xB3;
        auto field = states_.resolve_field(reference->owner,
                                           reference->name,
                                           reference->descriptor,
                                           is_static);
        if (!field)
          return std::unexpected(field.error());
        if (is_static)
        {
          auto initialized = ensure_initialized(
              field->declaring_class,
              instruction_budget - executed);
          if (!initialized)
            return std::unexpected(initialized.error());
          if (initialized->has_value())
          {
            auto dispatched = dispatch_exception(**initialized,
                                                 opcode_pc);
            if (!dispatched)
            {
              return std::unexpected(dispatched.error());
            }
            if (dispatched->has_value())
            {
              return std::move(**dispatched);
            }
            break;
          }
        }

        if (opcode == 0xB2)
        {
          auto value = states_.static_field(*field);
          if (!value)
            return std::unexpected(value.error());
          if (field->constant_value_index.has_value() &&
              field->descriptor == "Ljava/lang/String;")
          {
            auto current = value->as_reference();
            if (!current)
              return std::unexpected(current.error());
            if (current->is_null())
            {
              auto declaring_class = classes_.load(field->declaring_class);
              if (!declaring_class)
                return std::unexpected(declaring_class.error());
              auto encoded = (*declaring_class)->string_constant(
                  *field->constant_value_index);
              if (!encoded)
                return std::unexpected(encoded.error());
              auto string = intern_string(*encoded);
              if (!string)
                return std::unexpected(string.error());
              value = Value::from_reference(*string);
              auto stored = states_.set_static_field(*field, *value);
              if (!stored)
                return std::unexpected(stored.error());
            }
          }
          auto pushed = frame.push(*value);
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        else if (opcode == 0xB3)
        {
          auto value = frame.pop();
          if (!value)
            return std::unexpected(value.error());
          auto stored = states_.set_static_field(*field, *value);
          if (!stored)
            return std::unexpected(stored.error());
        }
        else if (opcode == 0xB4)
        {
          auto object = pop_reference(frame);
          if (!object)
            return std::unexpected(object.error());
          if (object->is_null())
          {
            auto raised = raise_implicit("java/lang/NullPointerException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto value = heap_.field(*object, field->index);
          if (!value)
            return std::unexpected(value.error());
          auto pushed = frame.push(*value);
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        else
        {
          auto value = frame.pop();
          auto object = pop_reference(frame);
          if (!value || !object)
          {
            return fail(ErrorCode::malformed_class,
                        "invalid putfield operands");
          }
          if (object->is_null())
          {
            auto raised = raise_implicit("java/lang/NullPointerException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto stored = heap_.set_field(*object, field->index, *value);
          if (!stored)
            return std::unexpected(stored.error());
        }
        break;
      }
      case 0xB6:
      case 0xB7:
      case 0xB8:
      case 0xB9:
      {
        auto index = frame.read_u16();
        if (!index)
          return std::unexpected(index.error());
        std::optional<u8> interface_count;
        if (opcode == 0xB9)
        {
          auto count = frame.read_u8();
          auto zero = frame.read_u8();
          if (!count || !zero || *count == 0U || *zero != 0U)
          {
            return fail(ErrorCode::malformed_class,
                        "invalid invokeinterface operands");
          }
          interface_count = *count;
        }
        auto reference = frame.owner().member_reference(*index);
        if (!reference)
          return std::unexpected(reference.error());
        if ((opcode == 0xB6 &&
             reference->kind != classfile::ConstantKind::method_ref) ||
            (opcode == 0xB9 &&
             reference->kind !=
                 classfile::ConstantKind::interface_method_ref) ||
            ((opcode == 0xB7 || opcode == 0xB8) &&
             reference->kind != classfile::ConstantKind::method_ref &&
             reference->kind !=
                 classfile::ConstantKind::interface_method_ref))
        {
          return fail(ErrorCode::malformed_class,
                      "invoke opcode uses an incompatible constant kind");
        }
        const bool is_static = opcode == 0xB8;
        auto descriptor = parse_method_descriptor(reference->descriptor);
        if (!descriptor)
          return std::unexpected(descriptor.error());
        if (interface_count.has_value())
        {
          const usize expected_slots = descriptor->parameter_slots(true);
          if (expected_slots >
                  static_cast<usize>(std::numeric_limits<u8>::max()) ||
              *interface_count != static_cast<u8>(expected_slots))
          {
            return fail(ErrorCode::malformed_class,
                        "invokeinterface count does not match descriptor");
          }
        }
        auto arguments = pop_arguments(frame, *descriptor, !is_static);
        if (!arguments)
          return std::unexpected(arguments.error());
        if (!is_static)
        {
          auto receiver = arguments->front().as_reference();
          if (!receiver)
            return std::unexpected(receiver.error());
          if (receiver->is_null())
          {
            auto raised = raise_implicit("java/lang/NullPointerException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
        }

        if (is_static)
        {
          auto initialized = ensure_initialized(
              reference->owner,
              instruction_budget - executed);
          if (!initialized)
            return std::unexpected(initialized.error());
          if (initialized->has_value())
          {
            auto dispatched = dispatch_exception(**initialized,
                                                 opcode_pc);
            if (!dispatched)
            {
              return std::unexpected(dispatched.error());
            }
            if (dispatched->has_value())
            {
              return std::move(**dispatched);
            }
            break;
          }
        }

        std::optional<Invocation> nested;
        const bool is_clone_intrinsic =
            !is_static &&
            (opcode == 0xB6 || opcode == 0xB7) &&
            reference->name == "clone" &&
            reference->descriptor == "()Ljava/lang/Object;" &&
            (reference->owner == "java/lang/Object" ||
             reference->owner.starts_with('['));
        if (is_clone_intrinsic)
        {
          auto source = arguments->front().as_reference();
          if (!source)
            return std::unexpected(source.error());
          auto source_class = heap_.class_name(*source);
          if (!source_class)
            return std::unexpected(source_class.error());
          if (!source_class->starts_with('['))
          {
            auto cloneable = classes_.is_assignable(
                *source_class, "java/lang/Cloneable");
            if (!cloneable)
              return std::unexpected(cloneable.error());
            if (!*cloneable)
            {
              auto raised = raise_implicit(
                  "java/lang/CloneNotSupportedException", opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
          }
          auto clone = heap_.clone_object(*source);
          if (!clone && clone.error().code == ErrorCode::overflow)
          {
            auto collected = collect_active_garbage(*source);
            if (!collected)
              return std::unexpected(collected.error());
            clone = heap_.clone_object(*source);
          }
          if (!clone)
          {
            if (clone.error().code == ErrorCode::overflow)
            {
              auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                           opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(clone.error());
          }
          auto pushed = frame.push(Value::from_reference(*clone));
          if (!pushed)
            return std::unexpected(pushed.error());
          break;
        }
        const bool is_reflective_constructor =
            !is_static && opcode == 0xB6 &&
            reference->owner == "java/lang/Class" &&
            reference->name == "newInstance" &&
            reference->descriptor == "()Ljava/lang/Object;";
        if (is_reflective_constructor)
        {
          auto mirror = arguments->front().as_reference();
          if (!mirror)
            return std::unexpected(mirror.error());
          auto class_name = mirrored_class_name(*mirror);
          if (!class_name)
            return std::unexpected(class_name.error());
          const bool primitive = class_name->size() == 1U &&
              std::string_view("ZBCSIJFDV").find(class_name->front()) !=
                  std::string_view::npos;
          if (primitive || class_name->starts_with('['))
          {
            auto raised = raise_implicit("java/lang/InstantiationException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto loaded = classes_.load(*class_name);
          if (!loaded)
          {
            if (loaded.error().code == ErrorCode::class_not_found)
            {
              auto raised = raise_implicit(
                  "java/lang/InstantiationException", opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(loaded.error());
          }
          if (((*loaded)->access_flags() &
               (kAccInterface | kAccAbstract)) != 0U)
          {
            auto raised = raise_implicit("java/lang/InstantiationException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto initialized = ensure_initialized(
              *class_name, instruction_budget - executed);
          if (!initialized)
            return std::unexpected(initialized.error());
          if (initialized->has_value())
          {
            auto dispatched = dispatch_exception(**initialized, opcode_pc);
            if (!dispatched)
              return std::unexpected(dispatched.error());
            if (dispatched->has_value())
              return std::move(**dispatched);
            break;
          }
          auto constructor = classes_.resolve_declared_method(
              *class_name, "<init>", "()V");
          if (!constructor)
          {
            if (constructor.error().code == ErrorCode::method_not_found)
            {
              auto raised = raise_implicit(
                  "java/lang/InstantiationException", opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(constructor.error());
          }
          const auto package_name = [](std::string_view name) {
            const usize separator = name.rfind('/');
            return separator == std::string_view::npos
                ? std::string_view {}
                : name.substr(0U, separator);
          };
          const u16 constructor_flags = constructor->method->access_flags;
          const bool legacy_same_package_access =
              (*loaded)->major_version() <= 50U &&
              (constructor_flags & kAccPrivate) == 0U &&
              package_name(frame.owner().name()) == package_name(*class_name);
          if ((constructor_flags & kAccPublic) == 0U &&
              !legacy_same_package_access)
          {
            auto raised = raise_implicit("java/lang/IllegalAccessException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto object = allocate_instance_with_gc(*class_name);
          if (!object)
          {
            if (object.error().code == ErrorCode::overflow)
            {
              auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                           opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(object.error());
          }
          std::vector<Value> constructor_arguments {
              Value::from_reference(*object),
          };
          auto reflective_invocation = prepare_invocation(
              std::move(*constructor), constructor_arguments, true);
          if (!reflective_invocation)
            return std::unexpected(reflective_invocation.error());
          reflective_invocation->return_override =
              Value::from_reference(*object);
          nested = std::move(*reflective_invocation);
        }
        if (!nested.has_value() && !is_static && opcode == 0xB9)
        {
          auto receiver = arguments->front().as_reference();
          if (!receiver)
            return std::unexpected(receiver.error());
          const auto lambda = lambda_bindings_.find(receiver->bits);
          const bool lambda_descriptor_matches =
              lambda != lambda_bindings_.end() &&
              (lambda->second.sam_descriptor == reference->descriptor ||
               std::find(lambda->second.bridge_descriptors.begin(),
                         lambda->second.bridge_descriptors.end(),
                         reference->descriptor) !=
                   lambda->second.bridge_descriptors.end());
          if (lambda != lambda_bindings_.end() &&
              lambda->second.interface_name == reference->owner &&
              lambda->second.sam_name == reference->name &&
              lambda_descriptor_matches)
          {
            std::optional<ObjectRef> constructor_receiver;
            if (lambda->second.implementation_kind == 8U)
            {
              auto allocated = allocate_instance_with_gc(
                  lambda->second.implementation.owner);
              if (!allocated)
              {
                if (allocated.error().code == ErrorCode::overflow)
                {
                  auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                               opcode_pc);
                  if (!raised)
                    return std::unexpected(raised.error());
                  if (raised->has_value())
                    return std::move(**raised);
                  break;
                }
                return std::unexpected(allocated.error());
              }
              constructor_receiver = *allocated;
            }
            auto lambda_invocation = prepare_lambda_invocation(
                *receiver,
                lambda->second,
                std::span<const Value>(arguments->data() + 1,
                                       arguments->size() - 1U),
                constructor_receiver);
            if (!lambda_invocation)
              return std::unexpected(lambda_invocation.error());
            nested = std::move(*lambda_invocation);
          }
        }

        if (!nested.has_value())
        {
          std::string dispatch_class = reference->owner;
          if (!is_static && opcode != 0xB7)
          {
            auto receiver = arguments->front().as_reference();
            if (!receiver)
              return std::unexpected(receiver.error());
            auto runtime_class = heap_.class_name(*receiver);
            if (!runtime_class)
              return std::unexpected(runtime_class.error());
            if (opcode == 0xB9)
            {
              auto implements_interface = classes_.is_assignable(
                  *runtime_class,
                  reference->owner);
              if (!implements_interface)
              {
                return std::unexpected(implements_interface.error());
              }
              if (!*implements_interface)
              {
                auto raised = raise_implicit(
                    "java/lang/IncompatibleClassChangeError",
                    opcode_pc);
                if (!raised)
                  return std::unexpected(raised.error());
                if (raised->has_value())
                  return std::move(**raised);
                break;
              }
            }
            dispatch_class = *runtime_class;
          }
          Result<ResolvedMethod> target =
              opcode == 0xB7 && reference->name == "<init>"
                  ? classes_.resolve_declared_method(reference->owner,
                                                     reference->name,
                                                     reference->descriptor)
                  : classes_.resolve_method(dispatch_class,
                                            reference->name,
                                            reference->descriptor);
          if (!target)
            return std::unexpected(target.error());
          if (((target->method->access_flags & kAccStatic) != 0) != is_static)
          {
            auto raised = raise_implicit(
                "java/lang/IncompatibleClassChangeError", opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          if ((target->method->access_flags & kAccAbstract) != 0U)
          {
            auto raised = raise_implicit("java/lang/AbstractMethodError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          nested = Invocation{
              .method = std::move(*target),
              .arguments = std::move(*arguments),
              .has_receiver = !is_static,
          };
        }
        const bool nested_native_override =
            nested->method.owner != nullptr &&
            natives_.contains(nested->method.owner->name(),
                              nested->method.method->name,
                              nested->method.method->descriptor);
        const bool nested_is_native = nested_native_override ||
                                      !nested->method.method->code.has_value();
        if (!nested_is_native && frames.size() >= kMaximumCallDepth)
        {
          auto raised = raise_implicit("java/lang/StackOverflowError",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        std::vector<ObjectRef> native_safepoint_roots;
        native_safepoint_roots.reserve(frames.size() * 8U +
                                       nested->arguments.size());
        for (const ExecutionFrame& active_frame : frames)
          active_frame.append_reference_roots(native_safepoint_roots);
        for (const Value argument : nested->arguments)
        {
          if (argument.kind() != ValueKind::reference)
            continue;
          auto reference_value = argument.as_reference();
          if (reference_value && !reference_value->is_null())
            native_safepoint_roots.push_back(*reference_value);
        }
        publish_execution_roots(invocation_depth,
                                native_safepoint_roots);

        auto nested_monitor = acquire_synchronized_monitor(*nested);
        if (!nested_monitor)
        {
          return std::unexpected(nested_monitor.error());
        }
        if (nested_is_native)
        {
          auto native_result = invoke_native(*nested);
          auto released = release_synchronized_monitor(*nested_monitor);
          if (!released)
            return std::unexpected(released.error());
          if (!native_result)
          {
            if (native_result.error().code == ErrorCode::java_exception)
            {
              if (native_result.error().java_exception_class.empty())
              {
                return fail(ErrorCode::internal_error,
                            "native Java exception has no class name");
              }
              auto raised = raise_implicit(
                  native_result.error().java_exception_class,
                  opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            if (native_result.error().code ==
                    ErrorCode::unsupported_feature &&
                (nested->method.method->access_flags & kAccNative) != 0U)
            {
              auto raised = raise_implicit(
                  "java/lang/UnsatisfiedLinkError", opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(native_result.error());
          }
          std::optional<Value> nested_return = *native_result;
          if (!nested_return.has_value() &&
              nested->return_override.has_value())
          {
            nested_return = nested->return_override;
          }
          if (nested_return.has_value())
          {
            auto pushed = frame.push(*nested_return);
            if (!pushed)
              return std::unexpected(pushed.error());
          }
        }
        else
        {
          auto nested_descriptor = parse_method_descriptor(
              nested->method.method->descriptor);
          if (!nested_descriptor)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return std::unexpected(nested_descriptor.error());
          }
          auto next = ExecutionFrame::make(std::move(nested->method),
                                           std::move(*nested_descriptor),
                                           nested->arguments,
                                           nested->has_receiver);
          if (!next)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return std::unexpected(next.error());
          }
          if (nested_monitor->has_value())
          {
            next->set_synchronized_monitor(**nested_monitor);
          }
          if (nested->return_override.has_value())
          {
            next->set_return_override(*nested->return_override);
          }
          frames.push_back(std::move(*next));
        }
        break;
      }
      case 0xBA:
      {
        auto index = frame.read_u16();
        auto zero1 = frame.read_u8();
        auto zero2 = frame.read_u8();
        if (!index || !zero1 || !zero2 || *zero1 != 0U || *zero2 != 0U)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid invokedynamic operands");
        }
        auto dynamic = frame.owner().invoke_dynamic_reference(*index);
        if (!dynamic)
          return std::unexpected(dynamic.error());
        auto call_site = parse_method_descriptor(dynamic->descriptor);
        if (!call_site)
          return std::unexpected(call_site.error());
        auto captures = pop_arguments(frame, *call_site, false);
        if (!captures)
          return std::unexpected(captures.error());
        auto binding = resolve_lambda_binding(frame.owner(), *index);
        if (!binding)
        {
          if (binding.error().code == ErrorCode::unsupported_feature)
          {
            auto raised = raise_implicit("java/lang/BootstrapMethodError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(binding.error());
        }
        auto lambda = allocate_raw_object_with_gc(binding->interface_name,
                                                  captures->size());
        if (!lambda)
        {
          if (lambda.error().code == ErrorCode::overflow)
          {
            auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(lambda.error());
        }
        for (usize capture_index = 0;
             capture_index < captures->size();
             ++capture_index)
        {
          auto stored = heap_.set_field(*lambda,
                                        capture_index,
                                        (*captures)[capture_index]);
          if (!stored)
            return std::unexpected(stored.error());
        }
        lambda_bindings_.insert_or_assign(lambda->bits, std::move(*binding));
        auto pushed = frame.push(Value::from_reference(*lambda));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xBB:
      {
        auto index = frame.read_u16();
        if (!index)
          return std::unexpected(index.error());
        auto class_name = frame.owner().class_name_constant(*index);
        if (!class_name)
          return std::unexpected(class_name.error());
        auto allocated_class = classes_.load(*class_name);
        if (!allocated_class)
        {
          return std::unexpected(allocated_class.error());
        }
        if (((*allocated_class)->access_flags() &
             (kAccInterface | kAccAbstract)) != 0U)
        {
          auto raised = raise_implicit("java/lang/InstantiationError",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto initialized = ensure_initialized(*class_name,
                                              instruction_budget - executed);
        if (!initialized)
          return std::unexpected(initialized.error());
        if (initialized->has_value())
        {
          auto dispatched = dispatch_exception(**initialized, opcode_pc);
          if (!dispatched)
            return std::unexpected(dispatched.error());
          if (dispatched->has_value())
            return std::move(**dispatched);
          break;
        }
        auto object = allocate_instance_with_gc(*class_name);
        if (!object)
        {
          if (object.error().code == ErrorCode::overflow)
          {
            auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(object.error());
        }
        auto pushed = frame.push(Value::from_reference(*object));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xBC:
      {
        auto atype = frame.read_u8();
        auto count = pop_int(frame);
        if (!atype || !count)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid newarray operands");
        }
        if (*count < 0)
        {
          auto raised = raise_implicit(
              "java/lang/NegativeArraySizeException", opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto type = primitive_array_type(*atype);
        if (!type)
          return std::unexpected(type.error());
        auto array = allocate_array_with_gc(type->first,
                                            static_cast<usize>(*count),
                                            type->second);
        if (!array)
        {
          if (array.error().code == ErrorCode::overflow)
          {
            auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(array.error());
        }
        auto pushed = frame.push(Value::from_reference(*array));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xBD:
      {
        auto index = frame.read_u16();
        auto count = pop_int(frame);
        if (!index || !count)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid anewarray operands");
        }
        if (*count < 0)
        {
          auto raised = raise_implicit(
              "java/lang/NegativeArraySizeException", opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto component = frame.owner().class_name_constant(*index);
        if (!component)
          return std::unexpected(component.error());
        std::string array_name;
        if (component->starts_with('['))
        {
          array_name = '[' + *component;
        }
        else
        {
          array_name = "[L" + *component + ';';
        }
        auto array = allocate_array_with_gc(array_name,
                                            static_cast<usize>(*count),
                                            Value::from_reference({}));
        if (!array)
        {
          if (array.error().code == ErrorCode::overflow)
          {
            auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(array.error());
        }
        auto pushed = frame.push(Value::from_reference(*array));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xBE:
      {
        auto array = pop_reference(frame);
        if (!array)
          return std::unexpected(array.error());
        if (array->is_null())
        {
          auto raised = raise_implicit("java/lang/NullPointerException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto length = heap_.array_length(*array);
        if (!length)
          return std::unexpected(length.error());
        if (*length > static_cast<usize>(std::numeric_limits<i32>::max()))
        {
          return fail(ErrorCode::overflow,
                      "Java array length exceeds int range");
        }
        auto pushed = frame.push(
            Value::from_int(static_cast<i32>(*length)));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xBF:
      {
        auto throwable = pop_reference(frame);
        if (!throwable)
          return std::unexpected(throwable.error());
        if (throwable->is_null())
        {
          auto raised = raise_implicit("java/lang/NullPointerException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto dispatched = dispatch_exception(*throwable, opcode_pc);
        if (!dispatched)
          return std::unexpected(dispatched.error());
        if (dispatched->has_value())
          return std::move(**dispatched);
        break;
      }
      case 0xC0:
      case 0xC1:
      {
        auto index = frame.read_u16();
        auto reference = pop_reference(frame);
        if (!index || !reference)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid type-check operands");
        }
        auto target_class = frame.owner().class_name_constant(*index);
        if (!target_class)
          return std::unexpected(target_class.error());

        if (reference->is_null())
        {
          auto pushed = frame.push(opcode == 0xC0
                                       ? Value::from_reference(*reference)
                                       : Value::from_int(0));
          if (!pushed)
            return std::unexpected(pushed.error());
          break;
        }

        auto source_class = heap_.class_name(*reference);
        if (!source_class)
          return std::unexpected(source_class.error());
        auto assignable = classes_.is_assignable(*source_class,
                                                 *target_class);
        if (!assignable)
          return std::unexpected(assignable.error());
        bool is_assignable = *assignable;
        if (!is_assignable)
        {
          const auto lambda = lambda_bindings_.find(reference->bits);
          if (lambda != lambda_bindings_.end())
          {
            is_assignable = std::find(
                                lambda->second.marker_interfaces.begin(),
                                lambda->second.marker_interfaces.end(),
                                *target_class) !=
                            lambda->second.marker_interfaces.end();
          }
        }
        if (opcode == 0xC0)
        {
          if (!is_assignable)
          {
            auto raised = raise_implicit("java/lang/ClassCastException",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          auto pushed = frame.push(Value::from_reference(*reference));
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        else
        {
          auto pushed = frame.push(Value::from_int(is_assignable ? 1 : 0));
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        break;
      }
      case 0xC2:
      case 0xC3:
      {
        auto reference = pop_reference(frame);
        if (!reference)
          return std::unexpected(reference.error());
        if (reference->is_null())
        {
          auto raised = raise_implicit("java/lang/NullPointerException",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        if (opcode == 0xC2)
        {
          std::vector<ObjectRef> monitor_roots;
          monitor_roots.reserve(frames.size() * 8U + 1U);
          for (const ExecutionFrame& active_frame : frames)
            active_frame.append_reference_roots(monitor_roots);
          monitor_roots.push_back(*reference);
          publish_execution_roots(invocation_depth, monitor_roots);
          auto entered = enter_monitor(*reference);
          if (!entered)
            return std::unexpected(entered.error());
        }
        else
        {
          auto exited = monitors_.exit(*reference,
                                       scheduler_.current_thread_id());
          if (!exited)
          {
            if (exited.error().code == ErrorCode::java_exception)
            {
              auto raised = raise_implicit(
                  exited.error().java_exception_class,
                  opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            return std::unexpected(exited.error());
          }
        }
        break;
      }
      case 0xC4:
      {
        auto widened_opcode = frame.read_u8();
        auto index = frame.read_u16();
        if (!widened_opcode || !index)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated wide instruction");
        }
        if (*widened_opcode >= 0x15 && *widened_opcode <= 0x19)
        {
          auto value = frame.local(*index);
          if (!value)
            return std::unexpected(value.error());
          if (!load_kind_matches(*widened_opcode, value->kind()))
          {
            return fail(ErrorCode::malformed_class,
                        "wide load does not match value kind");
          }
          auto pushed = frame.push(*value);
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        else if (*widened_opcode >= 0x36 && *widened_opcode <= 0x3A)
        {
          auto value = frame.pop();
          if (!value)
            return std::unexpected(value.error());
          if (!store_kind_matches(*widened_opcode, value->kind()))
          {
            return fail(ErrorCode::malformed_class,
                        "wide store does not match value kind");
          }
          auto stored = frame.set_local(*index, *value);
          if (!stored)
            return std::unexpected(stored.error());
        }
        else if (*widened_opcode == 0x84)
        {
          auto increment = frame.read_i16();
          if (!increment)
            return std::unexpected(increment.error());
          auto current = frame.local(*index);
          if (!current)
            return std::unexpected(current.error());
          auto integer = current->as_int();
          if (!integer)
            return std::unexpected(integer.error());
          const i32 updated = static_cast<i32>(
              static_cast<u32>(*integer) +
              static_cast<u32>(static_cast<i32>(*increment)));
          auto stored = frame.set_local(*index, Value::from_int(updated));
          if (!stored)
            return std::unexpected(stored.error());
        }
        else if (*widened_opcode == 0xA9)
        {
          auto address_value = frame.local(*index);
          if (!address_value)
          {
            return std::unexpected(address_value.error());
          }
          auto address = address_value->as_return_address();
          if (!address)
            return std::unexpected(address.error());
          auto jumped = frame.jump_absolute(*address);
          if (!jumped)
            return std::unexpected(jumped.error());
        }
        else
        {
          return fail(ErrorCode::unsupported_feature,
                      "wide opcode is not ported yet");
        }
        break;
      }
      case 0xC5:
      {
        auto index = frame.read_u16();
        auto dimensions = frame.read_u8();
        if (!index || !dimensions || *dimensions == 0U)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid multianewarray operands");
        }
        auto array_class = frame.owner().class_name_constant(*index);
        if (!array_class)
          return std::unexpected(array_class.error());
        usize declared_dimensions = 0;
        while (declared_dimensions < array_class->size() &&
               (*array_class)[declared_dimensions] == '[')
        {
          ++declared_dimensions;
        }
        if (declared_dimensions == 0U ||
            static_cast<usize>(*dimensions) > declared_dimensions)
        {
          return fail(ErrorCode::malformed_class,
                      "multianewarray dimensions exceed descriptor rank");
        }

        std::vector<i32> lengths(*dimensions);
        for (usize reverse = lengths.size(); reverse > 0; --reverse)
        {
          auto length = pop_int(frame);
          if (!length)
            return std::unexpected(length.error());
          lengths[reverse - 1U] = *length;
        }
        bool has_negative_length = false;
        for (const i32 length : lengths)
        {
          if (length < 0)
          {
            has_negative_length = true;
            break;
          }
        }
        if (has_negative_length)
        {
          auto raised = raise_implicit(
              "java/lang/NegativeArraySizeException", opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }

        usize arrays_at_level = 1U;
        usize total_arrays = 0U;
        bool exceeds_array_budget = false;
        for (usize level = 0; level < lengths.size(); ++level)
        {
          auto updated_total = checked_add(total_arrays,
                                           arrays_at_level);
          if (!updated_total || *updated_total > 1'000'000U)
          {
            exceeds_array_budget = true;
            break;
          }
          total_arrays = *updated_total;
          if (level + 1U < lengths.size())
          {
            auto next_count = checked_multiply(
                arrays_at_level,
                static_cast<usize>(lengths[level]));
            if (!next_count || *next_count > 1'000'000U)
            {
              exceeds_array_budget = true;
              break;
            }
            arrays_at_level = *next_count;
          }
        }
        if (exceeds_array_budget)
        {
          auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }

        auto root_default = array_default_value(*array_class);
        if (!root_default)
          return std::unexpected(root_default.error());
        auto root = allocate_array_with_gc(
            *array_class,
            static_cast<usize>(lengths.front()),
            *root_default);
        if (!root)
        {
          if (root.error().code == ErrorCode::overflow)
          {
            auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                         opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          return std::unexpected(root.error());
        }

        struct PendingArray final
        {
          ObjectRef reference;
          usize level{0};
        };
        std::vector<PendingArray> pending;
        pending.reserve(total_arrays);
        pending.push_back(PendingArray{.reference = *root, .level = 0});
        bool allocation_failed = false;
        for (usize cursor = 0; cursor < pending.size(); ++cursor)
        {
          const PendingArray current = pending[cursor];
          const usize child_level = current.level + 1U;
          if (child_level >= lengths.size())
          {
            continue;
          }
          const std::string child_descriptor =
              array_class->substr(child_level);
          const usize child_count =
              static_cast<usize>(lengths[child_level]);
          auto child_default = array_default_value(child_descriptor);
          if (!child_default)
            return std::unexpected(child_default.error());
          const usize parent_length =
              static_cast<usize>(lengths[current.level]);
          for (usize element = 0; element < parent_length; ++element)
          {
            auto child = allocate_array_with_gc(
                child_descriptor,
                child_count,
                *child_default,
                *root);
            if (!child)
            {
              if (child.error().code == ErrorCode::overflow)
              {
                allocation_failed = true;
                break;
              }
              return std::unexpected(child.error());
            }
            auto stored = heap_.set_element(
                current.reference,
                element,
                Value::from_reference(*child));
            if (!stored)
              return std::unexpected(stored.error());
            pending.push_back(PendingArray{
                .reference = *child,
                .level = child_level,
            });
          }
          if (allocation_failed)
          {
            break;
          }
        }
        if (allocation_failed)
        {
          auto raised = raise_implicit("java/lang/OutOfMemoryError",
                                       opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        auto pushed = frame.push(Value::from_reference(*root));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0xC6:
      case 0xC7:
      {
        auto offset = frame.read_i16();
        auto reference = pop_reference(frame);
        if (!offset || !reference)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid null-comparison branch");
        }
        const bool branch = opcode == 0xC6
                                ? reference->is_null()
                                : !reference->is_null();
        if (branch)
        {
          auto branched = frame.branch(opcode_pc, *offset);
          if (!branched)
            return std::unexpected(branched.error());
        }
        break;
      }
      case 0xC8:
      {
        auto offset = frame.read_i32();
        if (!offset)
          return std::unexpected(offset.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xC9:
      {
        auto offset = frame.read_i32();
        if (!offset)
          return std::unexpected(offset.error());
        const usize return_pc = frame.pc();
        auto pushed = frame.push(Value::return_address(return_pc));
        if (!pushed)
          return std::unexpected(pushed.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      default:
        return fail(ErrorCode::unsupported_feature,
                    "VM opcode is not ported yet: " +
                        std::to_string(opcode));
      }
    }

    return fail(ErrorCode::internal_error,
                "VM call stack terminated without a result");
  }

} // namespace phoneme::vm
