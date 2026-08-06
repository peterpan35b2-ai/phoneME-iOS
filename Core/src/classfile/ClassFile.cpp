#include "phoneme/classfile/ClassFile.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "phoneme/base/ByteReader.hpp"
#include "phoneme/base/Checked.hpp"
#include "phoneme/classfile/BytecodeVerifier.hpp"

namespace phoneme::classfile
{
  namespace
  {

    constexpr u32 kClassMagic = 0xCAFEBABEU;
    constexpr u16 kMinimumSupportedMajorVersion = 45;
    constexpr u16 kMaximumParsedMajorVersion = 65;

    class ConstantPool final
    {
    public:
      explicit ConstantPool(usize count) : entries_(count) {}

      [[nodiscard]] Constant &at(usize index) { return entries_.at(index); }

      [[nodiscard]] Result<const Constant *> get(u16 index) const
      {
        if (index == 0 || static_cast<usize>(index) >= entries_.size())
        {
          return fail(ErrorCode::malformed_class, "constant-pool index is out of range");
        }
        return &entries_[index];
      }

      [[nodiscard]] Result<std::string> utf8(u16 index) const
      {
        auto entry = get(index);
        if (!entry)
        {
          return std::unexpected(entry.error());
        }
        if ((*entry)->kind != ConstantKind::utf8)
        {
          return fail(ErrorCode::malformed_class,
                      "constant-pool entry is not a UTF-8 string");
        }
        return (*entry)->utf8;
      }

      [[nodiscard]] Result<std::string> class_name(u16 index) const
      {
        auto entry = get(index);
        if (!entry)
        {
          return std::unexpected(entry.error());
        }
        if ((*entry)->kind != ConstantKind::class_ref)
        {
          return fail(ErrorCode::malformed_class,
                      "constant-pool entry is not a class reference");
        }
        return utf8((*entry)->first);
      }

      [[nodiscard]] std::vector<Constant> release() &&
      {
        return std::move(entries_);
      }

    private:
      std::vector<Constant> entries_;
    };

    [[nodiscard]] Result<std::vector<BootstrapMethod>> parse_bootstrap_methods(
        std::span<const u8> attribute_bytes,
        const ConstantPool &pool)
    {
      ByteReader reader(attribute_bytes);
      auto method_count = reader.read_be_u16();
      if (!method_count)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated BootstrapMethods method count");
      }

      std::vector<BootstrapMethod> methods;
      methods.reserve(*method_count);
      for (u16 index = 0; index < *method_count; ++index)
      {
        auto method_handle_index = reader.read_be_u16();
        auto argument_count = reader.read_be_u16();
        if (!method_handle_index || !argument_count)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated BootstrapMethods entry");
        }
        auto method_handle = pool.get(*method_handle_index);
        if (!method_handle ||
            (*method_handle)->kind != ConstantKind::method_handle)
        {
          return fail(ErrorCode::malformed_class,
                      "bootstrap method does not reference a method handle");
        }

        BootstrapMethod method{
            .method_handle_index = *method_handle_index,
        };
        method.arguments.reserve(*argument_count);
        for (u16 argument_index = 0; argument_index < *argument_count;
             ++argument_index)
        {
          auto constant_index = reader.read_be_u16();
          if (!constant_index)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated bootstrap argument index");
          }
          auto argument = pool.get(*constant_index);
          if (!argument ||
              (*argument)->kind == ConstantKind::unusable)
          {
            return fail(ErrorCode::malformed_class,
                        "bootstrap argument references an invalid constant");
          }
          method.arguments.push_back(*constant_index);
        }
        methods.push_back(std::move(method));
      }
      if (!reader.empty())
      {
        return fail(ErrorCode::malformed_class,
                    "BootstrapMethods attribute has trailing bytes");
      }
      return methods;
    }

    [[nodiscard]] usize verification_slot_width(
        VerificationTypeKind kind) noexcept
    {
      return kind == VerificationTypeKind::long_integer ||
                     kind == VerificationTypeKind::float64
                 ? 2U
                 : 1U;
    }

    [[nodiscard]] Result<VerificationType> parse_verification_type(
        ByteReader &reader,
        const ConstantPool &pool,
        std::span<const u8> code)
    {
      auto tag = reader.read_u8();
      if (!tag || *tag > static_cast<u8>(VerificationTypeKind::uninitialized))
      {
        return fail(ErrorCode::malformed_class,
                    "invalid or truncated verification_type_info");
      }

      VerificationType result{
          .kind = static_cast<VerificationTypeKind>(*tag),
      };
      if (result.kind == VerificationTypeKind::object)
      {
        auto class_index = reader.read_be_u16();
        if (!class_index)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated object verification type");
        }
        auto class_name = pool.class_name(*class_index);
        if (!class_name)
        {
          return std::unexpected(class_name.error());
        }
        result.class_name = std::move(*class_name);
      }
      else if (result.kind == VerificationTypeKind::uninitialized)
      {
        auto new_instruction_pc = reader.read_be_u16();
        if (!new_instruction_pc)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated uninitialized verification type");
        }
        if (static_cast<usize>(*new_instruction_pc) >= code.size() ||
            code[*new_instruction_pc] != 0xBBU)
        {
          return fail(ErrorCode::malformed_class,
                      "uninitialized verification type does not reference new");
        }
        result.new_instruction_pc = *new_instruction_pc;
      }
      return result;
    }

    [[nodiscard]] Result<std::vector<VerificationType>> parse_verification_types(
        ByteReader &reader,
        const ConstantPool &pool,
        std::span<const u8> code,
        u16 count,
        usize maximum_slots,
        std::string_view context)
    {
      if (static_cast<usize>(count) > reader.remaining())
      {
        return fail(ErrorCode::malformed_class,
                    std::string(context) + " verification types exceed attribute");
      }

      std::vector<VerificationType> result;
      result.reserve(count);
      usize used_slots = 0;
      for (u16 index = 0; index < count; ++index)
      {
        auto type = parse_verification_type(reader, pool, code);
        if (!type)
        {
          return std::unexpected(type.error());
        }
        auto updated_slots = checked_add(used_slots,
                                         verification_slot_width(type->kind));
        if (!updated_slots || *updated_slots > maximum_slots)
        {
          return fail(ErrorCode::malformed_class,
                      std::string(context) +
                          " verification types exceed declared slots");
        }
        used_slots = *updated_slots;
        result.push_back(std::move(*type));
      }
      return result;
    }

    [[nodiscard]] Result<std::vector<StackMapFrame>> parse_cldc_stack_map(
        std::span<const u8> attribute_bytes,
        const ConstantPool &pool,
        std::span<const u8> code,
        u16 max_locals,
        u16 max_stack)
    {
      ByteReader reader(attribute_bytes);
      auto entry_count = reader.read_be_u16();
      if (!entry_count)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated CLDC StackMap entry count");
      }

      std::vector<StackMapFrame> frames;
      frames.reserve(*entry_count);
      std::optional<u16> previous_offset;
      for (u16 index = 0; index < *entry_count; ++index)
      {
        auto offset = reader.read_be_u16();
        auto local_count = reader.read_be_u16();
        if (!offset || !local_count)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated CLDC StackMap frame header");
        }
        if (static_cast<usize>(*offset) >= code.size() ||
            (previous_offset.has_value() && *offset <= *previous_offset))
        {
          return fail(ErrorCode::malformed_class,
                      "CLDC StackMap offsets are invalid or unordered");
        }
        auto locals = parse_verification_types(reader,
                                               pool,
                                               code,
                                               *local_count,
                                               max_locals,
                                               "CLDC StackMap locals");
        if (!locals)
        {
          return std::unexpected(locals.error());
        }
        auto stack_count = reader.read_be_u16();
        if (!stack_count)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated CLDC StackMap stack count");
        }
        auto stack = parse_verification_types(reader,
                                              pool,
                                              code,
                                              *stack_count,
                                              max_stack,
                                              "CLDC StackMap stack");
        if (!stack)
        {
          return std::unexpected(stack.error());
        }
        frames.push_back(StackMapFrame{
            .kind = StackMapFrameKind::cldc_full,
            .bytecode_offset = *offset,
            .locals = std::move(*locals),
            .stack = std::move(*stack),
        });
        previous_offset = *offset;
      }
      if (!reader.empty())
      {
        return fail(ErrorCode::malformed_class,
                    "CLDC StackMap attribute has trailing bytes");
      }
      return frames;
    }

    [[nodiscard]] Result<std::vector<StackMapFrame>> parse_stack_map_table(
        std::span<const u8> attribute_bytes,
        const ConstantPool &pool,
        std::span<const u8> code,
        u16 max_locals,
        u16 max_stack)
    {
      ByteReader reader(attribute_bytes);
      auto entry_count = reader.read_be_u16();
      if (!entry_count)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated StackMapTable entry count");
      }

      std::vector<StackMapFrame> frames;
      frames.reserve(*entry_count);
      i64 previous_offset = -1;
      for (u16 index = 0; index < *entry_count; ++index)
      {
        auto frame_type = reader.read_u8();
        if (!frame_type)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated StackMapTable frame type");
        }

        StackMapFrame frame;
        u16 offset_delta = 0;
        if (*frame_type <= 63U)
        {
          frame.kind = StackMapFrameKind::same;
          offset_delta = *frame_type;
        }
        else if (*frame_type <= 127U)
        {
          frame.kind = StackMapFrameKind::same_locals_one_stack;
          offset_delta = static_cast<u16>(*frame_type - 64U);
          auto stack = parse_verification_types(reader,
                                                pool,
                                                code,
                                                1,
                                                max_stack,
                                                "StackMapTable stack");
          if (!stack)
            return std::unexpected(stack.error());
          frame.stack = std::move(*stack);
        }
        else if (*frame_type == 247U)
        {
          frame.kind = StackMapFrameKind::same_locals_one_stack;
          auto delta = reader.read_be_u16();
          if (!delta)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated extended StackMapTable frame");
          }
          offset_delta = *delta;
          auto stack = parse_verification_types(reader,
                                                pool,
                                                code,
                                                1,
                                                max_stack,
                                                "StackMapTable stack");
          if (!stack)
            return std::unexpected(stack.error());
          frame.stack = std::move(*stack);
        }
        else if (*frame_type >= 248U && *frame_type <= 250U)
        {
          frame.kind = StackMapFrameKind::chop;
          frame.chopped_locals = static_cast<u8>(251U - *frame_type);
          auto delta = reader.read_be_u16();
          if (!delta)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated chop StackMapTable frame");
          }
          offset_delta = *delta;
        }
        else if (*frame_type == 251U)
        {
          frame.kind = StackMapFrameKind::same;
          auto delta = reader.read_be_u16();
          if (!delta)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated same StackMapTable frame");
          }
          offset_delta = *delta;
        }
        else if (*frame_type >= 252U && *frame_type <= 254U)
        {
          frame.kind = StackMapFrameKind::append;
          auto delta = reader.read_be_u16();
          if (!delta)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated append StackMapTable frame");
          }
          offset_delta = *delta;
          const u16 appended_count = static_cast<u16>(*frame_type - 251U);
          auto locals = parse_verification_types(reader,
                                                 pool,
                                                 code,
                                                 appended_count,
                                                 max_locals,
                                                 "StackMapTable appended locals");
          if (!locals)
            return std::unexpected(locals.error());
          frame.locals = std::move(*locals);
        }
        else if (*frame_type == 255U)
        {
          frame.kind = StackMapFrameKind::full;
          auto delta = reader.read_be_u16();
          auto local_count = reader.read_be_u16();
          if (!delta || !local_count)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated full StackMapTable frame header");
          }
          offset_delta = *delta;
          auto locals = parse_verification_types(reader,
                                                 pool,
                                                 code,
                                                 *local_count,
                                                 max_locals,
                                                 "StackMapTable locals");
          if (!locals)
            return std::unexpected(locals.error());
          frame.locals = std::move(*locals);
          auto stack_count = reader.read_be_u16();
          if (!stack_count)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated full StackMapTable stack count");
          }
          auto stack = parse_verification_types(reader,
                                                pool,
                                                code,
                                                *stack_count,
                                                max_stack,
                                                "StackMapTable stack");
          if (!stack)
            return std::unexpected(stack.error());
          frame.stack = std::move(*stack);
        }
        else
        {
          return fail(ErrorCode::malformed_class,
                      "StackMapTable uses a reserved frame type");
        }

        const i64 absolute_offset = previous_offset +
                                    static_cast<i64>(offset_delta) + 1;
        if (absolute_offset < 0 ||
            static_cast<u64>(absolute_offset) >=
                static_cast<u64>(code.size()))
        {
          return fail(ErrorCode::malformed_class,
                      "StackMapTable frame offset is outside bytecode");
        }
        auto narrowed_offset = checked_narrow<u16>(absolute_offset);
        if (!narrowed_offset)
        {
          return std::unexpected(narrowed_offset.error());
        }
        frame.bytecode_offset = *narrowed_offset;
        frames.push_back(std::move(frame));
        previous_offset = absolute_offset;
      }
      if (!reader.empty())
      {
        return fail(ErrorCode::malformed_class,
                    "StackMapTable attribute has trailing bytes");
      }
      return frames;
    }

    [[nodiscard]] Result<CodeAttribute> parse_code_attribute(
        std::span<const u8> attribute_bytes,
        const ConstantPool &pool)
    {
      ByteReader reader(attribute_bytes);

      auto max_stack = reader.read_be_u16();
      auto max_locals = reader.read_be_u16();
      auto code_length = reader.read_be_u32();
      if (!max_stack || !max_locals || !code_length)
      {
        return fail(ErrorCode::malformed_class, "truncated Code attribute");
      }

      if (*code_length > static_cast<u32>(std::numeric_limits<u16>::max()))
      {
        return fail(ErrorCode::malformed_class,
                    "Code attribute exceeds the JVM 65535-byte method limit");
      }
      auto code_size = checked_narrow<usize>(*code_length);
      if (!code_size)
      {
        return std::unexpected(code_size.error());
      }
      auto code = reader.read_span(*code_size);
      if (!code)
      {
        return fail(ErrorCode::malformed_class, "bytecode exceeds Code attribute");
      }

      auto exception_count = reader.read_be_u16();
      if (!exception_count)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated Code exception table count");
      }
      std::vector<ExceptionHandler> exception_table;
      exception_table.reserve(*exception_count);
      for (u16 index = 0; index < *exception_count; ++index)
      {
        auto start_pc = reader.read_be_u16();
        auto end_pc = reader.read_be_u16();
        auto handler_pc = reader.read_be_u16();
        auto catch_type_index = reader.read_be_u16();
        if (!start_pc || !end_pc || !handler_pc || !catch_type_index)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated Code exception table entry");
        }
        if (*start_pc >= *end_pc ||
            static_cast<usize>(*end_pc) > *code_size ||
            static_cast<usize>(*handler_pc) >= *code_size)
        {
          return fail(ErrorCode::malformed_class,
                      "Code exception handler range or target is invalid");
        }

        std::string catch_type;
        if (*catch_type_index != 0)
        {
          auto resolved_catch_type = pool.class_name(*catch_type_index);
          if (!resolved_catch_type)
          {
            return std::unexpected(resolved_catch_type.error());
          }
          catch_type = std::move(*resolved_catch_type);
        }
        exception_table.push_back(ExceptionHandler{
            .start_pc = *start_pc,
            .end_pc = *end_pc,
            .handler_pc = *handler_pc,
            .catch_type = std::move(catch_type),
        });
      }

      auto nested_count = reader.read_be_u16();
      if (!nested_count)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated Code nested attribute count");
      }
      std::vector<StackMapFrame> stack_map_frames;
      bool has_stack_map = false;
      for (u16 attribute_index = 0; attribute_index < *nested_count;
           ++attribute_index)
      {
        auto name_index = reader.read_be_u16();
        auto length = reader.read_be_u32();
        if (!name_index || !length)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated Code nested attribute header");
        }
        auto name = pool.utf8(*name_index);
        if (!name)
        {
          return std::unexpected(name.error());
        }
        auto attribute_size = checked_narrow<usize>(*length);
        if (!attribute_size)
        {
          return std::unexpected(attribute_size.error());
        }
        auto nested_bytes = reader.read_span(*attribute_size);
        if (!nested_bytes)
        {
          return fail(ErrorCode::malformed_class,
                      "Code nested attribute exceeds input bounds: " + *name);
        }

        if (*name == "StackMap" || *name == "StackMapTable")
        {
          if (has_stack_map)
          {
            return fail(ErrorCode::malformed_class,
                        "Code attribute contains multiple stack maps");
          }
          Result<std::vector<StackMapFrame>> parsed = *name == "StackMap"
                                                          ? parse_cldc_stack_map(*nested_bytes,
                                                                                 pool,
                                                                                 *code,
                                                                                 *max_locals,
                                                                                 *max_stack)
                                                          : parse_stack_map_table(*nested_bytes,
                                                                                  pool,
                                                                                  *code,
                                                                                  *max_locals,
                                                                                  *max_stack);
          if (!parsed)
          {
            return std::unexpected(parsed.error());
          }
          stack_map_frames = std::move(*parsed);
          has_stack_map = true;
        }
      }
      if (!reader.empty())
      {
        return fail(ErrorCode::malformed_class,
                    "Code attribute has trailing bytes");
      }

      auto verified = verify_code_structure(*code,
                                            exception_table,
                                            stack_map_frames);
      if (!verified)
      {
        return std::unexpected(verified.error());
      }

      return CodeAttribute{
          .max_stack = *max_stack,
          .max_locals = *max_locals,
          .bytecode = std::vector<u8>(code->begin(), code->end()),
          .exception_table = std::move(exception_table),
          .stack_map_frames = std::move(stack_map_frames),
      };
    }

    [[nodiscard]] Result<Field> parse_field(ByteReader &reader,
                                            const ConstantPool &pool)
    {
      auto access_flags = reader.read_be_u16();
      auto name_index = reader.read_be_u16();
      auto descriptor_index = reader.read_be_u16();
      auto attribute_count = reader.read_be_u16();
      if (!access_flags || !name_index || !descriptor_index || !attribute_count)
      {
        return fail(ErrorCode::malformed_class, "truncated field_info");
      }

      auto name = pool.utf8(*name_index);
      auto descriptor = pool.utf8(*descriptor_index);
      if (!name)
      {
        return std::unexpected(name.error());
      }
      if (!descriptor)
      {
        return std::unexpected(descriptor.error());
      }
      std::optional<u16> constant_value_index;
      for (u16 attribute_index = 0;
           attribute_index < *attribute_count;
           ++attribute_index)
      {
        auto attribute_name_index = reader.read_be_u16();
        auto attribute_length = reader.read_be_u32();
        if (!attribute_name_index || !attribute_length)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated field attribute header");
        }
        auto attribute_name = pool.utf8(*attribute_name_index);
        if (!attribute_name)
          return std::unexpected(attribute_name.error());
        auto attribute_size = checked_narrow<usize>(*attribute_length);
        if (!attribute_size)
          return std::unexpected(attribute_size.error());
        auto attribute_bytes = reader.read_span(*attribute_size);
        if (!attribute_bytes)
        {
          return fail(ErrorCode::malformed_class,
                      "field attribute exceeds class bounds");
        }
        if (*attribute_name != "ConstantValue")
          continue;

        // Legacy phoneME ignores ConstantValue on instance fields instead of
        // rejecting the whole class. Several obfuscators leave this harmless
        // attribute behind, so mirror the original loader while retaining
        // strict validation for static fields whose value is actually used.
        constexpr u16 kStatic = 0x0008U;
        if ((*access_flags & kStatic) == 0U)
          continue;
        if (constant_value_index.has_value() || *attribute_length != 2U)
        {
          return fail(ErrorCode::malformed_class,
                      "field contains an invalid ConstantValue attribute");
        }
        ByteReader constant_reader(*attribute_bytes);
        auto index = constant_reader.read_be_u16();
        if (!index || !constant_reader.empty())
        {
          return fail(ErrorCode::malformed_class,
                      "truncated ConstantValue attribute");
        }
        auto constant = pool.get(*index);
        if (!constant || (*constant)->kind == ConstantKind::unusable)
        {
          return fail(ErrorCode::malformed_class,
                      "ConstantValue references an invalid constant");
        }
        const Result<ConstantKind> expected = [&]() -> Result<ConstantKind>
        {
          if (*descriptor == "J")
            return ConstantKind::long64;
          if (*descriptor == "F")
            return ConstantKind::float32;
          if (*descriptor == "D")
            return ConstantKind::float64;
          if (*descriptor == "Ljava/lang/String;")
            return ConstantKind::string_ref;
          if (*descriptor == "Z" || *descriptor == "B" ||
              *descriptor == "C" || *descriptor == "S" ||
              *descriptor == "I")
          {
            return ConstantKind::integer;
          }
          return fail(ErrorCode::malformed_class,
                      "ConstantValue is not valid for this field descriptor");
        }();
        if (!expected)
          return std::unexpected(expected.error());
        if ((*constant)->kind != *expected)
        {
          return fail(ErrorCode::malformed_class,
                      "ConstantValue kind does not match field descriptor");
        }
        constant_value_index = *index;
      }

      return Field{
          .access_flags = *access_flags,
          .name = std::move(*name),
          .descriptor = std::move(*descriptor),
          .constant_value_index = constant_value_index,
      };
    }

    [[nodiscard]] Result<Method> parse_method(ByteReader &reader,
                                              const ConstantPool &pool)
    {
      auto access_flags = reader.read_be_u16();
      auto name_index = reader.read_be_u16();
      auto descriptor_index = reader.read_be_u16();
      auto attribute_count = reader.read_be_u16();
      if (!access_flags || !name_index || !descriptor_index || !attribute_count)
      {
        return fail(ErrorCode::malformed_class, "truncated method_info");
      }

      auto name = pool.utf8(*name_index);
      auto descriptor = pool.utf8(*descriptor_index);
      if (!name)
      {
        return std::unexpected(name.error());
      }
      if (!descriptor)
      {
        return std::unexpected(descriptor.error());
      }

      Method method{
          .access_flags = *access_flags,
          .name = std::move(*name),
          .descriptor = std::move(*descriptor),
          .code = std::nullopt,
      };

      for (u16 attribute_index = 0; attribute_index < *attribute_count;
           ++attribute_index)
      {
        auto attribute_name_index = reader.read_be_u16();
        auto attribute_length = reader.read_be_u32();
        if (!attribute_name_index || !attribute_length)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated method attribute header");
        }
        auto attribute_name = pool.utf8(*attribute_name_index);
        if (!attribute_name)
        {
          return std::unexpected(attribute_name.error());
        }
        auto attribute_size = checked_narrow<usize>(*attribute_length);
        if (!attribute_size)
        {
          return std::unexpected(attribute_size.error());
        }
        auto attribute_bytes = reader.read_span(*attribute_size);
        if (!attribute_bytes)
        {
          return fail(ErrorCode::malformed_class,
                      "method attribute exceeds class bounds");
        }

        if (*attribute_name == "Code")
        {
          if (method.code.has_value())
          {
            return fail(ErrorCode::malformed_class,
                        "method contains multiple Code attributes");
          }
          auto code = parse_code_attribute(*attribute_bytes, pool);
          if (!code)
          {
            auto error = code.error();
            error.message += " in method " + method.name + method.descriptor;
            return std::unexpected(std::move(error));
          }
          method.code = std::move(*code);
        }
      }

      return method;
    }

    [[nodiscard]] Result<ConstantPool> parse_constant_pool(ByteReader &reader,
                                                           u16 count)
    {
      if (count == 0)
      {
        return fail(ErrorCode::malformed_class,
                    "constant_pool_count must be greater than zero");
      }

      ConstantPool pool(count);
      for (u16 index = 1; index < count; ++index)
      {
        auto tag = reader.read_u8();
        if (!tag)
        {
          return fail(ErrorCode::malformed_class, "truncated constant pool");
        }

        auto &entry = pool.at(index);
        entry.kind = static_cast<ConstantKind>(*tag);

        switch (*tag)
        {
        case 1:
        {
          auto length = reader.read_be_u16();
          if (!length)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated UTF-8 constant length");
          }
          auto text = reader.read_string(*length);
          if (!text)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated UTF-8 constant bytes");
          }
          entry.utf8 = std::move(*text);
          break;
        }
        case 3:
        case 4:
        {
          auto bits = reader.read_be_u32();
          if (!bits)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated 32-bit constant");
          }
          entry.bits = *bits;
          break;
        }
        case 5:
        case 6:
        {
          auto bits = reader.read_be_u64();
          if (!bits)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated 64-bit constant");
          }
          entry.bits = *bits;
          if (index + 1 >= count)
          {
            return fail(ErrorCode::malformed_class,
                        "64-bit constant occupies missing pool slot");
          }
          ++index;
          pool.at(index).kind = ConstantKind::unusable;
          break;
        }
        case 7:
        case 8:
        case 16:
        case 19:
        case 20:
        {
          auto first = reader.read_be_u16();
          if (!first)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated single-index constant");
          }
          entry.first = *first;
          break;
        }
        case 9:
        case 10:
        case 11:
        case 12:
        case 17:
        case 18:
        {
          auto first = reader.read_be_u16();
          auto second = reader.read_be_u16();
          if (!first || !second)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated double-index constant");
          }
          entry.first = *first;
          entry.second = *second;
          break;
        }
        case 15:
        {
          auto reference_kind = reader.read_u8();
          auto reference_index = reader.read_be_u16();
          if (!reference_kind || !reference_index)
          {
            return fail(ErrorCode::malformed_class,
                        "truncated method-handle constant");
          }
          entry.first = *reference_kind;
          entry.second = *reference_index;
          break;
        }
        default:
          return fail(ErrorCode::malformed_class,
                      "unknown constant-pool tag " + std::to_string(*tag));
        }
      }
      return pool;
    }

  } // namespace

  Result<ClassFile> ClassFile::parse(std::span<const u8> bytes)
  {
    ByteReader reader(bytes);

    auto magic = reader.read_be_u32();
    auto minor = reader.read_be_u16();
    auto major = reader.read_be_u16();
    auto constant_pool_count = reader.read_be_u16();
    if (!magic || !minor || !major || !constant_pool_count)
    {
      return fail(ErrorCode::malformed_class, "truncated class-file header");
    }
    if (*magic != kClassMagic)
    {
      return fail(ErrorCode::malformed_class, "invalid class-file magic");
    }
    if (*major < kMinimumSupportedMajorVersion ||
        *major > kMaximumParsedMajorVersion)
    {
      return fail(ErrorCode::unsupported_class_version,
                  "unsupported class-file version " + std::to_string(*major) +
                      "." + std::to_string(*minor));
    }

    auto pool = parse_constant_pool(reader, *constant_pool_count);
    if (!pool)
    {
      return std::unexpected(pool.error());
    }

    auto access_flags = reader.read_be_u16();
    auto this_class = reader.read_be_u16();
    auto super_class = reader.read_be_u16();
    auto interface_count = reader.read_be_u16();
    if (!access_flags || !this_class || !super_class || !interface_count)
    {
      return fail(ErrorCode::malformed_class,
                  "truncated class declaration");
    }

    ClassFile result;
    result.minor_version_ = *minor;
    result.major_version_ = *major;
    result.access_flags_ = *access_flags;

    auto class_name = pool->class_name(*this_class);
    if (!class_name)
    {
      return std::unexpected(class_name.error());
    }
    result.name_ = std::move(*class_name);

    if (*super_class != 0)
    {
      auto super_name = pool->class_name(*super_class);
      if (!super_name)
      {
        return std::unexpected(super_name.error());
      }
      result.super_name_ = std::move(*super_name);
    }

    result.interfaces_.reserve(*interface_count);
    for (u16 index = 0; index < *interface_count; ++index)
    {
      auto interface_index = reader.read_be_u16();
      if (!interface_index)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated interface table");
      }
      auto interface_name = pool->class_name(*interface_index);
      if (!interface_name)
      {
        return std::unexpected(interface_name.error());
      }
      result.interfaces_.push_back(std::move(*interface_name));
    }

    auto field_count = reader.read_be_u16();
    if (!field_count)
    {
      return fail(ErrorCode::malformed_class, "truncated field count");
    }
    result.fields_.reserve(*field_count);
    for (u16 index = 0; index < *field_count; ++index)
    {
      auto field = parse_field(reader, *pool);
      if (!field)
      {
        return std::unexpected(field.error());
      }
      result.fields_.push_back(std::move(*field));
    }

    auto method_count = reader.read_be_u16();
    if (!method_count)
    {
      return fail(ErrorCode::malformed_class, "truncated method count");
    }
    result.methods_.reserve(*method_count);
    for (u16 index = 0; index < *method_count; ++index)
    {
      auto method = parse_method(reader, *pool);
      if (!method)
      {
        auto error = method.error();
        error.message += " in class " + result.name_;
        return std::unexpected(std::move(error));
      }
      result.methods_.push_back(std::move(*method));
    }

    auto class_attribute_count = reader.read_be_u16();
    if (!class_attribute_count)
    {
      return fail(ErrorCode::malformed_class,
                  "truncated class attribute count");
    }
    bool has_bootstrap_methods = false;
    for (u16 attribute_index = 0;
         attribute_index < *class_attribute_count;
         ++attribute_index)
    {
      auto attribute_name_index = reader.read_be_u16();
      auto attribute_length = reader.read_be_u32();
      if (!attribute_name_index || !attribute_length)
      {
        return fail(ErrorCode::malformed_class,
                    "truncated class attribute header");
      }
      auto attribute_name = pool->utf8(*attribute_name_index);
      if (!attribute_name)
      {
        return std::unexpected(attribute_name.error());
      }
      auto attribute_size = checked_narrow<usize>(*attribute_length);
      if (!attribute_size)
      {
        return std::unexpected(attribute_size.error());
      }
      auto attribute_bytes = reader.read_span(*attribute_size);
      if (!attribute_bytes)
      {
        return fail(ErrorCode::malformed_class,
                    "class attribute exceeds input bounds: " +
                        *attribute_name);
      }

      if (*attribute_name == "BootstrapMethods")
      {
        if (has_bootstrap_methods)
        {
          return fail(ErrorCode::malformed_class,
                      "class contains multiple BootstrapMethods attributes");
        }
        auto methods = parse_bootstrap_methods(*attribute_bytes, *pool);
        if (!methods)
        {
          return std::unexpected(methods.error());
        }
        result.bootstrap_methods_ = std::move(*methods);
        has_bootstrap_methods = true;
      }
    }
    if (!reader.empty())
    {
      return fail(ErrorCode::malformed_class,
                  "class file has trailing bytes");
    }

    result.constants_ = std::move(*pool).release();
    for (usize index = 1; index < result.constants_.size(); ++index)
    {
      const Constant &constant = result.constants_[index];
      if (constant.kind == ConstantKind::method_handle)
      {
        auto handle = result.method_handle_reference(
            static_cast<u16>(index));
        if (!handle)
        {
          return std::unexpected(handle.error());
        }
      }
      else if (constant.kind == ConstantKind::method_type)
      {
        auto descriptor = result.method_type_descriptor(
            static_cast<u16>(index));
        if (!descriptor)
        {
          return std::unexpected(descriptor.error());
        }
      }
      else if (constant.kind == ConstantKind::invoke_dynamic ||
               constant.kind == ConstantKind::dynamic)
      {
        if (constant.first >= result.bootstrap_methods_.size())
        {
          return fail(ErrorCode::malformed_class,
                      "dynamic constant references a missing bootstrap method");
        }
        if (constant.kind == ConstantKind::invoke_dynamic)
        {
          auto reference = result.invoke_dynamic_reference(
              static_cast<u16>(index));
          if (!reference)
          {
            return std::unexpected(reference.error());
          }
        }
      }
    }
    auto indexed = result.rebuild_method_index();
    if (!indexed)
    {
      return std::unexpected(indexed.error());
    }
    return result;
  }

  ClassFile ClassFile::builtin(std::string name,
                               std::string super_name,
                               u16 access_flags,
                               std::vector<Field> fields,
                               std::vector<Method> methods,
                               std::vector<std::string> interfaces)
  {
    ClassFile result;
    result.minor_version_ = 0;
    result.major_version_ = 45;
    result.access_flags_ = access_flags;
    result.name_ = std::move(name);
    result.super_name_ = std::move(super_name);
    result.interfaces_ = std::move(interfaces);
    result.fields_ = std::move(fields);
    result.methods_ = std::move(methods);
    static_cast<void>(result.rebuild_method_index());
    return result;
  }

  Result<const Constant *> ClassFile::constant(u16 index) const
  {
    if (index == 0 || static_cast<usize>(index) >= constants_.size())
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool index is out of range");
    }
    return &constants_[index];
  }

  Result<std::string> ClassFile::utf8_constant(u16 index) const
  {
    auto entry = constant(index);
    if (!entry)
    {
      return std::unexpected(entry.error());
    }
    if ((*entry)->kind != ConstantKind::utf8)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not UTF-8");
    }
    return (*entry)->utf8;
  }

  Result<std::string> ClassFile::class_name_constant(u16 index) const
  {
    auto entry = constant(index);
    if (!entry)
    {
      return std::unexpected(entry.error());
    }
    if ((*entry)->kind != ConstantKind::class_ref)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not a class reference");
    }
    return utf8_constant((*entry)->first);
  }

  Result<std::string> ClassFile::string_constant(u16 index) const
  {
    auto entry = constant(index);
    if (!entry)
    {
      return std::unexpected(entry.error());
    }
    if ((*entry)->kind != ConstantKind::string_ref)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not a String reference");
    }
    return utf8_constant((*entry)->first);
  }

  Result<MemberReference> ClassFile::member_reference(u16 index) const
  {
    auto entry = constant(index);
    if (!entry)
    {
      return std::unexpected(entry.error());
    }
    if ((*entry)->kind != ConstantKind::field_ref &&
        (*entry)->kind != ConstantKind::method_ref &&
        (*entry)->kind != ConstantKind::interface_method_ref)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not a member reference");
    }

    auto owner = class_name_constant((*entry)->first);
    if (!owner)
    {
      return std::unexpected(owner.error());
    }
    auto name_and_type = constant((*entry)->second);
    if (!name_and_type)
    {
      return std::unexpected(name_and_type.error());
    }
    if ((*name_and_type)->kind != ConstantKind::name_and_type)
    {
      return fail(ErrorCode::malformed_class,
                  "member reference has an invalid name-and-type entry");
    }
    auto name = utf8_constant((*name_and_type)->first);
    auto descriptor = utf8_constant((*name_and_type)->second);
    if (!name)
    {
      return std::unexpected(name.error());
    }
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }

    return MemberReference{
        .kind = (*entry)->kind,
        .owner = std::move(*owner),
        .name = std::move(*name),
        .descriptor = std::move(*descriptor),
    };
  }

  Result<MethodHandleReference> ClassFile::method_handle_reference(
      u16 index) const
  {
    auto handle = constant(index);
    if (!handle)
    {
      return std::unexpected(handle.error());
    }
    if ((*handle)->kind != ConstantKind::method_handle)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not a method handle");
    }
    const u16 reference_kind = (*handle)->first;
    if (reference_kind < 1U || reference_kind > 9U)
    {
      return fail(ErrorCode::malformed_class,
                  "method handle contains an invalid reference kind");
    }
    auto member = member_reference((*handle)->second);
    if (!member)
    {
      return std::unexpected(member.error());
    }

    bool compatible = false;
    switch (reference_kind)
    {
    case 1:
    case 2:
    case 3:
    case 4:
      compatible = member->kind == ConstantKind::field_ref;
      break;
    case 5:
    case 8:
      compatible = member->kind == ConstantKind::method_ref;
      break;
    case 6:
    case 7:
      compatible = member->kind == ConstantKind::method_ref ||
                   (major_version_ >= 52U &&
                    member->kind == ConstantKind::interface_method_ref);
      break;
    case 9:
      compatible = member->kind == ConstantKind::interface_method_ref;
      break;
    default:
      break;
    }
    if (!compatible)
    {
      return fail(ErrorCode::malformed_class,
                  "method handle reference kind does not match its member");
    }
    if (reference_kind == 8U)
    {
      if (member->name != "<init>")
      {
        return fail(ErrorCode::malformed_class,
                    "new-invoke-special method handle must reference <init>");
      }
    }
    else if (member->name == "<init>" || member->name == "<clinit>")
    {
      return fail(ErrorCode::malformed_class,
                  "method handle illegally references an initializer");
    }

    return MethodHandleReference{
        .reference_kind = static_cast<u8>(reference_kind),
        .member = std::move(*member),
    };
  }

  Result<std::string> ClassFile::method_type_descriptor(u16 index) const
  {
    auto method_type = constant(index);
    if (!method_type)
    {
      return std::unexpected(method_type.error());
    }
    if ((*method_type)->kind != ConstantKind::method_type)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not a method type");
    }
    auto descriptor = utf8_constant((*method_type)->first);
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }
    if (descriptor->empty() || descriptor->front() != '(')
    {
      return fail(ErrorCode::malformed_class,
                  "method type contains an invalid descriptor");
    }
    return descriptor;
  }

  Result<InvokeDynamicReference> ClassFile::invoke_dynamic_reference(
      u16 index) const
  {
    auto dynamic = constant(index);
    if (!dynamic)
    {
      return std::unexpected(dynamic.error());
    }
    if ((*dynamic)->kind != ConstantKind::invoke_dynamic)
    {
      return fail(ErrorCode::malformed_class,
                  "constant-pool entry is not InvokeDynamic");
    }
    if ((*dynamic)->first >= bootstrap_methods_.size())
    {
      return fail(ErrorCode::malformed_class,
                  "InvokeDynamic references a missing bootstrap method");
    }
    auto name_and_type = constant((*dynamic)->second);
    if (!name_and_type)
    {
      return std::unexpected(name_and_type.error());
    }
    if ((*name_and_type)->kind != ConstantKind::name_and_type)
    {
      return fail(ErrorCode::malformed_class,
                  "InvokeDynamic has an invalid name-and-type entry");
    }
    auto name = utf8_constant((*name_and_type)->first);
    auto descriptor = utf8_constant((*name_and_type)->second);
    if (!name)
    {
      return std::unexpected(name.error());
    }
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }
    if (*name == "<init>" || *name == "<clinit>" ||
        descriptor->empty() || descriptor->front() != '(')
    {
      return fail(ErrorCode::malformed_class,
                  "InvokeDynamic has an invalid name or descriptor");
    }
    return InvokeDynamicReference{
        .bootstrap_method_index = (*dynamic)->first,
        .name = std::move(*name),
        .descriptor = std::move(*descriptor),
    };
  }

  Result<const BootstrapMethod *> ClassFile::bootstrap_method(u16 index) const
  {
    if (static_cast<usize>(index) >= bootstrap_methods_.size())
    {
      return fail(ErrorCode::malformed_class,
                  "bootstrap method index is out of range");
    }
    return &bootstrap_methods_[index];
  }

  const Method *ClassFile::find_method(std::string_view name,
                                       std::string_view descriptor) const noexcept
  {
    const auto iterator = method_index_.find(MethodSignatureView{
        .name = name,
        .descriptor = descriptor,
    });
    if (iterator == method_index_.end() || iterator->second >= methods_.size())
    {
      return nullptr;
    }
    return &methods_[iterator->second];
  }

  Status ClassFile::rebuild_method_index()
  {
    method_index_.clear();
    method_index_.reserve(methods_.size());
    for (usize index = 0; index < methods_.size(); ++index)
    {
      const Method &method = methods_[index];
      const auto [iterator, inserted] = method_index_.emplace(
          MethodSignatureKey{
              .name = method.name,
              .descriptor = method.descriptor,
          },
          index);
      (void)iterator;
      if (!inserted)
      {
        return fail(ErrorCode::malformed_class,
                    "class contains a duplicate method signature: " +
                        method.name + method.descriptor);
      }
    }
    return {};
  }

} // namespace phoneme::classfile
