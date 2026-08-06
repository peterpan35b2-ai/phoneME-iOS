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
#include <numbers>
#include <utility>
#include <vector>

#include "phoneme/base/Checked.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/ModifiedUtf8.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"
#include "phoneme/vm/SlotStorage.hpp"
#include "phoneme/vm/VmTrace.hpp"

#ifndef PHONEME_ENABLE_DECODED_EXECUTION
#define PHONEME_ENABLE_DECODED_EXECUTION 0
#endif

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
    constexpr u64 kMaintenancePollInterval = 256U;
    static_assert((kMaintenancePollInterval &
                   (kMaintenancePollInterval - 1U)) == 0U);

    [[nodiscard]] std::vector<char32_t> utf32_from_utf16(
        std::u16string_view input)
    {
      std::vector<char32_t> output;
      output.reserve(input.size());
      for (usize index = 0U; index < input.size(); ++index)
      {
        u32 value = static_cast<u16>(input[index]);
        if (value >= 0xD800U && value <= 0xDBFFU &&
            index + 1U < input.size())
        {
          const u32 low = static_cast<u16>(input[index + 1U]);
          if (low >= 0xDC00U && low <= 0xDFFFU)
          {
            value = 0x10000U + ((value - 0xD800U) << 10U) +
                    (low - 0xDC00U);
            ++index;
          }
        }
        output.push_back(static_cast<char32_t>(value));
      }
      return output;
    }

    [[nodiscard]] bool decoded_execution_requested() noexcept
    {
#if PHONEME_ENABLE_DECODED_EXECUTION
      static const bool requested = [] {
        const char* option = std::getenv("PHONEME_USE_DECODED_EXECUTION");
        return option == nullptr || std::string_view(option) != "0";
      }();
      return requested;
#else
      return false;
#endif
    }

    [[nodiscard]] bool specialized_intrinsics_requested() noexcept
    {
      static const bool requested = [] {
        const char* option = std::getenv("PHONEME_USE_SPECIALIZED_INTRINSICS");
        return option == nullptr || std::string_view(option) != "0";
      }();
      return requested;
    }

    [[nodiscard]] bool should_trace_slow_native(
        std::string_view owner,
        std::string_view name) noexcept
    {
      // These natives intentionally wait or encompass a complete Java worker
      // lifetime. Their durations are expected and are traced by the scheduler,
      // monitor or framebuffer paths instead of being reported as slow I/O.
      if (owner == "java/lang/Thread" &&
          (name == "sleep" || name == "run" || name == "join"))
      {
        return false;
      }
      if (owner == "java/lang/Object" && name == "wait")
        return false;
      if (owner == "javax/microedition/lcdui/Canvas" &&
          name == "serviceRepaints")
      {
        return false;
      }
      return true;
    }

    [[nodiscard]] Error stable_linkage_error(
        Error error,
        OperandResolutionKind kind)
    {
      if (error.code == ErrorCode::java_exception)
        return error;
      if (error.code == ErrorCode::class_not_found)
      {
        if (kind == OperandResolutionKind::field &&
            error.message.starts_with("field was not found"))
        {
          return Error::make_java("java/lang/NoSuchFieldError",
                                  std::move(error.message));
        }
        return Error::make_java("java/lang/NoClassDefFoundError",
                                std::move(error.message));
      }
      if (error.code == ErrorCode::method_not_found)
      {
        return Error::make_java("java/lang/NoSuchMethodError",
                                std::move(error.message));
      }
      if (error.code == ErrorCode::invalid_state &&
          (kind == OperandResolutionKind::field ||
           kind == OperandResolutionKind::direct_call ||
           kind == OperandResolutionKind::virtual_call))
      {
        return Error::make_java(
            "java/lang/IncompatibleClassChangeError",
            std::move(error.message));
      }
      return error;
    }

    [[nodiscard]] bool translation_wrapping_space(
        char32_t character) noexcept
    {
      return character == U' ' || character == U'\t' ||
             character == U'\r' || character == U'\u3000';
    }

    [[nodiscard]] usize translated_wrapped_line_count(
        const graphics::Font &font,
        std::span<const char32_t> text,
        i32 maximum_width)
    {
      usize line_count = 0U;
      std::vector<char32_t> current;
      auto flush = [&]()
      {
        while (!current.empty() &&
               translation_wrapping_space(current.front()))
          current.erase(current.begin());
        while (!current.empty() &&
               translation_wrapping_space(current.back()))
          current.pop_back();
        if (!current.empty())
          ++line_count;
        current.clear();
      };

      for (const char32_t character : text)
      {
        if (character == U'\n')
        {
          flush();
          continue;
        }
        current.push_back(character);
        if (font.chars_width(current) <= maximum_width)
          continue;

        usize break_index = current.size();
        while (break_index > 1U &&
               !translation_wrapping_space(current[break_index - 1U]))
          --break_index;
        if (break_index <= 1U)
        {
          const char32_t overflow = current.back();
          current.pop_back();
          flush();
          current.push_back(overflow);
          continue;
        }

        std::vector<char32_t> remainder(
            current.begin() + static_cast<std::ptrdiff_t>(break_index),
            current.end());
        current.erase(
            current.begin() + static_cast<std::ptrdiff_t>(break_index),
            current.end());
        flush();
        current = std::move(remainder);
        while (!current.empty() &&
               translation_wrapping_space(current.front()))
          current.erase(current.begin());
      }
      flush();
      return std::max<usize>(line_count, 1U);
    }

    [[nodiscard]] i32 translated_available_width(
        i32 x,
        i32 anchor,
        i32 clip_x,
        i32 clip_width,
        i32 translate_x) noexcept
    {
      const i32 resolved_anchor = anchor == 0
          ? graphics::anchor_left | graphics::anchor_top
          : anchor;
      const i32 horizontal = resolved_anchor &
          (graphics::anchor_left | graphics::anchor_right |
           graphics::anchor_hcenter);
      const i64 absolute_x = static_cast<i64>(x) + translate_x;
      const i64 clip_left = clip_x;
      const i64 clip_right = clip_left + clip_width;
      i64 available = clip_width;
      if (horizontal == graphics::anchor_left)
        available = clip_right - std::max(absolute_x, clip_left);
      else if (horizontal == graphics::anchor_right)
        available = std::min(absolute_x, clip_right) - clip_left;
      else if (horizontal == graphics::anchor_hcenter)
        available = 2 * std::min(
            std::max<i64>(0, absolute_x - clip_left),
            std::max<i64>(0, clip_right - absolute_x));
      return static_cast<i32>(std::clamp<i64>(
          available, 1, std::numeric_limits<i32>::max()));
    }

    [[nodiscard]] i32 translated_text_height(
        const graphics::Font &font,
        std::span<const char32_t> text,
        i32 maximum_width)
    {
      const usize line_count = translated_wrapped_line_count(
          font, text, std::max(maximum_width, 1));
      const i32 line_advance = std::max(font.height(), 1) + 1;
      const i64 total_height =
          static_cast<i64>(line_count) * line_advance - 1;
      return static_cast<i32>(std::clamp<i64>(
          total_height, 1, std::numeric_limits<i32>::max()));
    }

    [[nodiscard]] i32 translated_text_top(
        i32 y,
        i32 anchor,
        const graphics::Font &font,
        i32 translate_y) noexcept
    {
      const i32 resolved_anchor = anchor == 0
          ? graphics::anchor_left | graphics::anchor_top
          : anchor;
      const i32 vertical = resolved_anchor &
          (graphics::anchor_top | graphics::anchor_bottom |
           graphics::anchor_baseline);
      i64 top = static_cast<i64>(y) + translate_y;
      if (vertical == graphics::anchor_bottom)
        top -= font.height();
      else if (vertical == graphics::anchor_baseline)
        top -= font.baseline();
      return static_cast<i32>(std::clamp<i64>(
          top,
          std::numeric_limits<i32>::min(),
          std::numeric_limits<i32>::max()));
    }

    [[nodiscard]] i32 translated_y_for_top(
        i32 top,
        i32 block_height,
        i32 anchor,
        const graphics::Font &font,
        i32 translate_y) noexcept
    {
      const i32 resolved_anchor = anchor == 0
          ? graphics::anchor_left | graphics::anchor_top
          : anchor;
      const i32 vertical = resolved_anchor &
          (graphics::anchor_top | graphics::anchor_bottom |
           graphics::anchor_baseline);
      i64 y = top;
      if (vertical == graphics::anchor_bottom)
        y += block_height;
      else if (vertical == graphics::anchor_baseline)
        y += font.baseline();
      y -= translate_y;
      return static_cast<i32>(std::clamp<i64>(
          y,
          std::numeric_limits<i32>::min(),
          std::numeric_limits<i32>::max()));
    }

    [[nodiscard]] constexpr HeapLimits heap_limits_with_emergency_reserve(
        HeapLimits requested) noexcept
    {
      const usize hard_limit =
          static_cast<usize>(std::numeric_limits<u32>::max()) - 1U;
      requested.maximum_objects = requested.maximum_objects < hard_limit
          ? requested.maximum_objects + 1U : hard_limit;
      return requested;
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

    [[nodiscard]] bool matches_long_bit_permutation_intrinsic(
        const classfile::Method& method) noexcept
    {
      if ((method.access_flags & kAccStatic) == 0U ||
          method.descriptor != "(JII[I[J)J" || !method.code.has_value())
      {
        return false;
      }
      static constexpr std::array<u8, 139> kPattern {{
          0x09U, 0x37U, 0x06U, 0x19U, 0x04U, 0xBEU, 0x36U, 0x08U,
          0x03U, 0x36U, 0x09U, 0xA7U, 0x00U, 0x44U, 0x1EU, 0xB2U,
          0x00U, 0x0BU, 0x15U, 0x09U, 0x2FU, 0x7FU, 0x37U, 0x0AU,
          0x19U, 0x04U, 0x15U, 0x09U, 0x2EU, 0x36U, 0x0CU, 0x16U,
          0x0AU, 0x09U, 0x94U, 0x99U, 0x00U, 0x29U, 0x15U, 0x0CU,
          0x9EU, 0x00U, 0x0DU, 0x16U, 0x0AU, 0x15U, 0x0CU, 0x7DU,
          0x37U, 0x0AU, 0xA7U, 0x00U, 0x13U, 0x15U, 0x0CU, 0x9CU,
          0x00U, 0x0EU, 0x16U, 0x0AU, 0x15U, 0x0CU, 0x02U, 0x82U,
          0x04U, 0x60U, 0x79U, 0x37U, 0x0AU, 0x16U, 0x06U, 0x16U,
          0x0AU, 0x81U, 0x37U, 0x06U, 0x84U, 0x09U, 0x01U, 0x15U,
          0x09U, 0x15U, 0x08U, 0xA1U, 0xFFU, 0xBBU, 0x10U, 0x40U,
          0x36U, 0x08U, 0x16U, 0x06U, 0x37U, 0x0AU, 0x15U, 0x08U,
          0x04U, 0x64U, 0x1DU, 0x64U, 0x36U, 0x0CU, 0x15U, 0x0CU,
          0x9EU, 0x00U, 0x0AU, 0x16U, 0x0AU, 0x15U, 0x0CU, 0x79U,
          0x37U, 0x0AU, 0x1CU, 0x15U, 0x08U, 0x60U, 0x04U, 0x64U,
          0x1DU, 0x64U, 0x36U, 0x0DU, 0x15U, 0x0DU, 0x9EU, 0x00U,
          0x0AU, 0x16U, 0x0AU, 0x15U, 0x0DU, 0x7DU, 0x37U, 0x0AU,
          0x16U, 0x0AU, 0xADU,
      }};
      const auto& bytecode = method.code->bytecode;
      if (bytecode.size() != kPattern.size()) return false;
      // Bytes 16-17 are the constant-pool index of the static long[] mask
      // field and legitimately vary between otherwise identical obfuscator
      // output.
      return std::equal(bytecode.begin(), bytecode.begin() + 16U,
                        kPattern.begin()) &&
             std::equal(bytecode.begin() + 18U, bytecode.end(),
                        kPattern.begin() + 18U);
    }

    [[nodiscard]] bool matches_range_decoder_bit_intrinsic(
        const classfile::Method& method) noexcept
    {
      if ((method.access_flags & kAccStatic) == 0U ||
          method.descriptor != "(I)I" || !method.code.has_value())
      {
        return false;
      }
      const auto& code = method.code->bytecode;
      if (code.size() != 150U) return false;
      static constexpr std::pair<usize, u8> kFixed[] = {
          {0U, 0xB2U}, {3U, 0x10U}, {4U, 11U}, {5U, 0x7BU},
          {6U, 0xB2U}, {9U, 0x1AU}, {10U, 0x35U}, {11U, 0x85U},
          {12U, 0x69U}, {13U, 0x40U}, {14U, 0xB2U}, {17U, 0x1FU},
          {18U, 0x94U}, {19U, 0x9CU}, {22U, 0x1FU}, {23U, 0xB3U},
          {26U, 0x11U}, {27U, 0x08U}, {28U, 0x00U}, {29U, 0xB2U},
          {32U, 0x1AU}, {33U, 0x35U}, {34U, 0x64U}, {35U, 0x3EU},
          {36U, 0xB2U}, {39U, 0x1AU}, {40U, 0x5CU}, {41U, 0x35U},
          {42U, 0x1DU}, {43U, 0x08U}, {44U, 0x7AU}, {45U, 0x60U},
          {46U, 0x93U}, {47U, 0x56U}, {48U, 0xB2U}, {51U, 0x14U},
          {54U, 0x94U}, {55U, 0x9CU}, {58U, 0xB2U}, {61U, 0x10U},
          {62U, 8U}, {63U, 0x79U}, {64U, 0xB8U}, {67U, 0x85U},
          {68U, 0x81U}, {69U, 0xB3U}, {72U, 0xB2U}, {75U, 0x10U},
          {76U, 8U}, {77U, 0x79U}, {78U, 0xB3U}, {81U, 0x03U},
          {82U, 0xACU}, {83U, 0xB2U}, {86U, 0x1FU}, {87U, 0x65U},
          {88U, 0xB3U}, {91U, 0xB2U}, {94U, 0x1FU}, {95U, 0x65U},
          {96U, 0xB3U}, {99U, 0xB2U}, {102U, 0x1AU}, {103U, 0x5CU},
          {104U, 0x35U}, {105U, 0xB2U}, {108U, 0x1AU}, {109U, 0x35U},
          {110U, 0x08U}, {111U, 0x7AU}, {112U, 0x64U}, {113U, 0x93U},
          {114U, 0x56U}, {115U, 0xB2U}, {118U, 0x14U},
          {121U, 0x94U}, {122U, 0x9CU}, {125U, 0xB2U},
          {128U, 0x10U}, {129U, 8U}, {130U, 0x79U}, {131U, 0xB8U},
          {134U, 0x85U}, {135U, 0x81U}, {136U, 0xB3U},
          {139U, 0xB2U}, {142U, 0x10U}, {143U, 8U}, {144U, 0x79U},
          {145U, 0xB3U}, {148U, 0x04U}, {149U, 0xACU},
      };
      for (const auto& [offset, expected] : kFixed)
      {
        if (code[offset] != expected) return false;
      }
      return true;
    }

    [[nodiscard]] bool matches_range_decoder_input_intrinsic(
        const classfile::Method& method) noexcept
    {
      if ((method.access_flags & kAccStatic) == 0U ||
          method.descriptor != "()I" || !method.code.has_value())
      {
        return false;
      }
      const auto& code = method.code->bytecode;
      if (code.size() != 31U) return false;
      static constexpr std::pair<usize, u8> kFixed[] = {
          {0U, 0xB2U}, {3U, 0xB2U}, {6U, 0xA0U},
          {9U, 0x11U}, {10U, 0x00U}, {11U, 0xFFU}, {12U, 0xACU},
          {13U, 0xB2U}, {16U, 0xB2U}, {19U, 0x59U}, {20U, 0x04U},
          {21U, 0x60U}, {22U, 0xB3U}, {25U, 0x33U},
          {26U, 0x11U}, {27U, 0x00U}, {28U, 0xFFU},
          {29U, 0x7EU}, {30U, 0xACU},
      };
      for (const auto& [offset, expected] : kFixed)
      {
        if (code[offset] != expected) return false;
      }
      return true;
    }

    [[nodiscard]] bool matches_vector_key_sort_initializer(
        const classfile::Method& method) noexcept
    {
      if ((method.access_flags & kAccStatic) == 0U ||
          method.descriptor != "()V" || !method.code.has_value())
      {
        return false;
      }
      const auto& code = method.code->bytecode;
      if (code.size() != 58U) return false;
      static constexpr std::pair<usize, u8> kFixed[] = {
          {0U, 0xB2U}, {3U, 0xB6U}, {6U, 0x3BU},
          {7U, 0xBBU}, {10U, 0x59U}, {11U, 0x1AU},
          {12U, 0xB7U}, {15U, 0x4CU}, {16U, 0x03U},
          {17U, 0x3DU}, {18U, 0xA7U}, {19U, 0x00U},
          {20U, 0x11U}, {21U, 0x2BU}, {22U, 0xB2U},
          {25U, 0x1CU}, {26U, 0xB6U}, {29U, 0xB6U},
          {32U, 0x84U}, {33U, 0x02U}, {34U, 0x01U},
          {35U, 0x1CU}, {36U, 0x1AU}, {37U, 0xA1U},
          {38U, 0xFFU}, {39U, 0xF0U}, {40U, 0x03U},
          {41U, 0xB2U}, {44U, 0xB6U}, {47U, 0x04U},
          {48U, 0x64U}, {49U, 0xB2U}, {52U, 0x2BU},
          {53U, 0x03U}, {54U, 0xB8U}, {57U, 0xB1U},
      };
      for (const auto& [offset, expected] : kFixed)
      {
        if (code[offset] != expected) return false;
      }
      return code[1U] == code[23U] && code[2U] == code[24U] &&
             code[1U] == code[42U] && code[2U] == code[43U] &&
             code[1U] == code[50U] && code[2U] == code[51U];
    }

    class ExecutionFrame final
    {
    public:
      struct InvokeInterfaceOperands final
      {
        u16 constant_pool_index{0};
        u8 count{0};
      };

      struct IncrementOperands final
      {
        u16 local_index{0};
        i32 increment{0};
      };

      struct WideOperands final
      {
        u8 opcode{0};
        u16 local_index{0};
        std::optional<i16> increment;
      };

      struct MultiArrayOperands final
      {
        u16 constant_pool_index{0};
        u8 dimensions{0};
      };

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

      [[nodiscard]] MethodId runtime_method_id() const noexcept
      {
        return resolved_.runtime != nullptr
            ? resolved_.runtime->id
            : MethodId {};
      }

      [[nodiscard]] u32 current_decoded_operand_index() const noexcept
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        if (decoded_ == nullptr ||
            current_decoded_index_ == kInvalidDecodedIndex ||
            current_decoded_index_ >= decoded_->instructions.size())
        {
          return kInvalidDecodedIndex;
        }
        return decoded_->instructions[current_decoded_index_].operand_index;
#else
        return kInvalidDecodedIndex;
#endif
      }

      void append_upcoming_translation_candidates(
          std::vector<std::vector<char32_t>>& output,
          usize maximum_candidates = 6U,
          usize maximum_instructions = 96U) const
      {
        if (maximum_candidates == 0U || maximum_instructions == 0U ||
            resolved_.runtime == nullptr || !resolved_.runtime->decoded)
        {
          return;
        }
        const DecodedMethod& decoded = *resolved_.runtime->decoded;
        if (current_instruction_pc_ > std::numeric_limits<u32>::max())
          return;
        const u32 current_index = decoded.instruction_index_for_bci(
            static_cast<u32>(current_instruction_pc_));
        if (current_index == kInvalidDecodedIndex ||
            current_index >= decoded.instructions.size())
        {
          return;
        }

        u32 instruction_index = decoded.instructions[current_index].next_index;
        usize scanned = 0U;
        while (instruction_index < decoded.instructions.size() &&
               scanned < maximum_instructions &&
               output.size() < maximum_candidates)
        {
          const DecodedInstruction& instruction =
              decoded.instructions[instruction_index];
          const u8 opcode = raw_opcode(instruction.opcode);
          if ((opcode == 0x12U || opcode == 0x13U) &&
              instruction.operand_index != kInvalidDecodedIndex &&
              instruction.operand_index < decoded.operands.size())
          {
            const DecodedOperand& operand =
                decoded.operands[instruction.operand_index];
            if (operand.kind == DecodedOperandKind::constant_pool_index)
            {
              auto constant = owner().constant(operand.constant_pool_index);
              if (constant &&
                  (*constant)->kind == classfile::ConstantKind::string_ref)
              {
                auto encoded = owner().string_constant(
                    operand.constant_pool_index);
                if (encoded)
                {
                  auto decoded_text = decode_modified_utf8(*encoded);
                  if (decoded_text)
                  {
                    auto candidate = utf32_from_utf16(*decoded_text);
                    if (translation::TranslationService::
                            contains_translatable_text(candidate) &&
                        std::find(output.begin(), output.end(), candidate) ==
                            output.end())
                    {
                      output.push_back(std::move(candidate));
                    }
                  }
                }
              }
            }
          }
          instruction_index = instruction.next_index;
          ++scanned;
        }
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

      [[nodiscard]] Result<u8> read_opcode()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        if (decoded_ != nullptr)
        {
          u32 instruction_index = decoded_index_;
          if (instruction_index >= decoded_->instructions.size() ||
              decoded_->instructions[instruction_index].bytecode_pc != pc_)
          {
            if (pc_ > std::numeric_limits<u32>::max())
            {
              return fail(ErrorCode::malformed_class,
                          "decoded bytecode PC exceeds index space");
            }
            instruction_index = decoded_->instruction_index_for_bci(
                static_cast<u32>(pc_));
          }
          if (instruction_index == kInvalidDecodedIndex ||
              instruction_index >= decoded_->instructions.size())
          {
            return fail(ErrorCode::malformed_class,
                        "decoded execution PC is not an instruction boundary");
          }
          const DecodedInstruction& instruction =
              decoded_->instructions[instruction_index];
          current_decoded_index_ = instruction_index;
          pc_ = static_cast<usize>(instruction.bytecode_pc) + 1U;
          decoded_index_ = instruction.next_index;
          PerformanceCounters::record_decoded_opcode_dispatch();
          return raw_opcode(instruction.opcode);
        }
#endif
        return read_u8();
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
        auto synchronized = synchronize_decoded_pc(handler_pc);
        if (!synchronized)
          return std::unexpected(synchronized.error());
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

      [[nodiscard]] Result<u16> read_constant_pool_index()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::constant_pool_index);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return (*decoded_operand)->constant_pool_index;
        }
#endif
        return read_u16();
      }

      [[nodiscard]] Result<InvokeInterfaceOperands>
      read_invokeinterface_operands()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::invokeinterface);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const InvokeInterfaceOperands result {
              .constant_pool_index = (*decoded_operand)->constant_pool_index,
              .count = (*decoded_operand)->auxiliary,
          };
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return result;
        }
#endif
        auto index = read_u16();
        auto count = read_u8();
        auto zero = read_u8();
        if (!index || !count || !zero || *count == 0U || *zero != 0U)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid invokeinterface operands");
        }
        return InvokeInterfaceOperands {
            .constant_pool_index = *index,
            .count = *count,
        };
      }

      [[nodiscard]] Result<i32> read_immediate(bool wide)
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::immediate);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const i32 value = (*decoded_operand)->immediate;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        if (wide)
        {
          auto value = read_i16();
          if (!value) return std::unexpected(value.error());
          return static_cast<i32>(*value);
        }
        auto value = read_i8();
        if (!value) return std::unexpected(value.error());
        return static_cast<i32>(*value);
      }

      [[nodiscard]] Result<u16> read_ldc_index(bool wide)
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::constant_pool_index);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const u16 value = (*decoded_operand)->constant_pool_index;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        if (wide) return read_u16();
        auto value = read_u8();
        if (!value) return std::unexpected(value.error());
        return static_cast<u16>(*value);
      }

      [[nodiscard]] Result<u16> read_local_index()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::local_index);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const u16 value = (*decoded_operand)->local_index;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        auto value = read_u8();
        if (!value) return std::unexpected(value.error());
        return static_cast<u16>(*value);
      }

      [[nodiscard]] Result<IncrementOperands> read_increment_operands()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::increment);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const IncrementOperands result {
              .local_index = (*decoded_operand)->local_index,
              .increment = (*decoded_operand)->immediate,
          };
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return result;
        }
#endif
        auto index = read_u8();
        auto increment = read_i8();
        if (!index || !increment)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated iinc instruction");
        }
        return IncrementOperands {
            .local_index = *index,
            .increment = *increment,
        };
      }

      [[nodiscard]] Result<i32> read_branch_offset(bool wide)
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::branch_target);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const i32 value = (*decoded_operand)->immediate;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        if (wide) return read_i32();
        auto value = read_i16();
        if (!value) return std::unexpected(value.error());
        return static_cast<i32>(*value);
      }

      [[nodiscard]] Result<const DecodedSwitchTable*> read_switch_table()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::switch_table);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const u32 switch_index = (*decoded_operand)->switch_index;
          if (switch_index >= decoded_->switches.size())
          {
            return fail(ErrorCode::malformed_class,
                        "decoded switch table index is invalid");
          }
          const DecodedSwitchTable* table = &decoded_->switches[switch_index];
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return table;
        }
#endif
        return static_cast<const DecodedSwitchTable*>(nullptr);
      }

      [[nodiscard]] Result<u16> read_invokedynamic_index()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::invokedynamic);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const u16 value = (*decoded_operand)->constant_pool_index;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        auto index = read_u16();
        auto zero1 = read_u8();
        auto zero2 = read_u8();
        if (!index || !zero1 || !zero2 || *zero1 != 0U || *zero2 != 0U)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid invokedynamic operands");
        }
        return *index;
      }

      [[nodiscard]] Result<u8> read_newarray_type()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::newarray_type);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const u8 value = (*decoded_operand)->auxiliary;
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return value;
        }
#endif
        return read_u8();
      }

      [[nodiscard]] Result<WideOperands> read_wide_operands()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand_unchecked();
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          if ((*decoded_operand)->kind != DecodedOperandKind::wide_local &&
              (*decoded_operand)->kind != DecodedOperandKind::wide_increment)
          {
            return fail(ErrorCode::malformed_class,
                        "decoded wide operand kind does not match opcode");
          }
          WideOperands result {
              .opcode = (*decoded_operand)->modified_opcode,
              .local_index = (*decoded_operand)->local_index,
              .increment = std::nullopt,
          };
          if ((*decoded_operand)->kind == DecodedOperandKind::wide_increment)
          {
            result.increment = static_cast<i16>((*decoded_operand)->immediate);
          }
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return result;
        }
#endif
        auto widened_opcode = read_u8();
        auto index = read_u16();
        if (!widened_opcode || !index)
        {
          return fail(ErrorCode::malformed_class,
                      "truncated wide instruction");
        }
        WideOperands result {
            .opcode = *widened_opcode,
            .local_index = *index,
            .increment = std::nullopt,
        };
        if (*widened_opcode == 0x84U)
        {
          auto increment = read_i16();
          if (!increment) return std::unexpected(increment.error());
          result.increment = *increment;
        }
        return result;
      }

      [[nodiscard]] Result<MultiArrayOperands> read_multianewarray_operands()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        auto decoded_operand = current_decoded_operand(
            DecodedOperandKind::multianewarray);
        if (!decoded_operand)
          return std::unexpected(decoded_operand.error());
        if (*decoded_operand != nullptr)
        {
          const MultiArrayOperands result {
              .constant_pool_index = (*decoded_operand)->constant_pool_index,
              .dimensions = (*decoded_operand)->auxiliary,
          };
          auto advanced = finish_decoded_operand();
          if (!advanced)
            return std::unexpected(advanced.error());
          PerformanceCounters::record_decoded_operand_dispatch();
          return result;
        }
#endif
        auto index = read_u16();
        auto dimensions = read_u8();
        if (!index || !dimensions || *dimensions == 0U)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid multianewarray operands");
        }
        return MultiArrayOperands {
            .constant_pool_index = *index,
            .dimensions = *dimensions,
        };
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
        // Legacy J2ME obfuscators sometimes leave arbitrary data in the
        // alignment gap. The bytes are not executable; consume them while
        // keeping the payload bounds checks strict.
        while ((pc_ & 3U) != 0U)
        {
          auto padding = read_u8();
          if (!padding)
          {
            return std::unexpected(padding.error());
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
        return synchronize_decoded_pc(target);
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
        return synchronize_decoded_pc(pc_);
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
      [[nodiscard]] Result<const DecodedOperand*>
      current_decoded_operand_unchecked() const
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        if (decoded_ == nullptr)
          return static_cast<const DecodedOperand*>(nullptr);
        if (current_decoded_index_ == kInvalidDecodedIndex ||
            current_decoded_index_ >= decoded_->instructions.size())
        {
          return fail(ErrorCode::malformed_class,
                      "decoded operand has no current instruction");
        }
        const DecodedInstruction& instruction =
            decoded_->instructions[current_decoded_index_];
        if (instruction.operand_index == kInvalidDecodedIndex ||
            instruction.operand_index >= decoded_->operands.size())
        {
          return fail(ErrorCode::malformed_class,
                      "decoded instruction has no operand metadata");
        }
        return &decoded_->operands[instruction.operand_index];
#else
        return static_cast<const DecodedOperand*>(nullptr);
#endif
      }

      [[nodiscard]] Result<const DecodedOperand*> current_decoded_operand(
          DecodedOperandKind expected_kind) const
      {
        auto operand = current_decoded_operand_unchecked();
        if (!operand)
          return std::unexpected(operand.error());
        if (*operand != nullptr && (*operand)->kind != expected_kind)
        {
          return fail(ErrorCode::malformed_class,
                      "decoded operand kind does not match opcode");
        }
        return *operand;
      }

      [[nodiscard]] Status finish_decoded_operand()
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        if (decoded_ == nullptr)
          return {};
        if (current_decoded_index_ == kInvalidDecodedIndex ||
            current_decoded_index_ >= decoded_->instructions.size())
        {
          return fail(ErrorCode::malformed_class,
                      "decoded operand cannot advance without an instruction");
        }
        const u32 next_index =
            decoded_->instructions[current_decoded_index_].next_index;
        if (next_index == decoded_->instructions.size())
        {
          pc_ = code_.size();
          return {};
        }
        if (next_index >= decoded_->instructions.size())
        {
          return fail(ErrorCode::malformed_class,
                      "decoded operand next index is outside method");
        }
        pc_ = decoded_->instructions[next_index].bytecode_pc;
#endif
        return {};
      }

      [[nodiscard]] Status synchronize_decoded_pc(usize target)
      {
#if PHONEME_ENABLE_DECODED_EXECUTION
        if (decoded_ != nullptr)
        {
          if (target > std::numeric_limits<u32>::max())
          {
            return fail(ErrorCode::malformed_class,
                        "decoded branch target exceeds index space");
          }
          const u32 instruction_index = decoded_->instruction_index_for_bci(
              static_cast<u32>(target));
          if (instruction_index == kInvalidDecodedIndex ||
              instruction_index >= decoded_->instructions.size())
          {
            return fail(ErrorCode::malformed_class,
                        "decoded branch target is not an instruction boundary");
          }
          decoded_index_ = instruction_index;
        }
#else
        (void)target;
#endif
        return {};
      }

      ExecutionFrame(ResolvedMethod resolved,
                     MethodDescriptor descriptor,
                     bool has_receiver)
          : resolved_(std::move(resolved)),
            descriptor_(std::move(descriptor)),
            has_receiver_(has_receiver),
            code_(resolved_.method->code->bytecode),
            decoded_(decoded_execution_requested() &&
                             resolved_.runtime != nullptr
                         ? resolved_.runtime->decoded.get()
                         : nullptr),
            locals_(resolved_.method->code->max_locals),
            stack_(resolved_.method->code->max_stack) {}

      ResolvedMethod resolved_;
      MethodDescriptor descriptor_;
      bool has_receiver_{false};
      std::span<const u8> code_;
      const DecodedMethod* decoded_{nullptr};
      [[maybe_unused]] u32 decoded_index_{0U};
      [[maybe_unused]] u32 current_decoded_index_{kInvalidDecodedIndex};
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
      : Machine(classes,
                HeapLimits {.maximum_objects = maximum_heap_objects})
  {
  }

  Machine::Machine(ClassRepository &classes, HeapLimits heap_limits)
      : classes_(classes), states_(classes),
        heap_(heap_limits_with_emergency_reserve(heap_limits)),
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
    // A worker may be waiting for another thread's class initializer. Wake it
    // before Scheduler::shutdown joins workers so teardown cannot deadlock on
    // the class-initialization condition.
    class_initialization_condition_.notify_all();
    scheduler_.shutdown(&monitors_);
  }

  void Machine::begin_character_translation_frame() noexcept
  {
    character_translation_capture_.clear();
    character_translation_replay_index_ = 0U;
    character_translation_replay_valid_ = true;
    translated_text_capture_.clear();
    translated_text_replay_index_ = 0U;
    translated_text_replay_valid_ = true;
    character_translation_frame_active_ = true;
    ++character_translation_run_generation_;
  }

  void Machine::break_character_translation_run() noexcept
  {
    if (character_translation_frame_active_)
      ++character_translation_run_generation_;
  }

  Machine::CharacterTranslationDecision Machine::translate_draw_character(
      u64 graphics_id,
      char32_t character,
      i32 x,
      i32 y,
      i32 anchor,
      i32 font_face,
      i32 font_style,
      i32 font_size)
  {
    CharacterTranslationDecision decision {
        .action = CharacterTranslationDecision::Action::draw_original,
        .translated = nullptr,
        .x = x,
        .y = y,
        .anchor = anchor,
    };
    const auto service = translation_service();
    if (!character_translation_frame_active_ || !service ||
        !service->enabled())
      return decision;

    const CharacterDrawSample sample {
        .graphics_id = graphics_id,
        .character = character,
        .x = x,
        .y = y,
        .anchor = anchor,
        .font_face = font_face,
        .font_style = font_style,
        .font_size = font_size,
        .run_generation = character_translation_run_generation_,
    };
    character_translation_capture_.push_back(sample);

    if (!character_translation_replay_valid_ ||
        character_translation_replay_index_ >=
            character_translation_plan_samples_.size())
    {
      character_translation_replay_valid_ = false;
      return decision;
    }

    const PlannedCharacterSample &planned =
        character_translation_plan_samples_[character_translation_replay_index_];
    const CharacterDrawSample &expected = planned.sample;
    constexpr i32 kCoordinateTolerance = 4;
    const bool matches =
        expected.graphics_id == sample.graphics_id &&
        expected.character == sample.character &&
        std::abs(expected.x - sample.x) <= kCoordinateTolerance &&
        std::abs(expected.y - sample.y) <= kCoordinateTolerance &&
        expected.anchor == sample.anchor &&
        expected.font_face == sample.font_face &&
        expected.font_style == sample.font_style &&
        expected.font_size == sample.font_size;
    ++character_translation_replay_index_;
    if (!matches)
    {
      character_translation_replay_valid_ = false;
      return decision;
    }
    if (planned.run_index == std::numeric_limits<usize>::max() ||
        planned.run_index >= character_translation_runs_.size())
      return decision;

    const CharacterRunPlan &run =
        character_translation_runs_[planned.run_index];
    auto translated = service->lookup_or_request(run.source);
    if (!translated)
      return decision;

    if (!planned.first_in_run)
    {
      decision.action = CharacterTranslationDecision::Action::suppress;
      return decision;
    }
    decision.action = CharacterTranslationDecision::Action::draw_translation;
    decision.translated = std::move(translated);
    decision.x = sample.x + (run.x - expected.x);
    decision.y = sample.y + (run.y - expected.y);
    decision.anchor = run.anchor;
    return decision;
  }

  void Machine::configure_image_draw_interceptor(ImageDrawSink sink)
  {
    const bool enabled = static_cast<bool>(sink);
    {
      std::scoped_lock lock(image_draw_sink_mutex_);
      image_draw_sink_ = std::move(sink);
    }
    image_draw_interception_enabled_.store(enabled,
                                           std::memory_order_release);
  }

  void Machine::intercept_image_draw(const ImageDrawEvent& event) const
  {
    if (!image_draw_interception_enabled_.load(std::memory_order_acquire))
      return;

    ImageDrawSink sink;
    {
      std::scoped_lock lock(image_draw_sink_mutex_);
      sink = image_draw_sink_;
    }
    if (sink) sink(event);
  }

  Machine::TextTranslationLayoutDecision Machine::plan_translated_text(
      u64 graphics_id,
      std::span<const char32_t> text,
      i32 x,
      i32 y,
      i32 anchor,
      i32 font_face,
      i32 font_style,
      i32 font_size,
      i32 clip_x,
      i32 clip_y,
      i32 clip_width,
      i32 clip_height,
      i32 translate_x,
      i32 translate_y)
  {
    auto created_font = graphics::Font::create(
        font_face, font_style, font_size);
    const graphics::Font font = created_font
        ? *created_font
        : graphics::Font::default_font();
    const i32 maximum_width = translated_available_width(
        x, anchor, clip_x, clip_width, translate_x);
    const i32 original_top = translated_text_top(
        y, anchor, font, translate_y);
    const i32 block_height = translated_text_height(
        font, text, maximum_width);
    const i32 safe_clip_height = std::max(clip_height, 0);
    const i32 clip_bottom = static_cast<i32>(std::clamp<i64>(
        static_cast<i64>(clip_y) + safe_clip_height,
        std::numeric_limits<i32>::min(),
        std::numeric_limits<i32>::max()));

    i32 planned_top = original_top;
    if (safe_clip_height <= 0 || block_height >= safe_clip_height)
      planned_top = clip_y;
    else
      planned_top = std::clamp(
          original_top, clip_y, clip_bottom - block_height);

    TextTranslationLayoutDecision decision {
        .planned = false,
        .y = translated_y_for_top(
            planned_top, block_height, anchor, font, translate_y),
        .height = block_height,
    };
    if (!character_translation_frame_active_ || text.empty())
      return decision;

    TranslatedTextDrawSample sample {
        .graphics_id = graphics_id,
        .text = std::vector<char32_t>(text.begin(), text.end()),
        .x = x,
        .y = y,
        .anchor = anchor,
        .font_face = font_face,
        .font_style = font_style,
        .font_size = font_size,
        .clip_x = clip_x,
        .clip_y = clip_y,
        .clip_width = clip_width,
        .clip_height = clip_height,
        .translate_x = translate_x,
        .translate_y = translate_y,
    };
    translated_text_capture_.push_back(sample);

    if (!translated_text_replay_valid_ ||
        translated_text_replay_index_ >= translated_text_plan_.size())
    {
      translated_text_replay_valid_ = false;
      return decision;
    }

    const PlannedTranslatedTextSample &planned =
        translated_text_plan_[translated_text_replay_index_];
    const TranslatedTextDrawSample &expected = planned.sample;
    constexpr i32 kCoordinateTolerance = 4;
    const bool matches =
        expected.graphics_id == sample.graphics_id &&
        expected.text == sample.text &&
        std::abs(expected.x - sample.x) <= kCoordinateTolerance &&
        std::abs(expected.y - sample.y) <= kCoordinateTolerance &&
        expected.anchor == sample.anchor &&
        expected.font_face == sample.font_face &&
        expected.font_style == sample.font_style &&
        expected.font_size == sample.font_size &&
        expected.clip_x == sample.clip_x &&
        expected.clip_y == sample.clip_y &&
        expected.clip_width == sample.clip_width &&
        expected.clip_height == sample.clip_height &&
        expected.translate_x == sample.translate_x &&
        expected.translate_y == sample.translate_y;
    ++translated_text_replay_index_;
    if (!matches)
    {
      translated_text_replay_valid_ = false;
      return decision;
    }

    decision.planned = true;
    decision.suppress = planned.suppress;
    decision.y = planned.y + (sample.y - expected.y);
    decision.height = planned.height;
    return decision;
  }

  void Machine::end_character_translation_frame()
  {
    if (!character_translation_frame_active_)
      return;
    character_translation_frame_active_ = false;

    const auto service = translation_service();
    if (!service || !service->enabled())
    {
      character_translation_capture_.clear();
      character_translation_plan_samples_.clear();
      character_translation_runs_.clear();
      translated_text_capture_.clear();
      translated_text_plan_.clear();
      return;
    }

    std::vector<PlannedCharacterSample> planned;
    planned.reserve(character_translation_capture_.size());
    for (const auto &sample : character_translation_capture_)
    {
      planned.push_back(PlannedCharacterSample {
          .sample = sample,
      });
    }

    std::vector<CharacterRunPlan> runs;
    usize begin = 0U;
    while (begin < character_translation_capture_.size())
    {
      usize end = begin + 1U;
      const CharacterDrawSample &first =
          character_translation_capture_[begin];
      i32 previous_x = first.x;
      i32 previous_y = first.y;
      const i32 maximum_horizontal_step =
          std::max<i32>(24, first.font_size * 4);
      const i32 maximum_vertical_step =
          std::max<i32>(20, maximum_horizontal_step);
      const i32 maximum_line_indent =
          std::max<i32>(48, maximum_horizontal_step * 2);
      while (end < character_translation_capture_.size())
      {
        const CharacterDrawSample &candidate =
            character_translation_capture_[end];
        const bool same_style =
            candidate.run_generation == first.run_generation &&
            candidate.graphics_id == first.graphics_id &&
            candidate.anchor == first.anchor &&
            candidate.font_face == first.font_face &&
            candidate.font_style == first.font_style &&
            candidate.font_size == first.font_size;
        const bool continues_line =
            candidate.y == previous_y &&
            std::abs(candidate.x - previous_x) <=
                maximum_horizontal_step;
        const bool starts_adjacent_line =
            candidate.y > previous_y &&
            candidate.y - previous_y <= maximum_vertical_step &&
            std::abs(candidate.x - first.x) <= maximum_line_indent;
        if (!same_style || (!continues_line && !starts_adjacent_line))
          break;
        previous_x = candidate.x;
        previous_y = candidate.y;
        ++end;
      }

      std::vector<char32_t> source;
      source.reserve((end - begin) + 2U);
      i32 source_line_y = first.y;
      for (usize index = begin; index < end; ++index)
      {
        const CharacterDrawSample &sample =
            character_translation_capture_[index];
        if (sample.y != source_line_y)
        {
          if (!source.empty() && source.back() != U'\n')
            source.push_back(U'\n');
          source_line_y = sample.y;
        }
        source.push_back(sample.character);
      }

      if (translation::TranslationService::contains_translatable_text(source))
      {
        const usize run_index = runs.size();
        runs.push_back(CharacterRunPlan {
            .first_sample = begin,
            .sample_count = end - begin,
            .x = first.x,
            .y = first.y,
            .anchor = first.anchor,
            .source = std::move(source),
        });
        for (usize index = begin; index < end; ++index)
        {
          planned[index].run_index = run_index;
          planned[index].first_in_run = index == begin;
        }
        (void)service->lookup_or_request(runs.back().source);
      }
      begin = end;
    }

    std::vector<PlannedTranslatedTextSample> text_plan;
    text_plan.reserve(translated_text_capture_.size());
    for (const auto &sample : translated_text_capture_)
    {
      auto created_font = graphics::Font::create(
          sample.font_face, sample.font_style, sample.font_size);
      const graphics::Font font = created_font
          ? *created_font
          : graphics::Font::default_font();
      text_plan.push_back(PlannedTranslatedTextSample {
          .sample = sample,
          .y = sample.y,
          .height = font.height(),
      });
    }

    // Many J2ME games build a text outline by drawing the exact same string
    // several times at neighbouring coordinates with different colours. Once
    // translated, rendering every pass creates several complete Vietnamese
    // strings on top of each other. Collapse each small outline/shadow cluster
    // to one central draw; draw_translated_text() supplies a readable outline.
    constexpr i32 kDuplicateTextRadius = 4;
    const auto resolved_anchor = [](i32 anchor) noexcept
    {
      return anchor == 0
          ? graphics::anchor_left | graphics::anchor_top
          : anchor;
    };
    const auto same_text_layer = [&](const TranslatedTextDrawSample &left,
                                     const TranslatedTextDrawSample &right)
    {
      const i32 left_anchor = resolved_anchor(left.anchor);
      const i32 right_anchor = resolved_anchor(right.anchor);
      return left.graphics_id == right.graphics_id &&
             left.text == right.text &&
             left.font_face == right.font_face &&
             left.font_style == right.font_style &&
             left.font_size == right.font_size &&
             left.clip_x == right.clip_x &&
             left.clip_y == right.clip_y &&
             left.clip_width == right.clip_width &&
             left.clip_height == right.clip_height &&
             left.translate_x == right.translate_x &&
             left.translate_y == right.translate_y &&
             left_anchor == right_anchor &&
             std::abs(left.x - right.x) <= kDuplicateTextRadius &&
             std::abs(left.y - right.y) <= kDuplicateTextRadius;
    };

    std::vector<bool> duplicate_grouped(
        translated_text_capture_.size(), false);
    for (usize root = 0U; root < translated_text_capture_.size(); ++root)
    {
      if (duplicate_grouped[root])
        continue;
      std::vector<usize> group {root};
      duplicate_grouped[root] = true;
      for (usize candidate = root + 1U;
           candidate < translated_text_capture_.size();
           ++candidate)
      {
        if (!duplicate_grouped[candidate] &&
            same_text_layer(translated_text_capture_[root],
                            translated_text_capture_[candidate]))
        {
          duplicate_grouped[candidate] = true;
          group.push_back(candidate);
        }
      }
      if (group.size() <= 1U)
        continue;

      usize preferred = group.front();
      i64 best_distance = std::numeric_limits<i64>::max();
      for (const usize candidate : group)
      {
        i64 distance = 0;
        for (const usize other : group)
        {
          distance += std::abs(
              static_cast<i64>(translated_text_capture_[candidate].x) -
              translated_text_capture_[other].x);
          distance += std::abs(
              static_cast<i64>(translated_text_capture_[candidate].y) -
              translated_text_capture_[other].y);
        }
        // Prefer the last central pass on a tie. Games conventionally render
        // shadows first and the foreground colour last.
        if (distance < best_distance ||
            (distance == best_distance && candidate > preferred))
        {
          best_distance = distance;
          preferred = candidate;
        }
      }
      for (const usize index : group)
      {
        if (index != preferred)
          text_plan[index].suppress = true;
      }
    }

    std::vector<usize> visible_text_indices;
    visible_text_indices.reserve(translated_text_capture_.size());
    for (usize index = 0U; index < translated_text_capture_.size(); ++index)
    {
      if (!text_plan[index].suppress)
        visible_text_indices.push_back(index);
    }

    usize visible_begin = 0U;
    while (visible_begin < visible_text_indices.size())
    {
      const usize first_index = visible_text_indices[visible_begin];
      const TranslatedTextDrawSample &first =
          translated_text_capture_[first_index];
      const i32 first_anchor = resolved_anchor(first.anchor);
      const i32 first_horizontal = first_anchor &
          (graphics::anchor_left | graphics::anchor_right |
           graphics::anchor_hcenter);
      const i32 first_vertical = first_anchor &
          (graphics::anchor_top | graphics::anchor_bottom |
           graphics::anchor_baseline);
      usize visible_end = visible_begin + 1U;
      i64 previous_y = static_cast<i64>(first.y) + first.translate_y;
      const i32 maximum_gap = 48;
      const i32 maximum_indent = std::max<i32>(
          64, std::max(first.clip_width, 1) / 2);
      while (visible_end < visible_text_indices.size())
      {
        const usize candidate_index = visible_text_indices[visible_end];
        const TranslatedTextDrawSample &candidate =
            translated_text_capture_[candidate_index];
        const i32 candidate_anchor = resolved_anchor(candidate.anchor);
        const i32 candidate_horizontal = candidate_anchor &
            (graphics::anchor_left | graphics::anchor_right |
             graphics::anchor_hcenter);
        const i32 candidate_vertical = candidate_anchor &
            (graphics::anchor_top | graphics::anchor_bottom |
             graphics::anchor_baseline);
        const i64 candidate_y =
            static_cast<i64>(candidate.y) + candidate.translate_y;
        const bool same_clip =
            candidate.graphics_id == first.graphics_id &&
            candidate.clip_x == first.clip_x &&
            candidate.clip_y == first.clip_y &&
            candidate.clip_width == first.clip_width &&
            candidate.clip_height == first.clip_height &&
            candidate.translate_x == first.translate_x &&
            candidate.translate_y == first.translate_y &&
            candidate_horizontal == first_horizontal &&
            candidate_vertical == first_vertical;
        const bool adjacent =
            candidate_y > previous_y &&
            candidate_y - previous_y <= maximum_gap &&
            std::abs(candidate.x - first.x) <= maximum_indent;
        if (!same_clip || !adjacent)
          break;
        previous_y = candidate_y;
        ++visible_end;
      }

      const usize block_count = visible_end - visible_begin;
      std::vector<graphics::Font> fonts;
      std::vector<i32> heights(block_count, 1);
      std::vector<i32> original_tops(block_count, 0);
      std::vector<i32> planned_tops(block_count, 0);
      std::vector<i32> gaps(block_count, 1);
      fonts.reserve(block_count);

      for (usize visible_index = visible_begin;
           visible_index < visible_end;
           ++visible_index)
      {
        const usize index = visible_text_indices[visible_index];
        const TranslatedTextDrawSample &sample =
            translated_text_capture_[index];
        const usize local_index = visible_index - visible_begin;
        auto created_font = graphics::Font::create(
            sample.font_face, sample.font_style, sample.font_size);
        const graphics::Font font = created_font
            ? *created_font
            : graphics::Font::default_font();
        fonts.push_back(font);
        const i32 maximum_width = translated_available_width(
            sample.x,
            sample.anchor,
            sample.clip_x,
            sample.clip_width,
            sample.translate_x);
        heights[local_index] = translated_text_height(
            font, sample.text, maximum_width);
        original_tops[local_index] = translated_text_top(
            sample.y, sample.anchor, font, sample.translate_y);
        if (local_index > 0U)
        {
          const i32 natural_gap =
              original_tops[local_index] -
              (original_tops[local_index - 1U] +
               fonts[local_index - 1U].height());
          const i32 maximum_natural_gap = std::max<i32>(
              1,
              std::min(font.height(),
                       fonts[local_index - 1U].height()) / 2);
          gaps[local_index] = std::clamp(
              natural_gap, 1, maximum_natural_gap);
        }
      }

      const i32 clip_top = first.clip_y;
      const i32 safe_clip_height = std::max(first.clip_height, 0);
      const i32 clip_bottom = static_cast<i32>(std::clamp<i64>(
          static_cast<i64>(clip_top) + safe_clip_height,
          std::numeric_limits<i32>::min(),
          std::numeric_limits<i32>::max()));

      planned_tops.front() = original_tops.front();
      for (usize local_index = 1U; local_index < block_count; ++local_index)
      {
        const i64 minimum_top =
            static_cast<i64>(planned_tops[local_index - 1U]) +
            heights[local_index - 1U] + gaps[local_index];
        const i32 pushed_top = static_cast<i32>(std::clamp<i64>(
            minimum_top,
            std::numeric_limits<i32>::min(),
            std::numeric_limits<i32>::max()));
        planned_tops[local_index] =
            std::max(original_tops[local_index], pushed_top);
      }

      auto shift_all = [&](i64 delta)
      {
        for (i32 &top : planned_tops)
        {
          top = static_cast<i32>(std::clamp<i64>(
              static_cast<i64>(top) + delta,
              std::numeric_limits<i32>::min(),
              std::numeric_limits<i32>::max()));
        }
      };
      auto planned_bottom = [&]() -> i64
      {
        return static_cast<i64>(planned_tops.back()) + heights.back();
      };

      if (planned_bottom() > clip_bottom)
        shift_all(static_cast<i64>(clip_bottom) - planned_bottom());

      if (planned_tops.front() < clip_top && planned_bottom() < clip_bottom)
      {
        const i64 shift_down = std::min<i64>(
            static_cast<i64>(clip_top) - planned_tops.front(),
            static_cast<i64>(clip_bottom) - planned_bottom());
        shift_all(shift_down);
      }

      for (usize visible_index = visible_begin;
           visible_index < visible_end;
           ++visible_index)
      {
        const usize index = visible_text_indices[visible_index];
        const usize local_index = visible_index - visible_begin;
        const TranslatedTextDrawSample &sample =
            translated_text_capture_[index];
        text_plan[index].y = translated_y_for_top(
            planned_tops[local_index],
            heights[local_index],
            sample.anchor,
            fonts[local_index],
            sample.translate_y);
        text_plan[index].height = heights[local_index];
      }
      visible_begin = visible_end;
    }

    character_translation_plan_samples_ = std::move(planned);
    character_translation_runs_ = std::move(runs);
    character_translation_capture_.clear();
    translated_text_plan_ = std::move(text_plan);
    translated_text_capture_.clear();
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

  Result<ExecutionResult> Machine::run_java_thread_entry(ObjectRef thread)
  {
    auto runtime_class = heap_.class_name(thread);
    if (!runtime_class)
      return std::unexpected(runtime_class.error());
    auto run_method = classes_.resolve_method(*runtime_class, "run", "()V");
    if (!run_method)
      return std::unexpected(run_method.error());

    // The built-in java.lang.Thread.run native only forwards to the stored
    // Runnable target. Enter that target directly from the scheduler so a
    // normal Thread(Runnable) does not consume an extra host C++ execute frame.
    // A subclass override must still run virtually and retains Java semantics.
    if (run_method->owner != nullptr &&
        run_method->owner->name() == "java/lang/Thread")
    {
      auto target = scheduler_.runnable_target(thread);
      if (!target)
        return std::unexpected(target.error());
      if (target->is_null())
      {
        return ExecutionResult {
            .return_value = std::nullopt,
            .throwable = std::nullopt,
            .executed_instructions = 0U,
        };
      }
      return invoke_instance(
          *target,
          "java/lang/Runnable",
          "run",
          "()V",
          {},
          kLongLivedThreadInstructionBudget);
    }

    return invoke_instance(
        thread,
        "java/lang/Thread",
        "run",
        "()V",
        {},
        kLongLivedThreadInstructionBudget);
  }

  Result<std::optional<Value>> Machine::run_java_thread_target(ObjectRef thread)
  {
    auto target = scheduler_.runnable_target(thread);
    if (!target)
      return std::unexpected(target.error());
    if (target->is_null())
      return std::optional<Value>{};
    auto result = invoke_instance(
        *target,
        "java/lang/Runnable",
        "run",
        "()V",
        {},
        kLongLivedThreadInstructionBudget);
    if (!result)
      return std::unexpected(result.error());
    if (result->throwable.has_value())
    {
      auto class_name = heap_.class_name(*result->throwable);
      if (!class_name)
        return std::unexpected(class_name.error());

      std::string message;
      auto detail_value = heap_.field(*result->throwable, 0U);
      if (detail_value)
      {
        auto detail_reference = detail_value->as_reference();
        if (detail_reference && !detail_reference->is_null())
        {
          auto detail = heap_.string_value(*detail_reference);
          if (detail)
          {
            message.reserve(detail->size());
            for (char16_t character : *detail)
            {
              message.push_back(character <= 0x7fU
                  ? static_cast<char>(character)
                  : '?');
            }
          }
        }
      }
      if (message.empty())
        message = "uncaught throwable from Runnable.run";
      if (!result->exception_context.empty())
      {
        message += " at ";
        message += result->exception_context;
      }
      return fail_java(*class_name, message);
    }
    return std::optional<Value>{};
  }

  void Machine::configure_frame_pacing(
      i32 frames_per_second,
      FramePacingMode mode) noexcept
  {
    scheduler_.configure_frame_pacing(frames_per_second, mode);
  }

  void Machine::pace_frame_publication()
  {
    scheduler_.pace_current_frame_publication(*this);
  }

  void Machine::note_frame_pacing_boundary() noexcept
  {
    scheduler_.note_current_frame_boundary();
  }

  void Machine::break_frame_pacing_sequence() noexcept
  {
    scheduler_.break_current_frame_pacing_sequence();
  }

  Result<SchedulerWaitResult> Machine::sleep_current_thread(i64 millis)
  {
    if (millis < 0)
      return fail_java("java/lang/IllegalArgumentException",
                       "Thread.sleep timeout must not be negative");
    return scheduler_.sleep_current(
        *this,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds(millis)));
  }

  Result<SchedulerWaitResult> Machine::join_java_thread(
      ObjectRef thread,
      std::optional<i64> millis)
  {
    scheduler_.break_current_frame_pacing_sequence();
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
    scheduler_.break_current_frame_pacing_sequence();
    if (millis.has_value() && *millis < 0)
      return fail_java("java/lang/IllegalArgumentException",
                       "Object.wait timeout must not be negative");
    std::optional<std::chrono::milliseconds> timeout;
    if (millis.has_value() && *millis > 0)
      timeout = std::chrono::milliseconds(*millis);

    u32 released_depth = 0U;
    std::optional<Error> blocking_pump_error;
    const bool pump_timed_wait = timeout.has_value() && canvas_bridge_ != nullptr;
    auto result = monitors_.wait(
        object,
        scheduler_.current_thread_id(),
        timeout,
        [this, &released_depth, &blocking_pump_error, pump_timed_wait] {
          scheduler_.set_current_state(JavaThreadState::waiting);
          released_depth = suspend_execution_for_blocking();
          if (pump_timed_wait)
          {
            auto pumped = canvas_bridge_->pump_blocking_wait_work();
            if (!pumped)
              blocking_pump_error = pumped.error();
          }
        },
        [this, &released_depth] {
          resume_execution_after_blocking(released_depth);
          scheduler_.set_current_state(JavaThreadState::running);
        },
        [this] {
          // Shutdown may race with a Java thread that consumes its interrupt
          // and then enters Object.wait after MonitorTable::clear() has already
          // notified the old monitors. The scheduler stop flag is persistent;
          // include it in the wait predicate so no new indefinite wait can be
          // created while workers are being joined.
          return scheduler_.current_is_interrupted() ||
                 scheduler_.current_stop_requested();
        });
    if (blocking_pump_error.has_value())
      return std::unexpected(std::move(*blocking_pump_error));
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

  Status Machine::enter_external_execution()
  {
    if (g_execution_machine != nullptr && g_execution_machine != this)
    {
      return fail(ErrorCode::invalid_state,
                  "a host thread cannot execute two Machine instances recursively");
    }
    execution_mutex_.lock();
    g_execution_machine = this;
    ++g_execution_lock_depth;
    return {};
  }

  bool Machine::try_enter_external_execution() noexcept
  {
    if (g_execution_machine != nullptr && g_execution_machine != this)
      return false;
    if (!execution_mutex_.try_lock())
      return false;
    g_execution_machine = this;
    ++g_execution_lock_depth;
    return true;
  }

  void Machine::leave_external_execution() noexcept
  {
    if (g_execution_machine != this || g_execution_lock_depth == 0U)
      std::abort();
    --g_execution_lock_depth;
    execution_mutex_.unlock();
    if (g_execution_lock_depth == 0U)
      g_execution_machine = nullptr;
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

    // The recursive mutex is now physically unowned by this host thread, so
    // its TLS execution state must say the same thing. Keeping the old logical
    // depth here is unsafe when a blocking hook pumps Canvas work: that nested
    // execution acquires the mutex from depth zero, but a nested sleep/wait
    // would try to unlock the stale outer depth as well and corrupt the gate.
    g_execution_lock_depth = 0U;
    g_execution_machine = nullptr;
    return depth;
  }

  void Machine::resume_execution_after_blocking(u32 depth) noexcept
  {
    if (depth == 0U)
      return;
    if (g_execution_machine != nullptr || g_execution_lock_depth != 0U)
      std::abort();
    for (u32 index = 0U; index < depth; ++index)
      execution_mutex_.lock();
    g_execution_machine = this;
    g_execution_lock_depth = depth;
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
    if (serial_callback_coalescing_)
    {
      for (const auto &queued : serial_callbacks_)
      {
        auto queued_runnable = queued.get();
        if (queued_runnable && *queued_runnable == runnable)
          return {};
      }
    }
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
    if (maximum_callbacks == 0U)
      return {};

    NativeRootScope worker_thread_root;
    ObjectRef first_runnable {};
    {
      std::scoped_lock lock(serial_callbacks_mutex_);
      if (serial_callback_failure_.has_value())
      {
        Error failure = std::move(*serial_callback_failure_);
        serial_callback_failure_.reset();
        return std::unexpected(std::move(failure));
      }
      if (serial_callback_worker_running_ || serial_callbacks_.empty())
        return {};
      auto first = serial_callbacks_.front().get();
      if (!first)
        return std::unexpected(first.error());
      first_runnable = *first;
      serial_callback_worker_running_ = true;
    }

    auto allocated_thread = allocate_pinned_instance("java/lang/Thread");
    if (!allocated_thread)
    {
      std::scoped_lock lock(serial_callbacks_mutex_);
      serial_callback_worker_running_ = false;
      return std::unexpected(allocated_thread.error());
    }
    worker_thread_root = std::move(*allocated_thread);
    auto worker_thread = worker_thread_root.get();
    if (!worker_thread)
    {
      std::scoped_lock lock(serial_callbacks_mutex_);
      serial_callback_worker_running_ = false;
      return std::unexpected(worker_thread.error());
    }
    auto initialized = initialize_java_thread(*worker_thread, first_runnable);
    if (!initialized)
    {
      std::scoped_lock lock(serial_callbacks_mutex_);
      serial_callback_worker_running_ = false;
      return std::unexpected(initialized.error());
    }

    const ObjectRef worker_thread_object = *worker_thread;
    auto scheduled = scheduler_.start_native_thread(
        *this,
        worker_thread_object,
        [this, maximum_callbacks](std::stop_token stop_token)
            -> Result<std::optional<ObjectRef>> {
          const auto finish_worker = [this](std::optional<Error> failure = {}) {
            std::scoped_lock lock(serial_callbacks_mutex_);
            serial_callback_failure_ = std::move(failure);
            serial_callback_worker_running_ = false;
          };

          for (usize delivered = 0U;
               delivered < maximum_callbacks &&
               !stop_token.stop_requested();
               ++delivered)
          {
            NativeRootScope callback;
            {
              std::scoped_lock lock(serial_callbacks_mutex_);
              if (serial_callbacks_.empty())
              {
                serial_callback_worker_running_ = false;
                return std::optional<ObjectRef> {};
              }
              callback = std::move(serial_callbacks_.front());
              serial_callbacks_.pop_front();
            }

            auto runnable = callback.get();
            if (!runnable)
            {
              finish_worker(runnable.error());
              return std::optional<ObjectRef> {};
            }
            // Display.callSerially belongs to the LCDUI event thread. Running
            // the callback inline in the host pump lets a long resource loader
            // or game loop block frame delivery and the entire native UI. The
            // scheduler worker preserves callback ordering while allowing the
            // host to keep pumping Canvas frames and input concurrently.
            constexpr u64 kSerialCallbackInstructionBudget = 200'000'000U;
            auto result = invoke_instance(*runnable,
                                          "java/lang/Runnable",
                                          "run",
                                          "()V",
                                          {},
                                          kSerialCallbackInstructionBudget);
            if (!result)
            {
              finish_worker(result.error());
              return std::optional<ObjectRef> {};
            }
            if (result->completed_normally())
              continue;
            if (!result->throwable.has_value())
            {
              finish_worker(Error::make(
                  ErrorCode::internal_error,
                  "LCDUI serial callback failed without throwable"));
              return std::optional<ObjectRef> {};
            }
            auto throwable = heap_.class_name(*result->throwable);
            if (!throwable)
            {
              finish_worker(throwable.error());
              return std::optional<ObjectRef> {};
            }
            std::string diagnostic =
                "LCDUI serial callback threw " + *throwable;
            if (!result->exception_context.empty())
            {
              diagnostic += " from ";
              diagnostic += result->exception_context;
            }
            finish_worker(Error::make_java(*throwable,
                                           std::move(diagnostic)));
            return std::optional<ObjectRef> {};
          }

          finish_worker();
          return std::optional<ObjectRef> {};
        });
    if (!scheduled)
    {
      std::scoped_lock lock(serial_callbacks_mutex_);
      serial_callback_worker_running_ = false;
      return std::unexpected(scheduled.error());
    }
    return {};
  }

  usize Machine::pending_serial_callbacks() const noexcept
  {
    std::scoped_lock lock(serial_callbacks_mutex_);
    return serial_callbacks_.size() +
           (serial_callback_worker_running_ ? 1U : 0U);
  }

  void Machine::set_serial_callback_coalescing(bool enabled) noexcept
  {
    std::scoped_lock lock(serial_callbacks_mutex_);
    serial_callback_coalescing_ = enabled;
    if (!enabled || serial_callbacks_.size() < 2U)
      return;

    std::unordered_set<u64> retained;
    std::deque<NativeRootScope> compacted;
    while (!serial_callbacks_.empty())
    {
      auto callback = std::move(serial_callbacks_.front());
      serial_callbacks_.pop_front();
      auto runnable = callback.get();
      if (!runnable || runnable->is_null() ||
          !retained.insert(runnable->bits).second)
      {
        continue;
      }
      compacted.push_back(std::move(callback));
    }
    serial_callbacks_ = std::move(compacted);
  }

  Status Machine::schedule_lcdui_alert_timeout(ObjectRef alert,
                                                i32 timeout_millis)
  {
    if (alert.is_null() || timeout_millis <= 0)
    {
      return fail(ErrorCode::invalid_argument,
                  "LCDUI alert timeout schedule is invalid");
    }
    auto root = pin_native_root(alert);
    if (!root)
      return std::unexpected(root.error());
    std::scoped_lock lock(lcd_ui_alert_timeout_mutex_);
    lcd_ui_alert_timeout_root_ = std::move(*root);
    lcd_ui_alert_timeout_deadline_ =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_millis);
    return {};
  }

  void Machine::cancel_lcdui_alert_timeout(ObjectRef alert) noexcept
  {
    std::scoped_lock lock(lcd_ui_alert_timeout_mutex_);
    if (!lcd_ui_alert_timeout_root_.has_value())
      return;
    if (!alert.is_null())
    {
      auto scheduled = lcd_ui_alert_timeout_root_->get();
      if (!scheduled || *scheduled != alert)
        return;
    }
    lcd_ui_alert_timeout_root_.reset();
    lcd_ui_alert_timeout_deadline_ = {};
  }

  Result<std::optional<ObjectRef>> Machine::take_due_lcdui_alert()
  {
    std::scoped_lock lock(lcd_ui_alert_timeout_mutex_);
    if (!lcd_ui_alert_timeout_root_.has_value() ||
        std::chrono::steady_clock::now() < lcd_ui_alert_timeout_deadline_)
    {
      return std::optional<ObjectRef> {};
    }
    auto scheduled = lcd_ui_alert_timeout_root_->get();
    if (!scheduled)
      return std::unexpected(scheduled.error());
    const ObjectRef alert = *scheduled;
    lcd_ui_alert_timeout_root_.reset();
    lcd_ui_alert_timeout_deadline_ = {};
    return std::optional<ObjectRef>(alert);
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

  void Machine::set_system_property(std::u16string key,
                                    std::u16string value)
  {
    if (key.empty())
      return;
    system_properties_.insert_or_assign(std::move(key), std::move(value));
  }

  std::optional<std::u16string> Machine::configured_system_property(
      std::u16string_view key) const
  {
    const auto property = system_properties_.find(std::u16string(key));
    if (property == system_properties_.end())
      return std::nullopt;
    return property->second;
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
    MidletSignal current = midlet_signal_.load(std::memory_order_acquire);
    for (;;)
    {
      if (current == MidletSignal::destroyed &&
          signal != MidletSignal::destroyed)
        return;
      if (midlet_signal_.compare_exchange_weak(
              current,
              signal,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
        return;
    }
  }

  MidletSignal Machine::consume_midlet_signal() noexcept
  {
    return midlet_signal_.exchange(MidletSignal::none,
                                   std::memory_order_acq_rel);
  }

  bool Machine::consume_midlet_destroyed_signal() noexcept
  {
    MidletSignal expected = MidletSignal::destroyed;
    return midlet_signal_.compare_exchange_strong(
        expected,
        MidletSignal::none,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
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
      u64 instruction_budget,
      InstructionBudgetMode budget_mode)
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
    const auto lambda = lambda_bindings_.find(receiver.bits);
    const bool lambda_descriptor_matches =
        lambda != lambda_bindings_.end() &&
        (lambda->second.sam_descriptor == descriptor ||
         std::find(lambda->second.bridge_descriptors.begin(),
                   lambda->second.bridge_descriptors.end(),
                   descriptor) != lambda->second.bridge_descriptors.end());
    if (lambda != lambda_bindings_.end() &&
        lambda->second.interface_name == declared_class &&
        lambda->second.sam_name == method_name &&
        lambda_descriptor_matches)
    {
      std::optional<ObjectRef> constructor_receiver;
      std::optional<NativeRootScope> constructor_receiver_root;
      if (lambda->second.implementation_kind == 8U)
      {
        auto allocated = allocate_pinned_instance(
            lambda->second.implementation.owner);
        if (!allocated)
          return std::unexpected(allocated.error());
        constructor_receiver_root.emplace(std::move(*allocated));
        auto reference = constructor_receiver_root->get();
        if (!reference)
          return std::unexpected(reference.error());
        constructor_receiver = *reference;
      }
      auto invocation = prepare_lambda_invocation(receiver,
                                                  lambda->second,
                                                  arguments,
                                                  constructor_receiver);
      if (!invocation)
        return std::unexpected(invocation.error());
      return execute(std::move(*invocation), instruction_budget, budget_mode);
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
    return execute(std::move(*invocation), instruction_budget, budget_mode);
  }

  void Machine::refresh_metadata_bindings_if_needed() noexcept
  {
    // Interpreter metadata caches are only touched while the VM execution gate
    // is held. Native bindings are also resolved by host/native callback
    // threads before they enter execute(), so that cache has its own mutex and
    // is refreshed in resolve_native_binding().
    const u64 metadata_generation = classes_.metadata().generation();
    if (metadata_binding_generation_ != metadata_generation)
    {
      field_bindings_.clear();
      direct_call_bindings_.clear();
      virtual_call_bindings_.clear();
      operand_resolution_method_id_ = {};
      operand_resolution_method_.reset();
      metadata_binding_generation_ = metadata_generation;
    }
  }

  Result<OperandResolutionEntry*>
  Machine::operand_resolution_entry(MethodId method_id,
                                    u32 operand_index,
                                    usize bytecode_pc)
  {
    if (!method_id.valid() || operand_index == kInvalidDecodedIndex ||
        bytecode_pc > static_cast<usize>(std::numeric_limits<u32>::max()))
    {
      return fail(ErrorCode::invalid_argument,
                  "decoded operand resolution requires valid IDs and BCI");
    }
    std::shared_ptr<const RuntimeMethod> runtime_method;
    if (operand_resolution_method_ != nullptr &&
        operand_resolution_method_id_ == method_id)
    {
      runtime_method = operand_resolution_method_;
    }
    else
    {
      runtime_method = classes_.metadata().find_method(method_id);
      operand_resolution_method_id_ = method_id;
      operand_resolution_method_ = runtime_method;
    }
    if (runtime_method == nullptr || runtime_method->decoded == nullptr ||
        runtime_method->operand_resolutions == nullptr)
    {
      return fail(ErrorCode::invalid_state,
                  "decoded operand resolution has no runtime method table");
    }
    return runtime_method->operand_resolutions->entry(
        operand_index,
        static_cast<u32>(bytecode_pc));
  }

  Result<std::shared_ptr<const classfile::ClassFile>>
  Machine::load_linkage_class(std::string_view class_name)
  {
    auto loaded = classes_.load(class_name);
    if (!loaded)
      return std::unexpected(loaded.error());

    std::string_view component = class_name;
    while (!component.empty() && component.front() == '[')
      component.remove_prefix(1U);
    if (component.size() >= 3U && component.front() == 'L' &&
        component.back() == ';')
    {
      component.remove_prefix(1U);
      component.remove_suffix(1U);
      auto loaded_component = classes_.load(component);
      if (!loaded_component)
        return std::unexpected(loaded_component.error());
    }
    return *loaded;
  }

  Result<OperandResolutionEntry*> Machine::resolve_class_operand(
      MethodId method_id,
      u32 operand_index,
      usize bytecode_pc,
      const classfile::ClassFile& owner,
      u16 constant_pool_index,
      bool load_target_class,
      bool derive_reference_array_name)
  {
    if (!method_id.valid() || operand_index == kInvalidDecodedIndex)
      return static_cast<OperandResolutionEntry*>(nullptr);

    auto cached_entry = operand_resolution_entry(method_id,
                                                 operand_index,
                                                 bytecode_pc);
    if (!cached_entry)
      return std::unexpected(cached_entry.error());
    OperandResolutionEntry& entry = **cached_entry;
    if (entry.state == OperandResolutionState::resolved)
    {
      if (entry.kind != OperandResolutionKind::class_reference ||
          entry.target_class_name.empty() ||
          (load_target_class && entry.target_class_file == nullptr) ||
          (derive_reference_array_name && entry.target_array_name.empty()))
      {
        return fail(ErrorCode::internal_error,
                    "decoded class operand cache has wrong kind");
      }
      PerformanceCounters::record_operand_resolution(true);
      return &entry;
    }
    if (entry.state == OperandResolutionState::failed)
    {
      if (entry.kind != OperandResolutionKind::class_reference ||
          !entry.failure.has_value())
      {
        return fail(ErrorCode::internal_error,
                    "decoded failed class operand has no error");
      }
      PerformanceCounters::record_operand_resolution(true);
      PerformanceCounters::record_operand_resolution_failure();
      return std::unexpected(*entry.failure);
    }
    if (entry.state == OperandResolutionState::resolving)
    {
      return fail(ErrorCode::invalid_state,
                  "recursive decoded class operand resolution");
    }

    PerformanceCounters::record_operand_resolution(false);
    auto began = entry.begin(OperandResolutionKind::class_reference);
    if (!began)
      return std::unexpected(began.error());

    auto class_name = owner.class_name_constant(constant_pool_index);
    if (!class_name)
    {
      auto cached = entry.fail_resolution(stable_linkage_error(
          class_name.error(), OperandResolutionKind::class_reference));
      if (!cached)
        return std::unexpected(cached.error());
      PerformanceCounters::record_operand_resolution_failure();
      return std::unexpected(*entry.failure);
    }

    std::shared_ptr<const classfile::ClassFile> loaded_class;
    ClassId class_id;
    if (load_target_class)
    {
      auto loaded = load_linkage_class(*class_name);
      if (!loaded)
      {
        auto cached = entry.fail_resolution(stable_linkage_error(
            loaded.error(), OperandResolutionKind::class_reference));
        if (!cached)
          return std::unexpected(cached.error());
        PerformanceCounters::record_operand_resolution_failure();
        return std::unexpected(*entry.failure);
      }
      loaded_class = *loaded;
      const auto runtime_class = classes_.metadata().find_class(*class_name);
      if (runtime_class == nullptr)
      {
        auto cached = entry.fail_resolution(Error::make(
            ErrorCode::internal_error,
            "loaded decoded class operand has no runtime metadata"));
        if (!cached)
          return std::unexpected(cached.error());
        PerformanceCounters::record_operand_resolution_failure();
        return std::unexpected(*entry.failure);
      }
      class_id = runtime_class->id;
    }

    std::string array_name;
    if (derive_reference_array_name)
    {
      if (class_name->starts_with('['))
        array_name = '[' + *class_name;
      else
        array_name = "[L" + *class_name + ';';
    }
    auto completed = entry.resolve_class_reference(
        std::move(*class_name),
        class_id,
        std::move(loaded_class),
        std::move(array_name));
    if (!completed)
      return std::unexpected(completed.error());
    return &entry;
  }

  NativeMethodBinding Machine::resolve_native_binding(
      const classfile::ClassFile& owner,
      const classfile::Method& method)
  {
    std::scoped_lock cache_lock(native_bindings_mutex_);
    const u64 registry_generation = natives_.generation();
    if (native_binding_generation_ != registry_generation)
    {
      native_bindings_.clear();
      native_binding_generation_ = registry_generation;
    }
    if (const auto cached = native_bindings_.find(&method);
        cached != native_bindings_.end())
    {
      return NativeMethodBinding {
          .id = cached->second,
          .generation = native_binding_generation_,
      };
    }

    const NativeMethodBinding binding = natives_.resolve_binding(
        owner.name(), method.name, method.descriptor);
    if (native_binding_generation_ != binding.generation)
    {
      native_bindings_.clear();
      native_binding_generation_ = binding.generation;
    }
    native_bindings_.insert_or_assign(&method, binding.id);
    return binding;
  }

  Result<Machine::Invocation> Machine::prepare_invocation(
      ResolvedMethod method,
      std::span<const Value> arguments,
      bool has_receiver,
      std::optional<NativeMethodId> prebound_native_method)
  {
    if (method.method == nullptr)
    {
      return fail(ErrorCode::method_not_found,
                  "resolved method is null");
    }
    std::shared_ptr<const CachedMethodDescriptor> descriptor =
        method.runtime != nullptr ? method.runtime->descriptor : nullptr;
    if (descriptor == nullptr)
    {
      auto cached = classes_.metadata().method_descriptor(
          method.method->descriptor);
      if (!cached)
      {
        return std::unexpected(cached.error());
      }
      descriptor = std::move(*cached);
    }

    const usize expected_values = descriptor->argument_values +
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
    for (usize index = 0;
         index < descriptor->descriptor.parameters.size();
         ++index)
    {
      const Value &value = arguments[index + (has_receiver ? 1U : 0U)];
      if (!value_matches(value, descriptor->descriptor.parameters[index]))
      {
        return fail(ErrorCode::invalid_argument,
                    "method argument does not match its descriptor");
      }
    }

    NativeMethodId native_method;
    if (prebound_native_method.has_value())
    {
      native_method = *prebound_native_method;
    }
    else if (method.owner != nullptr)
    {
      native_method = resolve_native_binding(
          *method.owner, *method.method).id;
    }

    PerformanceCounters::record_method_invocation();
    return Invocation{
        .method = std::move(method),
        .descriptor = std::move(descriptor),
        .native_method = native_method,
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
    const auto set_static_constant =
        [this, &canonical_name](std::string_view field_name,
                                std::string_view descriptor,
                                Value value) -> Status
    {
      auto field = states_.resolve_field(
          canonical_name, field_name, descriptor, true);
      if (!field)
        return std::unexpected(field.error());
      return states_.set_static_field(*field, value);
    };
    const auto set_int_constants =
        [&set_static_constant](
            std::span<const std::pair<std::string_view, i32>> constants,
            std::string_view descriptor = "I") -> Status
    {
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_constant(
            field_name, descriptor, Value::from_int(value));
        if (!stored)
          return std::unexpected(stored.error());
      }
      return {};
    };
    const auto set_static_string =
        [this, &set_static_constant](std::string_view field_name,
                                     std::string_view value) -> Status
    {
      std::u16string text;
      text.reserve(value.size());
      for (const char character : value)
        text.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
      auto string = states_.allocate_instance(heap_, "java/lang/String");
      if (!string)
        return std::unexpected(string.error());
      auto attached = heap_.attach_string(*string, std::move(text));
      if (!attached)
        return std::unexpected(attached.error());
      return set_static_constant(
          field_name, "Ljava/lang/String;",
          Value::from_reference(*string));
    };
    if (canonical_name == "java/lang/Byte")
    {
      auto minimum = set_static_constant(
          "MIN_VALUE", "B", Value::from_int(std::numeric_limits<i8>::min()));
      auto maximum = set_static_constant(
          "MAX_VALUE", "B", Value::from_int(std::numeric_limits<i8>::max()));
      if (!minimum) return std::unexpected(minimum.error());
      if (!maximum) return std::unexpected(maximum.error());
    }
    else if (canonical_name == "java/lang/Short")
    {
      auto minimum = set_static_constant(
          "MIN_VALUE", "S", Value::from_int(std::numeric_limits<i16>::min()));
      auto maximum = set_static_constant(
          "MAX_VALUE", "S", Value::from_int(std::numeric_limits<i16>::max()));
      if (!minimum) return std::unexpected(minimum.error());
      if (!maximum) return std::unexpected(maximum.error());
    }
    else if (canonical_name == "java/lang/Integer")
    {
      auto minimum = set_static_constant(
          "MIN_VALUE", "I", Value::from_int(std::numeric_limits<i32>::min()));
      auto maximum = set_static_constant(
          "MAX_VALUE", "I", Value::from_int(std::numeric_limits<i32>::max()));
      if (!minimum) return std::unexpected(minimum.error());
      if (!maximum) return std::unexpected(maximum.error());
    }
    else if (canonical_name == "java/lang/Long")
    {
      auto minimum = set_static_constant(
          "MIN_VALUE", "J", Value::from_long(std::numeric_limits<i64>::min()));
      auto maximum = set_static_constant(
          "MAX_VALUE", "J", Value::from_long(std::numeric_limits<i64>::max()));
      if (!minimum) return std::unexpected(minimum.error());
      if (!maximum) return std::unexpected(maximum.error());
    }
    else if (canonical_name == "java/lang/Character")
    {
      const std::array<std::pair<std::string_view, i32>, 4> constants {{
          {"MIN_RADIX", 2},
          {"MAX_RADIX", 36},
          {"MIN_VALUE", 0},
          {"MAX_VALUE", 0xFFFF},
      }};
      for (const auto &[field_name, value] : constants)
      {
        const std::string_view descriptor =
            field_name.ends_with("VALUE") ? "C" : "I";
        auto stored = set_static_constant(
            field_name, descriptor, Value::from_int(value));
        if (!stored) return std::unexpected(stored.error());
      }
    }
    else if (canonical_name == "java/lang/Float")
    {
      const std::array<std::pair<std::string_view, float>, 5> constants {{
          {"POSITIVE_INFINITY", std::numeric_limits<float>::infinity()},
          {"NEGATIVE_INFINITY", -std::numeric_limits<float>::infinity()},
          {"NaN", std::numeric_limits<float>::quiet_NaN()},
          {"MAX_VALUE", std::numeric_limits<float>::max()},
          {"MIN_VALUE", std::numeric_limits<float>::denorm_min()},
      }};
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_constant(
            field_name, "F", Value::from_float(value));
        if (!stored) return std::unexpected(stored.error());
      }
    }
    else if (canonical_name == "java/lang/Double")
    {
      const std::array<std::pair<std::string_view, double>, 5> constants {{
          {"POSITIVE_INFINITY", std::numeric_limits<double>::infinity()},
          {"NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity()},
          {"NaN", std::numeric_limits<double>::quiet_NaN()},
          {"MAX_VALUE", std::numeric_limits<double>::max()},
          {"MIN_VALUE", std::numeric_limits<double>::denorm_min()},
      }};
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_constant(
            field_name, "D", Value::from_double(value));
        if (!stored) return std::unexpected(stored.error());
      }
    }
    else if (canonical_name == "java/lang/Math")
    {
      auto e = set_static_constant(
          "E", "D", Value::from_double(std::numbers::e_v<double>));
      auto pi = set_static_constant(
          "PI", "D", Value::from_double(std::numbers::pi_v<double>));
      if (!e) return std::unexpected(e.error());
      if (!pi) return std::unexpected(pi.error());
    }
    else if (canonical_name == "java/util/Calendar")
    {
      const std::array<std::pair<std::string_view, i32>, 31> constants {{
          {"YEAR", 1}, {"MONTH", 2}, {"DATE", 5},
          {"DAY_OF_MONTH", 5}, {"DAY_OF_WEEK", 7}, {"AM_PM", 9},
          {"HOUR", 10}, {"HOUR_OF_DAY", 11}, {"MINUTE", 12},
          {"SECOND", 13}, {"MILLISECOND", 14},
          {"SUNDAY", 1}, {"MONDAY", 2}, {"TUESDAY", 3},
          {"WEDNESDAY", 4}, {"THURSDAY", 5}, {"FRIDAY", 6},
          {"SATURDAY", 7},
          {"JANUARY", 0}, {"FEBRUARY", 1}, {"MARCH", 2},
          {"APRIL", 3}, {"MAY", 4}, {"JUNE", 5}, {"JULY", 6},
          {"AUGUST", 7}, {"SEPTEMBER", 8}, {"OCTOBER", 9},
          {"NOVEMBER", 10}, {"DECEMBER", 11}, {"AM", 0},
      }};
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_constant(
            field_name, "I", Value::from_int(value));
        if (!stored) return std::unexpected(stored.error());
      }
      auto pm = set_static_constant("PM", "I", Value::from_int(1));
      if (!pm) return std::unexpected(pm.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Choice")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"EXCLUSIVE", 1},
          {"MULTIPLE", 2}, {"IMPLICIT", 3}, {"POPUP", 4},
          {"TEXT_WRAP_DEFAULT", 0}, {"TEXT_WRAP_ON", 1},
          {"TEXT_WRAP_OFF", 2},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Canvas")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"KEY_NUM0", 48},
          {"KEY_NUM1", 49}, {"KEY_NUM2", 50}, {"KEY_NUM3", 51},
          {"KEY_NUM4", 52}, {"KEY_NUM5", 53}, {"KEY_NUM6", 54},
          {"KEY_NUM7", 55}, {"KEY_NUM8", 56}, {"KEY_NUM9", 57},
          {"KEY_STAR", 42}, {"KEY_POUND", 35}, {"UP", 1},
          {"DOWN", 6}, {"LEFT", 2}, {"RIGHT", 5}, {"FIRE", 8},
          {"GAME_A", 9}, {"GAME_B", 10}, {"GAME_C", 11},
          {"GAME_D", 12},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/game/GameCanvas")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"UP_PRESSED", 1 << 1},
          {"DOWN_PRESSED", 1 << 6}, {"LEFT_PRESSED", 1 << 2},
          {"RIGHT_PRESSED", 1 << 5}, {"FIRE_PRESSED", 1 << 8},
          {"GAME_A_PRESSED", 1 << 9}, {"GAME_B_PRESSED", 1 << 10},
          {"GAME_C_PRESSED", 1 << 11}, {"GAME_D_PRESSED", 1 << 12},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Graphics")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"HCENTER", 1},
          {"VCENTER", 2}, {"LEFT", 4}, {"RIGHT", 8}, {"TOP", 16},
          {"BOTTOM", 32}, {"BASELINE", 64}, {"SOLID", 0},
          {"DOTTED", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Font")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"STYLE_PLAIN", 0},
          {"STYLE_BOLD", 1}, {"STYLE_ITALIC", 2},
          {"STYLE_UNDERLINED", 4}, {"SIZE_SMALL", 8},
          {"SIZE_MEDIUM", 0}, {"SIZE_LARGE", 16}, {"FACE_SYSTEM", 0},
          {"FACE_MONOSPACE", 32}, {"FACE_PROPORTIONAL", 64},
          {"FONT_STATIC_TEXT", 0}, {"FONT_INPUT_TEXT", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Command")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"SCREEN", 1}, {"BACK", 2},
          {"CANCEL", 3}, {"OK", 4}, {"HELP", 5}, {"STOP", 6},
          {"EXIT", 7}, {"ITEM", 8},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Item")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"LAYOUT_DEFAULT", 0},
          {"LAYOUT_LEFT", 1}, {"LAYOUT_RIGHT", 2},
          {"LAYOUT_CENTER", 3}, {"LAYOUT_TOP", 0x10},
          {"LAYOUT_BOTTOM", 0x20}, {"LAYOUT_VCENTER", 0x30},
          {"LAYOUT_NEWLINE_BEFORE", 0x100},
          {"LAYOUT_NEWLINE_AFTER", 0x200}, {"LAYOUT_SHRINK", 0x400},
          {"LAYOUT_EXPAND", 0x800}, {"LAYOUT_VSHRINK", 0x1000},
          {"LAYOUT_VEXPAND", 0x2000}, {"LAYOUT_2", 0x4000},
          {"PLAIN", 0}, {"HYPERLINK", 1}, {"BUTTON", 2},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/TextField")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"ANY", 0}, {"EMAILADDR", 1},
          {"NUMERIC", 2}, {"PHONENUMBER", 3}, {"URL", 4},
          {"DECIMAL", 5}, {"CONSTRAINT_MASK", 0xFFFF},
          {"PASSWORD", 0x10000}, {"UNEDITABLE", 0x20000},
          {"SENSITIVE", 0x40000}, {"NON_PREDICTIVE", 0x80000},
          {"INITIAL_CAPS_WORD", 0x100000},
          {"INITIAL_CAPS_SENTENCE", 0x200000},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/Gauge")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"INDEFINITE", -1},
          {"CONTINUOUS_IDLE", 0}, {"INCREMENTAL_IDLE", 1},
          {"CONTINUOUS_RUNNING", 2}, {"INCREMENTAL_UPDATING", 3},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/DateField")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"DATE", 1}, {"TIME", 2},
          {"DATE_TIME", 3},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/CustomItem")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"NONE", 0},
          {"TRAVERSE_HORIZONTAL", 1}, {"TRAVERSE_VERTICAL", 2},
          {"KEY_PRESS", 4}, {"KEY_RELEASE", 8}, {"KEY_REPEAT", 0x10},
          {"POINTER_PRESS", 0x20}, {"POINTER_RELEASE", 0x40},
          {"POINTER_DRAG", 0x80},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/lcdui/game/Sprite")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"TRANS_NONE", 0},
          {"TRANS_ROT90", 5}, {"TRANS_ROT180", 3}, {"TRANS_ROT270", 6},
          {"TRANS_MIRROR", 2}, {"TRANS_MIRROR_ROT90", 7},
          {"TRANS_MIRROR_ROT180", 1}, {"TRANS_MIRROR_ROT270", 4},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "com/nokia/mid/sound/Sound")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"FORMAT_TONE", 1},
          {"FORMAT_WAV", 5}, {"SOUND_PLAYING", 0},
          {"SOUND_STOPPED", 1}, {"SOUND_UNINITIALIZED", 3},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "com/nokia/mid/ui/DirectGraphics")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"FLIP_HORIZONTAL", 0x2000},
          {"FLIP_VERTICAL", 0x4000}, {"ROTATE_90", 90},
          {"ROTATE_180", 180}, {"ROTATE_270", 270},
          {"TYPE_BYTE_1_GRAY", 1}, {"TYPE_BYTE_1_GRAY_VERTICAL", -1},
          {"TYPE_BYTE_2_GRAY", 2}, {"TYPE_BYTE_4_GRAY", 4},
          {"TYPE_BYTE_8_GRAY", 8}, {"TYPE_BYTE_332_RGB", 332},
          {"TYPE_USHORT_4444_ARGB", 4444}, {"TYPE_USHORT_444_RGB", 444},
          {"TYPE_USHORT_555_RGB", 555}, {"TYPE_USHORT_1555_ARGB", 1555},
          {"TYPE_USHORT_565_RGB", 565}, {"TYPE_INT_888_RGB", 888},
          {"TYPE_INT_8888_ARGB", 8888},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "com/mascotcapsule/micro3d/v3/Effect3D")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"NORMAL_SHADING", 0},
          {"TOON_SHADING", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "com/mascotcapsule/micro3d/v3/Graphics3D")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{
              "COMMAND_AFFINE_INDEX", std::bit_cast<i32>(0x87000000U)},
          {"COMMAND_AMBIENT_LIGHT", std::bit_cast<i32>(0xA0000000U)},
          {"COMMAND_ATTRIBUTE", std::bit_cast<i32>(0x83000000U)},
          {"COMMAND_CENTER", std::bit_cast<i32>(0x85000000U)},
          {"COMMAND_CLIP", std::bit_cast<i32>(0x84000000U)},
          {"COMMAND_DIRECTION_LIGHT", std::bit_cast<i32>(0xA1000000U)},
          {"COMMAND_END", std::bit_cast<i32>(0x80000000U)},
          {"COMMAND_FLUSH", std::bit_cast<i32>(0x82000000U)},
          {"COMMAND_LIST_VERSION_1_0", std::bit_cast<i32>(0xFE000001U)},
          {"COMMAND_NOP", std::bit_cast<i32>(0x81000000U)},
          {"COMMAND_PARALLEL_SCALE", std::bit_cast<i32>(0x90000000U)},
          {"COMMAND_PARALLEL_SIZE", std::bit_cast<i32>(0x91000000U)},
          {"COMMAND_PERSPECTIVE_FOV", std::bit_cast<i32>(0x92000000U)},
          {"COMMAND_PERSPECTIVE_WH", std::bit_cast<i32>(0x93000000U)},
          {"COMMAND_TEXTURE_INDEX", std::bit_cast<i32>(0x86000000U)},
          {"COMMAND_THRESHOLD", std::bit_cast<i32>(0xAF000000U)},
          {"ENV_ATTR_LIGHTING", 1}, {"ENV_ATTR_SEMI_TRANSPARENT", 8},
          {"ENV_ATTR_SPHERE_MAP", 2}, {"ENV_ATTR_TOON_SHADING", 4},
          {"PATTR_BLEND_ADD", 64}, {"PATTR_BLEND_HALF", 32},
          {"PATTR_BLEND_NORMAL", 0}, {"PATTR_BLEND_SUB", 96},
          {"PATTR_COLORKEY", 16}, {"PATTR_LIGHTING", 1},
          {"PATTR_SPHERE_MAP", 2}, {"PDATA_COLOR_NONE", 0},
          {"PDATA_COLOR_PER_COMMAND", 1024},
          {"PDATA_COLOR_PER_FACE", 2048}, {"PDATA_NORMAL_NONE", 0},
          {"PDATA_NORMAL_PER_FACE", 512},
          {"PDATA_NORMAL_PER_VERTEX", 768},
          {"PDATA_POINT_SPRITE_PARAMS_PER_CMD", 4096},
          {"PDATA_POINT_SPRITE_PARAMS_PER_FACE", 8192},
          {"PDATA_POINT_SPRITE_PARAMS_PER_VERTEX", 12288},
          {"PDATA_TEXURE_COORD", 12288}, {"PDATA_TEXURE_COORD_NONE", 0},
          {"POINT_SPRITE_LOCAL_SIZE", 0}, {"POINT_SPRITE_NO_PERS", 2},
          {"POINT_SPRITE_PERSPECTIVE", 0}, {"POINT_SPRITE_PIXEL_SIZE", 1},
          {"PRIMITVE_LINES", 0x02000000}, {"PRIMITVE_POINTS", 0x01000000},
          {"PRIMITVE_POINT_SPRITES", 0x05000000},
          {"PRIMITVE_QUADS", 0x04000000},
          {"PRIMITVE_TRIANGLES", 0x03000000},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "com/nokia/mid/ui/FullCanvas")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"KEY_UP_ARROW", -1},
          {"KEY_DOWN_ARROW", -2}, {"KEY_LEFT_ARROW", -3},
          {"KEY_RIGHT_ARROW", -4}, {"KEY_SOFTKEY1", -6},
          {"KEY_SOFTKEY2", -7}, {"KEY_SOFTKEY3", -5},
          {"KEY_SEND", -10}, {"KEY_END", -11},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/io/file/FileSystemListener")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"ROOT_ADDED", 0},
          {"ROOT_REMOVED", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/Player")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"UNREALIZED", 100},
          {"REALIZED", 200}, {"PREFETCHED", 300}, {"STARTED", 400},
          {"CLOSED", 0},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
      auto unknown = set_static_constant(
          "TIME_UNKNOWN", "J", Value::from_long(-1));
      if (!unknown) return std::unexpected(unknown.error());
    }
    else if (canonical_name == "javax/microedition/media/control/ToneControl")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"VERSION", -2}, {"TEMPO", -3},
          {"RESOLUTION", -4}, {"BLOCK_START", -5}, {"BLOCK_END", -6},
          {"PLAY_BLOCK", -7}, {"SET_VOLUME", -8}, {"REPEAT", -9},
          {"SILENCE", -1}, {"C4", 60},
      };
      auto stored = set_int_constants(constants, "B");
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/Manager")
    {
      auto tone = set_static_string("TONE_DEVICE_LOCATOR", "device://tone");
      auto midi = set_static_string("MIDI_DEVICE_LOCATOR", "device://midi");
      if (!tone) return std::unexpected(tone.error());
      if (!midi) return std::unexpected(midi.error());
    }
    else if (canonical_name == "javax/microedition/media/PlayerListener")
    {
      constexpr std::pair<std::string_view, std::string_view> constants[] {
          std::pair<std::string_view, std::string_view>{"STARTED", "started"},
          {"STOPPED", "stopped"}, {"END_OF_MEDIA", "endOfMedia"},
          {"DURATION_UPDATED", "durationUpdated"},
          {"DEVICE_UNAVAILABLE", "deviceUnavailable"},
          {"DEVICE_AVAILABLE", "deviceAvailable"},
          {"VOLUME_CHANGED", "volumeChanged"}, {"ERROR", "error"},
          {"CLOSED", "closed"}, {"BUFFERING_STARTED", "bufferingStarted"},
          {"BUFFERING_STOPPED", "bufferingStopped"},
          {"RECORD_STARTED", "recordStarted"},
          {"RECORD_STOPPED", "recordStopped"},
          {"RECORD_ERROR", "recordError"}, {"SIZE_CHANGED", "sizeChanged"},
          {"STOPPED_AT_TIME", "stoppedAtTime"},
      };
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_string(field_name, value);
        if (!stored) return std::unexpected(stored.error());
      }
    }
    else if (canonical_name == "javax/microedition/media/control/GUIControl")
    {
      const std::array<std::pair<std::string_view, i32>, 1> constants {{
          {"USE_GUI_PRIMITIVE", 0},
      }};
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/control/MIDIControl")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"NOTE_ON", 0x90},
          {"CONTROL_CHANGE", 0xB0},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/control/MetaDataControl")
    {
      constexpr std::pair<std::string_view, std::string_view> constants[] {
          std::pair<std::string_view, std::string_view>{"AUTHOR_KEY", "author"},
          {"COPYRIGHT_KEY", "copyright"}, {"DATE_KEY", "date"},
          {"TITLE_KEY", "title"},
      };
      for (const auto &[field_name, value] : constants)
      {
        auto stored = set_static_string(field_name, value);
        if (!stored) return std::unexpected(stored.error());
      }
    }
    else if (canonical_name == "javax/microedition/media/control/StopTimeControl")
    {
      auto stored = set_static_constant(
          "RESET", "J", Value::from_long(std::numeric_limits<i64>::max()));
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/control/VideoControl")
    {
      const std::array<std::pair<std::string_view, i32>, 1> constants {{
          {"USE_DIRECT_VIDEO", 1},
      }};
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/media/protocol/SourceStream")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"NOT_SEEKABLE", 0},
          {"SEEKABLE_TO_START", 1}, {"RANDOM_ACCESSIBLE", 2},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/rms/RecordComparator")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"PRECEDES", -1},
          {"EQUIVALENT", 0}, {"FOLLOWS", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
    }
    else if (canonical_name == "javax/microedition/rms/RecordStore")
    {
      constexpr std::pair<std::string_view, i32> constants[] {
          std::pair<std::string_view, i32>{"AUTHMODE_PRIVATE", 0},
          {"AUTHMODE_ANY", 1},
      };
      auto stored = set_int_constants(constants);
      if (!stored) return std::unexpected(stored.error());
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
    const JavaThreadId initialization_thread =
        scheduler_.current_thread_id();
    for (;;)
    {
      bool erroneous = false;
      {
        std::unique_lock initialization_lock(class_initialization_mutex_);
        if (initialized_classes_.contains(canonical_name))
          return std::optional<ObjectRef>{};
        if (erroneous_classes_.contains(canonical_name))
        {
          erroneous = true;
        }
        else if (const auto owner =
                     initializing_class_owners_.find(canonical_name);
                 owner == initializing_class_owners_.end())
        {
          initializing_class_owners_.emplace(canonical_name,
                                             initialization_thread);
          vm_trace("class-init",
                   "begin java=%u class=%s",
                   static_cast<unsigned>(initialization_thread),
                   canonical_name.c_str());
          break;
        }
        else if (owner->second == initialization_thread)
        {
          // Recursive requests by the thread currently running <clinit> are
          // allowed to observe the class's in-progress default values.
          return std::optional<ObjectRef>{};
        }
        else
        {
          // phoneME keeps an initialization owner per class. A competing
          // thread must wait rather than treating a partially initialized
          // class as ready. Release the VM execution gate while waiting or the
          // owner could never resume after a scheduler quantum.
          vm_trace("class-init",
                   "wait java=%u class=%s owner=%u",
                   static_cast<unsigned>(initialization_thread),
                   canonical_name.c_str(),
                   static_cast<unsigned>(owner->second));
          scheduler_.set_current_state(JavaThreadState::waiting);
          const u32 released_depth = suspend_execution_for_blocking();
          class_initialization_condition_.wait(
              initialization_lock,
              [this, &canonical_name]
              {
                return shutdown_started_.load(std::memory_order_acquire) ||
                       initialized_classes_.contains(canonical_name) ||
                       erroneous_classes_.contains(canonical_name) ||
                       !initializing_class_owners_.contains(canonical_name);
              });
          // Never reacquire the execution gate while holding the class-state
          // mutex: the owner finishes <clinit> under that gate and must lock
          // class state to publish completion.
          initialization_lock.unlock();
          resume_execution_after_blocking(released_depth);
          scheduler_.set_current_state(JavaThreadState::running);
          vm_trace("class-init",
                   "resume java=%u class=%s",
                   static_cast<unsigned>(initialization_thread),
                   canonical_name.c_str());
          if (shutdown_started_.load(std::memory_order_acquire))
          {
            return fail(ErrorCode::invalid_state,
                        "class initialization was cancelled by VM shutdown");
          }
          continue;
        }
      }
      if (erroneous)
      {
        auto throwable = create_throwable("java/lang/NoClassDefFoundError");
        if (!throwable)
          return std::unexpected(throwable.error());
        return std::optional<ObjectRef>(*throwable);
      }
    }

    PerformanceCounters::record_class_initialization();
    const auto finish_initialization =
        [this, &canonical_name, initialization_thread](bool initialized,
                                                       bool erroneous)
    {
      {
        std::scoped_lock initialization_lock(class_initialization_mutex_);
        initializing_class_owners_.erase(canonical_name);
        if (initialized)
          initialized_classes_.insert(canonical_name);
        if (erroneous)
          erroneous_classes_.insert(canonical_name);
      }
      class_initialization_condition_.notify_all();
      vm_trace("class-init",
               "end java=%u class=%s result=%s",
               static_cast<unsigned>(initialization_thread),
               canonical_name.c_str(),
               initialized ? "initialized"
                           : (erroneous ? "erroneous" : "rolled-back"));
    };
    const auto rollback = [finish_initialization]()
    {
      finish_initialization(false, false);
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
        finish_initialization(false, true);
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
        finish_initialization(false, true);
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

        finish_initialization(false, true);
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
        auto cause_stored = heap_.set_field(
            *wrapper, 1U, Value::from_reference(*result->throwable));
        if (!cause_stored)
        {
          return std::unexpected(cause_stored.error());
        }
        auto cause_initialized = heap_.set_field(
            *wrapper, 2U, Value::from_int(1));
        if (!cause_initialized)
        {
          return std::unexpected(cause_initialized.error());
        }
        return std::optional<ObjectRef>(*wrapper);
      }
    }

    finish_initialization(true, false);
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
    if (invocation.descriptor == nullptr)
    {
      return fail(ErrorCode::internal_error,
                  "native invocation has no cached descriptor");
    }
    const CachedMethodDescriptor &descriptor = *invocation.descriptor;
    const std::string &owner_name = invocation.method.owner->name();
    const std::string &method_name = invocation.method.method->name;
    const std::string &method_descriptor =
        invocation.method.method->descriptor;
    if (!invocation.native_method.valid() &&
        is_builtin_class(owner_name) && method_name == "<init>" &&
        method_descriptor == "()V")
    {
      return std::optional<Value>{};
    }

    Result<std::optional<Value>> result = invocation.native_method.valid()
        ? natives_.invoke(*this,
                          invocation.native_method,
                          invocation.arguments)
        : fail(ErrorCode::unsupported_feature,
               "native method is not ported: " + owner_name + "." +
                   method_name + method_descriptor);
    if (!result)
    {
      return std::unexpected(result.error());
    }
    if (descriptor.return_kind == JavaTypeKind::void_type)
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
          !value_matches(**result, descriptor.descriptor.return_type))
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

  Result<ObjectRef> Machine::create_throwable(
      std::string_view class_name,
      std::string_view message)
  {
    auto throwable = create_throwable(class_name);
    if (!throwable || message.empty() ||
        class_name == "java/lang/OutOfMemoryError")
    {
      return throwable;
    }
    auto message_string = intern_string(message);
    if (!message_string)
    {
      return std::unexpected(message_string.error());
    }
    auto message_stored = heap_.set_field(
        *throwable, 0U, Value::from_reference(*message_string));
    if (!message_stored)
    {
      return std::unexpected(message_stored.error());
    }
    auto cause_stored = heap_.set_field(
        *throwable, 1U, Value::from_reference({}));
    if (!cause_stored)
    {
      return std::unexpected(cause_stored.error());
    }
    auto cause_initialized = heap_.set_field(
        *throwable, 2U, Value::from_int(0));
    if (!cause_initialized)
    {
      return std::unexpected(cause_initialized.error());
    }
    return *throwable;
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

  Result<ExecutionResult> Machine::execute(
      Invocation invocation,
      u64 instruction_budget,
      InstructionBudgetMode budget_mode)
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
    if (g_execution_lock_depth == 1U)
      scheduler_.begin_execution_slice();
    const u32 invocation_depth = g_execution_lock_depth;
    PerformanceCounters::observe_java_call_depth(invocation_depth);
    const JavaThreadId invocation_thread = scheduler_.current_thread_id();
    const HeapAccessContext previous_heap_context =
        current_heap_access_context();
    usize current_heap_access_pc = 0U;
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
      {
        g_execution_machine = nullptr;
        PerformanceCounters::flush_thread_local();
      }
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

    const bool has_native_override = invocation.native_method.valid();
    if (has_native_override || !invocation.method.method->code.has_value())
    {
      const auto native_started = std::chrono::steady_clock::now();
      auto native_result = invoke_native(invocation);
      const auto native_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - native_started);
      if (native_duration >= std::chrono::milliseconds(10) &&
          should_trace_slow_native(invocation.method.owner->name(),
                                   invocation.method.method->name))
      {
        vm_trace("native",
                 "slow-call java=%u target=%s.%s%s duration_us=%lld ok=%d",
                 static_cast<unsigned>(scheduler_.current_thread_id()),
                 invocation.method.owner->name().c_str(),
                 invocation.method.method->name.c_str(),
                 invocation.method.method->descriptor.c_str(),
                 static_cast<long long>(native_duration.count()),
                 native_result.has_value() ? 1 : 0);
      }
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
              native_result.error().java_exception_class,
              native_result.error().message);
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

    if (invocation.descriptor == nullptr)
    {
      return fail(ErrorCode::internal_error,
                  "VM invocation has no cached descriptor");
    }
    if (invocation.method.runtime != nullptr &&
        invocation.method.owner != nullptr &&
        !invocation.return_override.has_value())
    {
      auto jitted = jit_.try_execute(
          invocation.method.runtime->id,
          *invocation.method.owner,
          *invocation.method.method,
          *invocation.descriptor,
          invocation.arguments,
          invocation.has_receiver,
          instruction_budget);
      if (!jitted)
      {
        auto released = release_synchronized_monitor(*root_monitor);
        if (!released)
          return std::unexpected(released.error());
        return std::unexpected(jitted.error());
      }
      if (jitted->has_value())
      {
        const u64 jit_instructions =
            static_cast<u64>((*jitted)->bytecode_instructions);
        accounted_instructions = jit_instructions;
        auto released = release_synchronized_monitor(*root_monitor);
        if (!released)
          return std::unexpected(released.error());
        return ExecutionResult{
            .return_value = (*jitted)->return_value,
            .throwable = std::nullopt,
            .executed_instructions = jit_instructions,
        };
      }
    }
    auto root_frame = ExecutionFrame::make(std::move(invocation.method),
                                           invocation.descriptor->descriptor,
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
    PerformanceCounters::observe_java_call_depth(frames.size());
    u64 executed = 0;
    u64 watchdog_instructions = 0;
    const u64 progress_total_budget =
        instruction_budget > std::numeric_limits<u64>::max() / 32U
            ? std::numeric_limits<u64>::max()
            : instruction_budget * 32U;
    u64 next_scheduler_quantum = kSchedulerQuantum;
    const classfile::ClassFile* heap_access_owner = nullptr;
    const classfile::Method* heap_access_method = nullptr;

    const auto publish_active_execution_roots =
        [this, &frames, invocation_depth](
            std::span<const Value> extra_values = {})
    {
      std::vector<ObjectRef> roots;
      roots.reserve(frames.size() * 8U + extra_values.size() + 8U);
      for (const ExecutionFrame& active_frame : frames)
        active_frame.append_reference_roots(roots);
      for (const Value value : extra_values)
      {
        if (value.kind() != ValueKind::reference)
          continue;
        auto reference = value.as_reference();
        if (reference && !reference->is_null())
          roots.push_back(*reference);
      }
      publish_execution_roots(invocation_depth, roots);
    };

    const auto ensure_initialized_from_execution =
        [this, &publish_active_execution_roots](
            std::string_view class_name,
            u64 remaining_budget,
            std::span<const Value> extra_values = {})
        -> Result<std::optional<ObjectRef>>
    {
      // Class initialization enters a recursive Machine::execute invocation.
      // That nested invocation may hit a scheduler quantum or blocking native
      // and release the VM gate. Publish the complete outer interpreter state
      // first so a different Java thread cannot collect references that were
      // created since the previous safepoint. Include operands already popped
      // into C++ argument vectors, such as invokestatic parameters.
      publish_active_execution_roots(extra_values);
      return ensure_initialized(class_name, remaining_budget);
    };

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

    struct RangeDecoderIntrinsicFields final
    {
      FieldLocation range;
      FieldLocation code;
      FieldLocation probabilities;
      FieldLocation input_position;
      FieldLocation input_length;
      FieldLocation input_bytes;
    };
    std::unordered_map<const classfile::Method*,
                       std::optional<RangeDecoderIntrinsicFields>>
        range_decoder_intrinsic_cache;

    const auto resolve_range_decoder_intrinsic =
        [this](const Invocation& candidate)
        -> Result<std::optional<RangeDecoderIntrinsicFields>>
    {
      if (candidate.method.owner == nullptr ||
          candidate.method.method == nullptr || candidate.has_receiver ||
          candidate.arguments.size() != 1U ||
          !matches_range_decoder_bit_intrinsic(*candidate.method.method))
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      const auto& owner = *candidate.method.owner;
      const auto& code = candidate.method.method->code->bytecode;
      const auto cp_index = [](const std::vector<u8>& bytecode,
                               usize offset) -> u16
      {
        return static_cast<u16>(
            (static_cast<u16>(bytecode[offset]) << 8U) |
            static_cast<u16>(bytecode[offset + 1U]));
      };
      const u16 range_index = cp_index(code, 1U);
      const u16 probabilities_index = cp_index(code, 7U);
      const u16 decoder_code_index = cp_index(code, 15U);
      const u16 input_method_index = cp_index(code, 65U);
      for (usize offset : {24U, 49U, 73U, 79U, 84U, 89U,
                           116U, 140U, 146U})
      {
        if (cp_index(code, offset) != range_index)
          return std::optional<RangeDecoderIntrinsicFields>{};
      }
      for (usize offset : {30U, 37U, 100U, 106U})
      {
        if (cp_index(code, offset) != probabilities_index)
          return std::optional<RangeDecoderIntrinsicFields>{};
      }
      for (usize offset : {59U, 70U, 92U, 97U, 126U, 137U})
      {
        if (cp_index(code, offset) != decoder_code_index)
          return std::optional<RangeDecoderIntrinsicFields>{};
      }
      if (cp_index(code, 132U) != input_method_index ||
          cp_index(code, 52U) != cp_index(code, 119U))
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }

      auto normalization_constant = owner.constant(cp_index(code, 52U));
      if (!normalization_constant ||
          (*normalization_constant)->kind !=
              classfile::ConstantKind::long64 ||
          (*normalization_constant)->bits != 16'777'216ULL)
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      auto range_reference = owner.member_reference(range_index);
      auto probabilities_reference = owner.member_reference(probabilities_index);
      auto decoder_code_reference = owner.member_reference(decoder_code_index);
      auto input_reference = owner.member_reference(input_method_index);
      if (!range_reference || !probabilities_reference ||
          !decoder_code_reference || !input_reference)
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      if (range_reference->descriptor != "J" ||
          probabilities_reference->descriptor != "[S" ||
          decoder_code_reference->descriptor != "J" ||
          input_reference->descriptor != "()I")
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }

      auto input_method = classes_.resolve_declared_method(
          input_reference->owner,
          input_reference->name,
          input_reference->descriptor);
      if (!input_method ||
          !matches_range_decoder_input_intrinsic(*input_method->method))
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      const auto& input_code = input_method->method->code->bytecode;
      const u16 position_index = cp_index(input_code, 1U);
      const u16 length_index = cp_index(input_code, 4U);
      const u16 bytes_index = cp_index(input_code, 14U);
      if (cp_index(input_code, 17U) != position_index ||
          cp_index(input_code, 23U) != position_index)
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      auto position_reference =
          input_method->owner->member_reference(position_index);
      auto length_reference = input_method->owner->member_reference(length_index);
      auto bytes_reference = input_method->owner->member_reference(bytes_index);
      if (!position_reference || !length_reference || !bytes_reference ||
          position_reference->descriptor != "I" ||
          length_reference->descriptor != "I" ||
          bytes_reference->descriptor != "[B")
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }

      auto range_field = states_.resolve_field(
          range_reference->owner, range_reference->name,
          range_reference->descriptor, true);
      auto decoder_code_field = states_.resolve_field(
          decoder_code_reference->owner, decoder_code_reference->name,
          decoder_code_reference->descriptor, true);
      auto probabilities_field = states_.resolve_field(
          probabilities_reference->owner, probabilities_reference->name,
          probabilities_reference->descriptor, true);
      auto position_field = states_.resolve_field(
          position_reference->owner, position_reference->name,
          position_reference->descriptor, true);
      auto length_field = states_.resolve_field(
          length_reference->owner, length_reference->name,
          length_reference->descriptor, true);
      auto bytes_field = states_.resolve_field(
          bytes_reference->owner, bytes_reference->name,
          bytes_reference->descriptor, true);
      if (!range_field || !decoder_code_field || !probabilities_field ||
          !position_field || !length_field || !bytes_field)
      {
        return std::optional<RangeDecoderIntrinsicFields>{};
      }
      return std::optional<RangeDecoderIntrinsicFields>(
          RangeDecoderIntrinsicFields {
              .range = std::move(*range_field),
              .code = std::move(*decoder_code_field),
              .probabilities = std::move(*probabilities_field),
              .input_position = std::move(*position_field),
              .input_length = std::move(*length_field),
              .input_bytes = std::move(*bytes_field),
          });
    };

    const auto try_range_decoder_bit_intrinsic =
        [this, &range_decoder_intrinsic_cache,
         &resolve_range_decoder_intrinsic](const Invocation& candidate)
        -> Result<std::optional<Value>>
    {
      if (!specialized_intrinsics_requested())
        return std::optional<Value>{};
      if (candidate.method.method == nullptr || candidate.has_receiver ||
          candidate.arguments.size() != 1U ||
          !matches_range_decoder_bit_intrinsic(*candidate.method.method))
      {
        return std::optional<Value>{};
      }
      auto cached = range_decoder_intrinsic_cache.find(
          candidate.method.method);
      if (cached == range_decoder_intrinsic_cache.end())
      {
        auto resolved = resolve_range_decoder_intrinsic(candidate);
        if (!resolved) return std::unexpected(resolved.error());
        cached = range_decoder_intrinsic_cache.emplace(
            candidate.method.method, std::move(*resolved)).first;
      }
      if (!cached->second.has_value()) return std::optional<Value>{};
      const RangeDecoderIntrinsicFields& fields = *cached->second;

      auto probability_index = candidate.arguments[0].as_int();
      auto range_value = states_.static_field(fields.range);
      auto decoder_code_value = states_.static_field(fields.code);
      auto probabilities_value = states_.static_field(fields.probabilities);
      auto position_value = states_.static_field(fields.input_position);
      auto length_value = states_.static_field(fields.input_length);
      auto bytes_value = states_.static_field(fields.input_bytes);
      if (!probability_index || !range_value || !decoder_code_value ||
          !probabilities_value || !position_value || !length_value ||
          !bytes_value)
      {
        return std::optional<Value>{};
      }
      auto range = range_value->as_long();
      auto decoder_code = decoder_code_value->as_long();
      auto probabilities = probabilities_value->as_reference();
      auto position = position_value->as_int();
      auto input_length = length_value->as_int();
      auto input_bytes = bytes_value->as_reference();
      if (!range || !decoder_code || !probabilities || !position ||
          !input_length || !input_bytes || probabilities->is_null() ||
          input_bytes->is_null() || *probability_index < 0 ||
          *position < 0 || *input_length < *position)
      {
        return std::optional<Value>{};
      }
      auto probability_count = heap_.array_length(*probabilities);
      auto input_capacity = heap_.array_length(*input_bytes);
      if (!probability_count || !input_capacity ||
          static_cast<usize>(*probability_index) >= *probability_count ||
          static_cast<usize>(*input_length) > *input_capacity)
      {
        return std::optional<Value>{};
      }
      auto probability_value = heap_.element(
          *probabilities, static_cast<usize>(*probability_index));
      if (!probability_value) return std::optional<Value>{};
      auto probability = probability_value->as_int();
      if (!probability || *probability < 0 || *probability > 2'048)
        return std::optional<Value>{};

      u64 next_range = static_cast<u64>(*range);
      u64 next_code = static_cast<u64>(*decoder_code);
      const u64 bound = (next_range >> 11U) *
                        static_cast<u64>(*probability);
      i32 next_probability = *probability;
      i32 decoded_bit = 0;
      if (next_code < bound)
      {
        next_range = bound;
        next_probability += (2'048 - next_probability) >> 5;
      }
      else
      {
        next_range -= bound;
        next_code -= bound;
        next_probability -= next_probability >> 5;
        decoded_bit = 1;
      }

      i32 next_position = *position;
      if (next_range < 16'777'216ULL)
      {
        u8 next_byte = 0xFFU;
        if (next_position != *input_length)
        {
          auto byte_value = heap_.element(
              *input_bytes, static_cast<usize>(next_position));
          if (!byte_value) return std::optional<Value>{};
          auto byte = byte_value->as_int();
          if (!byte) return std::optional<Value>{};
          next_byte = static_cast<u8>(static_cast<i8>(*byte));
          ++next_position;
        }
        next_code = (next_code << 8U) | next_byte;
        next_range <<= 8U;
      }

      auto probability_stored = heap_.set_element(
          *probabilities,
          static_cast<usize>(*probability_index),
          Value::from_int(static_cast<i32>(
              static_cast<i16>(next_probability))));
      auto range_stored = states_.set_static_field(
          fields.range, Value::from_long(static_cast<i64>(next_range)));
      auto code_stored = states_.set_static_field(
          fields.code, Value::from_long(static_cast<i64>(next_code)));
      auto position_stored = states_.set_static_field(
          fields.input_position, Value::from_int(next_position));
      if (!probability_stored) return std::unexpected(probability_stored.error());
      if (!range_stored) return std::unexpected(range_stored.error());
      if (!code_stored) return std::unexpected(code_stored.error());
      if (!position_stored) return std::unexpected(position_stored.error());
      return std::optional<Value>(Value::from_int(decoded_bit));
    };

    const auto try_vector_key_sort_intrinsic =
        [this](const Invocation& candidate) -> Result<bool>
    {
      if (!specialized_intrinsics_requested()) return false;
      if (candidate.method.owner == nullptr ||
          candidate.method.method == nullptr || candidate.has_receiver ||
          !candidate.arguments.empty() ||
          !matches_vector_key_sort_initializer(*candidate.method.method))
      {
        return false;
      }

      const auto& owner = *candidate.method.owner;
      const auto& sort_code = candidate.method.method->code->bytecode;
      const u16 vector_field_index = static_cast<u16>(
          (static_cast<u16>(sort_code[1U]) << 8U) |
          static_cast<u16>(sort_code[2U]));
      auto vector_reference = owner.member_reference(vector_field_index);
      if (!vector_reference ||
          vector_reference->descriptor != "Ljava/util/Vector;")
      {
        return false;
      }

      const classfile::Method* comparator = nullptr;
      for (const auto& method : owner.methods())
      {
        if ((method.access_flags & kAccStatic) != 0U ||
            !method.code.has_value() || method.code->bytecode.size() != 69U ||
            !method.descriptor.ends_with(")Z"))
        {
          continue;
        }
        const auto& code = method.code->bytecode;
        const bool shape_matches =
            code[0U] == 0x2AU && code[1U] == 0x2BU &&
            code[2U] == 0xA6U && code[5U] == 0x04U &&
            code[6U] == 0xACU && code[7U] == 0x2BU &&
            code[8U] == 0xC1U && code[11U] == 0x99U &&
            code[14U] == 0x2AU && code[15U] == 0xB4U &&
            code[18U] == 0x10U && code[19U] == 56U &&
            code[20U] == 0x10U && code[21U] == 63U &&
            code[22U] == 0x2AU && code[23U] == 0xB4U &&
            code[26U] == 0x2AU && code[27U] == 0xB4U &&
            code[30U] == 0xB8U && code[33U] == 0x2BU &&
            code[34U] == 0xC0U && code[37U] == 0x4DU &&
            code[38U] == 0x2CU && code[39U] == 0xB4U &&
            code[42U] == 0x10U && code[43U] == 56U &&
            code[44U] == 0x10U && code[45U] == 63U &&
            code[46U] == 0x2CU && code[47U] == 0xB4U &&
            code[50U] == 0x2CU && code[51U] == 0xB4U &&
            code[54U] == 0xB8U && code[57U] == 0x65U &&
            code[58U] == 0x09U && code[59U] == 0x94U &&
            code[60U] == 0x9EU && code[63U] == 0x03U &&
            code[64U] == 0xACU && code[65U] == 0x04U &&
            code[66U] == 0xACU && code[67U] == 0x04U &&
            code[68U] == 0xACU &&
            code[9U] == code[35U] && code[10U] == code[36U] &&
            code[16U] == code[40U] && code[17U] == code[41U] &&
            code[24U] == code[48U] && code[25U] == code[49U] &&
            code[31U] == code[55U] && code[32U] == code[56U];
        if (shape_matches)
        {
          comparator = &method;
          break;
        }
      }
      if (comparator == nullptr) return false;

      const auto& comparator_code = comparator->code->bytecode;
      const auto member_index = [&comparator_code](usize offset) {
        return static_cast<u16>(
            (static_cast<u16>(comparator_code[offset]) << 8U) |
            static_cast<u16>(comparator_code[offset + 1U]));
      };
      auto key_reference = owner.member_reference(member_index(16U));
      auto shifts_reference = owner.member_reference(member_index(24U));
      auto permutation_reference = owner.member_reference(member_index(31U));
      if (!key_reference || !shifts_reference || !permutation_reference ||
          key_reference->descriptor != "J" ||
          shifts_reference->descriptor != "[I" ||
          permutation_reference->descriptor != "(JII[I[J)J")
      {
        return false;
      }
      auto permutation_method = classes_.resolve_declared_method(
          permutation_reference->owner,
          permutation_reference->name,
          permutation_reference->descriptor);
      if (!permutation_method ||
          !matches_long_bit_permutation_intrinsic(
              *permutation_method->method))
      {
        return false;
      }
      const auto& permutation_code =
          permutation_method->method->code->bytecode;
      const u16 masks_field_index = static_cast<u16>(
          (static_cast<u16>(permutation_code[16U]) << 8U) |
          static_cast<u16>(permutation_code[17U]));
      auto masks_reference =
          permutation_method->owner->member_reference(masks_field_index);
      if (!masks_reference || masks_reference->descriptor != "[J")
      {
        return false;
      }

      auto vector_field = states_.resolve_field(vector_reference->owner,
                                                vector_reference->name,
                                                vector_reference->descriptor,
                                                true);
      auto key_field = states_.resolve_field(key_reference->owner,
                                             key_reference->name,
                                             key_reference->descriptor,
                                             false);
      auto shifts_field = states_.resolve_field(shifts_reference->owner,
                                                shifts_reference->name,
                                                shifts_reference->descriptor,
                                                false);
      auto masks_field = states_.resolve_field(masks_reference->owner,
                                               masks_reference->name,
                                               masks_reference->descriptor,
                                               true);
      if (!vector_field || !key_field || !shifts_field || !masks_field)
      {
        return false;
      }
      auto vector_value = states_.static_field(*vector_field);
      auto masks_value = states_.static_field(*masks_field);
      if (!vector_value || !masks_value) return false;
      auto vector = vector_value->as_reference();
      auto masks = masks_value->as_reference();
      if (!vector || !masks || vector->is_null() || masks->is_null())
      {
        return false;
      }
      auto vector_class = heap_.class_name(*vector);
      if (!vector_class || *vector_class != "java/util/Vector")
      {
        return false;
      }
      auto data_value = heap_.field(*vector, 0U);
      auto count_value = heap_.field(*vector, 1U);
      if (!data_value || !count_value) return false;
      auto data = data_value->as_reference();
      auto count = count_value->as_int();
      if (!data || !count || data->is_null() || *count < 0)
      {
        return false;
      }
      auto data_length = heap_.array_length(*data);
      auto mask_count = heap_.array_length(*masks);
      if (!data_length || !mask_count ||
          static_cast<usize>(*count) > *data_length)
      {
        return false;
      }

      std::vector<u64> mask_values;
      mask_values.reserve(*mask_count);
      for (usize index = 0; index < *mask_count; ++index)
      {
        auto element = heap_.element(*masks, index);
        if (!element) return false;
        auto value = element->as_long();
        if (!value) return false;
        mask_values.push_back(static_cast<u64>(*value));
      }

      struct SortEntry final {
        u64 key {0U};
        Value value;
      };
      std::vector<SortEntry> entries;
      entries.reserve(static_cast<usize>(*count));
      for (i32 index = 0; index < *count; ++index)
      {
        auto element = heap_.element(*data, static_cast<usize>(index));
        if (!element) return false;
        auto object = element->as_reference();
        if (!object || object->is_null()) return false;
        auto object_class = heap_.class_name(*object);
        if (!object_class || *object_class != owner.name()) return false;
        auto input_value = heap_.field(*object, key_field->index);
        auto shift_value = heap_.field(*object, shifts_field->index);
        if (!input_value || !shift_value) return false;
        auto input = input_value->as_long();
        auto shifts = shift_value->as_reference();
        if (!input || !shifts || shifts->is_null()) return false;
        auto shift_count = heap_.array_length(*shifts);
        if (!shift_count || *shift_count > mask_values.size()) return false;

        const u64 source = static_cast<u64>(*input);
        u64 accumulated = 0U;
        for (usize shift_index = 0; shift_index < *shift_count;
             ++shift_index)
        {
          auto shift_element = heap_.element(*shifts, shift_index);
          if (!shift_element) return false;
          auto shift = shift_element->as_int();
          if (!shift) return false;
          u64 selected = source & mask_values[shift_index];
          if (selected == 0U) continue;
          if (*shift > 0)
          {
            selected >>= static_cast<u32>(*shift) & 63U;
          }
          else if (*shift < 0)
          {
            selected <<=
                (0U - static_cast<u32>(*shift)) & 63U;
          }
          accumulated |= selected;
        }
        entries.push_back(SortEntry {
            .key = accumulated >> 56U,
            .value = *element,
        });
      }

      std::stable_sort(entries.begin(), entries.end(),
                       [](const SortEntry& left, const SortEntry& right) {
                         return left.key < right.key;
                       });
      for (usize index = 0; index < entries.size(); ++index)
      {
        auto stored = heap_.set_element(*data, index, entries[index].value);
        if (!stored) return std::unexpected(stored.error());
      }
      return true;
    };

    const auto try_long_bit_permutation_intrinsic =
        [this](const Invocation& candidate)
        -> Result<std::optional<Value>>
    {
      if (!specialized_intrinsics_requested())
        return std::optional<Value>{};
      if (candidate.method.owner == nullptr ||
          candidate.method.method == nullptr || candidate.has_receiver ||
          candidate.arguments.size() != 5U ||
          !matches_long_bit_permutation_intrinsic(*candidate.method.method))
      {
        return std::optional<Value>{};
      }

      const auto& bytecode = candidate.method.method->code->bytecode;
      const u16 field_index = static_cast<u16>(
          (static_cast<u16>(bytecode[16U]) << 8U) |
          static_cast<u16>(bytecode[17U]));
      auto mask_reference = candidate.method.owner->member_reference(field_index);
      if (!mask_reference)
        return std::unexpected(mask_reference.error());
      if (mask_reference->descriptor != "[J")
        return std::optional<Value>{};
      auto mask_field = states_.resolve_field(mask_reference->owner,
                                              mask_reference->name,
                                              mask_reference->descriptor,
                                              true);
      if (!mask_field)
        return std::unexpected(mask_field.error());
      auto mask_value = states_.static_field(*mask_field);
      if (!mask_value)
        return std::unexpected(mask_value.error());

      auto input = candidate.arguments[0].as_long();
      auto first_position = candidate.arguments[1].as_int();
      auto last_position = candidate.arguments[2].as_int();
      auto shift_reference = candidate.arguments[3].as_reference();
      auto masks = mask_value->as_reference();
      if (!input || !first_position || !last_position ||
          !shift_reference || !masks || shift_reference->is_null() ||
          masks->is_null())
      {
        // Preserve normal Java exception behavior for malformed/null inputs.
        return std::optional<Value>{};
      }
      auto shift_count = heap_.array_length(*shift_reference);
      auto mask_count = heap_.array_length(*masks);
      if (!shift_count || !mask_count)
      {
        return std::optional<Value>{};
      }
      if (*mask_count < *shift_count)
      {
        return std::optional<Value>{};
      }

      const u64 source = static_cast<u64>(*input);
      u64 accumulated = 0U;
      for (usize index = 0; index < *shift_count; ++index)
      {
        auto mask_element = heap_.element(*masks, index);
        auto shift_element = heap_.element(*shift_reference, index);
        if (!mask_element || !shift_element)
        {
          return std::optional<Value>{};
        }
        auto mask = mask_element->as_long();
        auto shift = shift_element->as_int();
        if (!mask || !shift)
        {
          return std::optional<Value>{};
        }
        u64 selected = source & static_cast<u64>(*mask);
        if (selected == 0U) continue;
        if (*shift > 0)
        {
          selected >>= static_cast<u32>(*shift) & 63U;
        }
        else if (*shift < 0)
        {
          const u32 distance =
              (0U - static_cast<u32>(*shift)) & 63U;
          selected <<= distance;
        }
        accumulated |= selected;
      }

      const i32 leading_shift = static_cast<i32>(
          63U - static_cast<u32>(*last_position));
      if (leading_shift > 0)
      {
        accumulated <<= static_cast<u32>(leading_shift) & 63U;
      }
      const i32 trailing_shift = static_cast<i32>(
          static_cast<u32>(*first_position) + 63U -
          static_cast<u32>(*last_position));
      if (trailing_shift > 0)
      {
        accumulated >>= static_cast<u32>(trailing_shift) & 63U;
      }
      return std::optional<Value>(
          Value::from_long(std::bit_cast<i64>(accumulated)));
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
      PerformanceCounters::record_exception_dispatch();
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
      std::string throwable_message;
      auto message_value = heap_.field(throwable, 0U);
      if (message_value)
      {
        auto message_reference = message_value->as_reference();
        if (message_reference && !message_reference->is_null())
        {
          auto text = heap_.string_value(*message_reference);
          if (text)
          {
            throwable_message.assign(text->begin(), text->end());
          }
        }
      }
      vm_trace("exception", "throw %s%s%s from %s",
               throwable_class->c_str(),
               throwable_message.empty() ? "" : ": ",
               throwable_message.c_str(),
               exception_context.c_str());
      if (vm_trace_enabled())
      {
        for (usize depth = 0U; depth < frames.size(); ++depth)
        {
          const ExecutionFrame& trace_frame =
              frames[frames.size() - 1U - depth];
          vm_trace("exception-stack", "#%zu %s.%s%s pc=%zu",
                   depth,
                   trace_frame.owner().name().c_str(),
                   trace_frame.method().name.c_str(),
                   trace_frame.method().descriptor.c_str(),
                   trace_frame.current_instruction_pc());
        }
      }
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
                                    usize throw_pc,
                                    std::string_view message = {})
        -> Result<std::optional<ExecutionResult>>
    {
      auto throwable = message.empty()
          ? create_throwable(class_name)
          : create_throwable(class_name, message);
      if (!throwable)
      {
        return std::unexpected(throwable.error());
      }
      return dispatch_exception(*throwable, throw_pc);
    };
    const auto raise_resolution_error =
        [&raise_implicit](const Error& error, usize throw_pc)
        -> Result<std::optional<ExecutionResult>>
    {
      if (error.code != ErrorCode::java_exception ||
          error.java_exception_class.empty())
      {
        return std::unexpected(error);
      }
      return raise_implicit(error.java_exception_class,
                            throw_pc,
                            error.message);
    };

    while (!frames.empty())
    {
      const bool maintenance_boundary =
          (executed & (kMaintenancePollInterval - 1U)) == 0U;
      if (maintenance_boundary && scheduler_.current_stop_requested())
      {
        return fail(ErrorCode::invalid_state,
                    "VM execution was cancelled by scheduler shutdown");
      }

      const bool quantum_boundary = executed == next_scheduler_quantum;
      const bool automatic_collection =
          quantum_boundary && heap_.automatic_collection_due();
      bool collect_requested = false;
      if (maintenance_boundary &&
          gc_requested_.load(std::memory_order_relaxed))
      {
        collect_requested =
            gc_requested_.exchange(false, std::memory_order_acq_rel);
      }
      if (quantum_boundary || collect_requested || automatic_collection)
      {
        std::vector<ObjectRef> published_roots;
        published_roots.reserve(frames.size() * 8U + 8U);
        for (const ExecutionFrame& active_frame : frames)
          active_frame.append_reference_roots(published_roots);
        publish_execution_roots(invocation_depth, published_roots);
        if (collect_requested || automatic_collection)
        {
          auto collected = collect_active_garbage();
          if (!collected)
            return std::unexpected(collected.error());
        }
        if (quantum_boundary)
        {
          next_scheduler_quantum += kSchedulerQuantum;
          PerformanceCounters::record_scheduler_quantum();
          scheduler_.cooperative_quantum(*this);
        }
      }

      const bool progress_watchdog =
          budget_mode == InstructionBudgetMode::progress_watchdog;
      const bool budget_exhausted = progress_watchdog
          ? watchdog_instructions >= instruction_budget ||
                executed >= progress_total_budget
          : executed >= instruction_budget;
      if (budget_exhausted)
      {
        PerformanceCounters::record_instruction_budget_exit();
        const ExecutionFrame& exhausted_frame = frames.back();
        return fail(
            ErrorCode::invalid_state,
            std::string(progress_watchdog
                            ? "VM progress watchdog was exhausted in "
                            : "VM instruction budget was exhausted in ") +
                exhausted_frame.owner().name() + "." +
                exhausted_frame.method().name +
                exhausted_frame.method().descriptor +
                " at bytecode " +
                std::to_string(exhausted_frame.current_instruction_pc()));
      }
      if (frames.size() > kMaximumCallDepth)
      {
        return fail(ErrorCode::invalid_state,
                    "VM call stack exceeded its maximum depth");
      }
      ++executed;
      if (watchdog_instructions != std::numeric_limits<u64>::max())
        ++watchdog_instructions;
      accounted_instructions = executed;

      ExecutionFrame &frame = frames.back();
      const usize opcode_pc = frame.pc();
      frame.begin_instruction(opcode_pc);
      current_heap_access_pc = opcode_pc;
      if (heap_access_owner != &frame.owner() ||
          heap_access_method != &frame.method())
      {
        heap_access_owner = &frame.owner();
        heap_access_method = &frame.method();
        set_heap_access_context(HeapAccessContext {
            .owner = frame.owner().name(),
            .method = frame.method().name,
            .descriptor = frame.method().descriptor,
            .bytecode_pc = opcode_pc,
            .live_bytecode_pc = &current_heap_access_pc,
        });
      }
      auto opcode_result = frame.read_opcode();
      if (!opcode_result)
      {
        return std::unexpected(opcode_result.error());
      }
      const u8 opcode = *opcode_result;
      PerformanceCounters::record_opcode(opcode);

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
        auto immediate = frame.read_immediate(false);
        if (!immediate)
          return std::unexpected(immediate.error());
        auto pushed = frame.push(Value::from_int(*immediate));
        if (!pushed)
          return std::unexpected(pushed.error());
        break;
      }
      case 0x11:
      {
        auto immediate = frame.read_immediate(true);
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
        auto index = frame.read_ldc_index(opcode != 0x12);
        if (!index)
          return std::unexpected(index.error());
        auto value = load_constant(frame.owner(), *index, opcode == 0x14);
        if (!value)
          return std::unexpected(value.error());
        if (opcode != 0x14)
        {
          auto constant = frame.owner().constant(*index);
          const auto service = translation_service();
          if (constant &&
              (*constant)->kind == classfile::ConstantKind::string_ref &&
              service && service->enabled())
          {
            std::vector<std::vector<char32_t>> candidates;
            candidates.reserve(6U);
            frame.append_upcoming_translation_candidates(candidates);
            for (const auto& candidate : candidates)
              service->prefetch(candidate);
          }
        }
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
        auto index = frame.read_local_index();
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
        auto index = frame.read_local_index();
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
        auto operands = frame.read_increment_operands();
        if (!operands)
          return std::unexpected(operands.error());
        auto current = frame.local(operands->local_index);
        if (!current)
          return std::unexpected(current.error());
        auto integer = current->as_int();
        if (!integer)
          return std::unexpected(integer.error());
        const i32 updated = static_cast<i32>(
            static_cast<u32>(*integer) +
            static_cast<u32>(operands->increment));
        auto stored = frame.set_local(
            operands->local_index, Value::from_int(updated));
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
        auto offset = frame.read_branch_offset(false);
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
        auto offset = frame.read_branch_offset(false);
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
        auto offset = frame.read_branch_offset(false);
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
        auto offset = frame.read_branch_offset(false);
        if (!offset)
          return std::unexpected(offset.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xA8:
      {
        auto offset = frame.read_branch_offset(false);
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
        auto index = frame.read_local_index();
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
        auto decoded_table = frame.read_switch_table();
        if (!decoded_table)
          return std::unexpected(decoded_table.error());
        if (*decoded_table != nullptr)
        {
          const DecodedSwitchTable& table = **decoded_table;
          u32 target_bci = table.default_target_bci;
          const i64 entry_index = static_cast<i64>(*key) -
                                  static_cast<i64>(table.low);
          if (entry_index >= 0 &&
              static_cast<u64>(entry_index) < table.entries.size())
          {
            target_bci = table.entries[
                static_cast<usize>(entry_index)].target_bci;
          }
          auto jumped = frame.jump_absolute(target_bci);
          if (!jumped)
            return std::unexpected(jumped.error());
          break;
        }
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
        auto decoded_table = frame.read_switch_table();
        if (!decoded_table)
          return std::unexpected(decoded_table.error());
        if (*decoded_table != nullptr)
        {
          const DecodedSwitchTable& table = **decoded_table;
          u32 target_bci = table.default_target_bci;
          const auto found = std::lower_bound(
              table.entries.begin(),
              table.entries.end(),
              *key,
              [](const DecodedSwitchEntry& entry, i32 match)
              {
                return entry.match < match;
              });
          if (found != table.entries.end() && found->match == *key)
            target_bci = found->target_bci;
          auto jumped = frame.jump_absolute(target_bci);
          if (!jumped)
            return std::unexpected(jumped.error());
          break;
        }
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
        auto index = frame.read_constant_pool_index();
        if (!index)
          return std::unexpected(index.error());
        const bool is_static = opcode == 0xB2 || opcode == 0xB3;
        refresh_metadata_bindings_if_needed();
        std::shared_ptr<const FieldLocation> field;
        const MethodId method_id = frame.runtime_method_id();
        const u32 operand_index = frame.current_decoded_operand_index();
        if (method_id.valid() && operand_index != kInvalidDecodedIndex)
        {
          auto cached_entry = operand_resolution_entry(method_id,
                                                       operand_index,
                                                       opcode_pc);
          if (!cached_entry)
            return std::unexpected(cached_entry.error());
          OperandResolutionEntry& entry = **cached_entry;
          if (entry.state == OperandResolutionState::resolved)
          {
            if (entry.kind != OperandResolutionKind::field ||
                entry.field == nullptr)
            {
              return fail(ErrorCode::internal_error,
                          "decoded field operand cache has wrong kind");
            }
            PerformanceCounters::record_operand_resolution(true);
            field = entry.field;
          }
          else if (entry.state == OperandResolutionState::failed)
          {
            if (entry.kind != OperandResolutionKind::field ||
                !entry.failure.has_value())
            {
              return fail(ErrorCode::internal_error,
                          "decoded failed field operand has no error");
            }
            PerformanceCounters::record_operand_resolution(true);
            PerformanceCounters::record_operand_resolution_failure();
            auto raised = raise_resolution_error(*entry.failure, opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          else if (entry.state == OperandResolutionState::resolving)
          {
            return fail(ErrorCode::invalid_state,
                        "recursive decoded field resolution");
          }
          else
          {
            PerformanceCounters::record_operand_resolution(false);
            auto began = entry.begin(OperandResolutionKind::field);
            if (!began)
              return std::unexpected(began.error());
            const auto cache_failure = [&](Error error)
                -> Result<std::optional<ExecutionResult>>
            {
              auto cached = entry.fail_resolution(stable_linkage_error(
                  std::move(error), OperandResolutionKind::field));
              if (!cached)
                return std::unexpected(cached.error());
              PerformanceCounters::record_operand_resolution_failure();
              return raise_resolution_error(*entry.failure, opcode_pc);
            };
            auto reference = frame.owner().member_reference(*index);
            if (!reference)
            {
              auto raised = cache_failure(reference.error());
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            if (reference->kind != classfile::ConstantKind::field_ref)
            {
              auto raised = cache_failure(Error::make(
                  ErrorCode::malformed_class,
                  "field opcode references a non-field constant"));
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            auto resolved = states_.resolve_field(reference->owner,
                                                  reference->name,
                                                  reference->descriptor,
                                                  is_static);
            if (!resolved)
            {
              auto raised = cache_failure(resolved.error());
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            auto resolved_field = std::make_shared<const FieldLocation>(
                std::move(*resolved));
            auto completed = entry.resolve_field(resolved_field);
            if (!completed)
              return std::unexpected(completed.error());
            field = std::move(resolved_field);
          }
        }
        else
        {
          const u32 binding_key =
              (static_cast<u32>(*index) << 1U) | (is_static ? 1U : 0U);
          auto &owner_bindings = field_bindings_[&frame.owner()];
          if (const auto cached = owner_bindings.find(binding_key);
              cached != owner_bindings.end())
          {
            field = cached->second;
          }
          else
          {
            auto reference = frame.owner().member_reference(*index);
            if (!reference)
              return std::unexpected(reference.error());
            if (reference->kind != classfile::ConstantKind::field_ref)
            {
              return fail(ErrorCode::malformed_class,
                          "field opcode references a non-field constant");
            }
            auto resolved = states_.resolve_field(reference->owner,
                                                  reference->name,
                                                  reference->descriptor,
                                                  is_static);
            if (!resolved)
            {
              auto raised = raise_resolution_error(stable_linkage_error(
                  resolved.error(), OperandResolutionKind::field), opcode_pc);
              if (!raised)
                return std::unexpected(raised.error());
              if (raised->has_value())
                return std::move(**raised);
              break;
            }
            field = std::make_shared<const FieldLocation>(std::move(*resolved));
            owner_bindings.emplace(binding_key, field);
          }
        }
        if (is_static)
        {
          auto initialized = ensure_initialized_from_execution(
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
        std::optional<u16> index;
        std::optional<u8> interface_count;
        if (opcode == 0xB9)
        {
          auto operands = frame.read_invokeinterface_operands();
          if (!operands)
            return std::unexpected(operands.error());
          index = operands->constant_pool_index;
          interface_count = operands->count;
        }
        else
        {
          auto constant_pool_index = frame.read_constant_pool_index();
          if (!constant_pool_index)
            return std::unexpected(constant_pool_index.error());
          index = *constant_pool_index;
        }
        refresh_metadata_bindings_if_needed();
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
        auto descriptor = classes_.metadata().method_descriptor(
            reference->descriptor);
        if (!descriptor)
          return std::unexpected(descriptor.error());
        if (interface_count.has_value())
        {
          const usize expected_slots =
              (*descriptor)->argument_slots_with_receiver;
          if (expected_slots >
                  static_cast<usize>(std::numeric_limits<u8>::max()) ||
              *interface_count != static_cast<u8>(expected_slots))
          {
            return fail(ErrorCode::malformed_class,
                        "invokeinterface count does not match descriptor");
          }
        }
        auto arguments = pop_arguments(
            frame, (*descriptor)->descriptor, !is_static);
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
          auto initialized = ensure_initialized_from_execution(
              reference->owner,
              instruction_budget - executed,
              *arguments);
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
          auto initialized = ensure_initialized_from_execution(
              *class_name,
              instruction_budget - executed,
              *arguments);
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
          std::shared_ptr<const RuntimeClass> dispatch_metadata;
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
            dispatch_metadata = classes_.metadata().find_class(dispatch_class);
            if (dispatch_metadata == nullptr)
            {
              auto loaded_dispatch_class = classes_.load(dispatch_class);
              if (!loaded_dispatch_class)
                return std::unexpected(loaded_dispatch_class.error());
              dispatch_metadata = classes_.metadata().find_class(dispatch_class);
            }
          }
          OperandResolutionEntry* direct_operand_entry = nullptr;
          OperandResolutionEntry* virtual_operand_entry = nullptr;
          DirectCallCache* legacy_direct_cache = nullptr;
          VirtualCallCache* virtual_cache = nullptr;
          std::optional<ResolvedMethod> inline_target;
          std::optional<NativeMethodId> prebound_native_method;
          const bool cacheable_direct_call = is_static || opcode == 0xB7;
          if (cacheable_direct_call)
          {
            const MethodId caller_method_id = frame.runtime_method_id();
            const u32 caller_operand_index =
                frame.current_decoded_operand_index();
            if (caller_method_id.valid() &&
                caller_operand_index != kInvalidDecodedIndex)
            {
              auto cached_entry = operand_resolution_entry(
                  caller_method_id,
                  caller_operand_index,
                  opcode_pc);
              if (!cached_entry)
                return std::unexpected(cached_entry.error());
              OperandResolutionEntry& entry = **cached_entry;
              direct_operand_entry = &entry;
              if (entry.state == OperandResolutionState::resolved)
              {
                if (entry.kind != OperandResolutionKind::direct_call ||
                    !entry.target_method.valid())
                {
                  return fail(ErrorCode::internal_error,
                              "decoded direct call cache has wrong kind");
                }
                auto runtime_target = classes_.metadata().find_method(
                    entry.target_method);
                if (runtime_target == nullptr)
                {
                  entry.reset();
                  PerformanceCounters::record_direct_call_cache(false);
                  PerformanceCounters::record_operand_resolution(false);
                  auto began = entry.begin(
                      OperandResolutionKind::direct_call);
                  if (!began)
                    return std::unexpected(began.error());
                }
                else
                {
                  const u64 current_native_generation = natives_.generation();
                  if (!entry.native_binding_cached ||
                      entry.native_generation != current_native_generation)
                  {
                    const NativeMethodBinding native_binding =
                        resolve_native_binding(*runtime_target->owner,
                                               *runtime_target->method);
                    auto updated = entry.update_native_binding(
                        native_binding.id,
                        native_binding.generation);
                    if (!updated)
                      return std::unexpected(updated.error());
                  }
                  prebound_native_method = entry.target_native_method;
                  inline_target = ResolvedMethod {
                      .owner = runtime_target->owner,
                      .method = runtime_target->method,
                      .runtime = std::move(runtime_target),
                  };
                  PerformanceCounters::record_direct_call_cache(true);
                  PerformanceCounters::record_operand_resolution(true);
                }
              }
              else if (entry.state == OperandResolutionState::failed)
              {
                if (entry.kind != OperandResolutionKind::direct_call ||
                    !entry.failure.has_value())
                {
                  return fail(ErrorCode::internal_error,
                              "decoded failed direct call has no error");
                }
                PerformanceCounters::record_direct_call_cache(true);
                PerformanceCounters::record_operand_resolution(true);
                PerformanceCounters::record_operand_resolution_failure();
                auto raised = raise_resolution_error(*entry.failure, opcode_pc);
                if (!raised)
                  return std::unexpected(raised.error());
                if (raised->has_value())
                  return std::move(**raised);
                break;
              }
              else if (entry.state == OperandResolutionState::resolving)
              {
                return fail(ErrorCode::invalid_state,
                            "recursive decoded direct call resolution");
              }
              else
              {
                PerformanceCounters::record_direct_call_cache(false);
                PerformanceCounters::record_operand_resolution(false);
                auto began = entry.begin(
                    OperandResolutionKind::direct_call);
                if (!began)
                  return std::unexpected(began.error());
              }
            }
            else
            {
              auto &cache = direct_call_bindings_[&frame.owner()][
                  static_cast<u32>(*index)];
              legacy_direct_cache = &cache;
              if (cache.valid)
              {
                auto runtime_target = classes_.metadata().find_method(
                    cache.target_method);
                if (runtime_target != nullptr)
                {
                  inline_target = ResolvedMethod {
                      .owner = runtime_target->owner,
                      .method = runtime_target->method,
                      .runtime = std::move(runtime_target),
                  };
                  PerformanceCounters::record_direct_call_cache(true);
                }
                else
                {
                  cache.valid = false;
                  PerformanceCounters::record_direct_call_cache(false);
                }
              }
              else
              {
                PerformanceCounters::record_direct_call_cache(false);
              }
            }
          }

          const bool cacheable_virtual_call =
              !is_static && opcode != 0xB7 && dispatch_metadata != nullptr;
          if (cacheable_virtual_call)
          {
            const MethodId caller_method_id = frame.runtime_method_id();
            const u32 caller_operand_index =
                frame.current_decoded_operand_index();
            if (caller_method_id.valid() &&
                caller_operand_index != kInvalidDecodedIndex)
            {
              auto cached_entry = operand_resolution_entry(
                  caller_method_id,
                  caller_operand_index,
                  opcode_pc);
              if (!cached_entry)
                return std::unexpected(cached_entry.error());
              OperandResolutionEntry& entry = **cached_entry;
              virtual_operand_entry = &entry;
              const auto begin_for_receiver = [&]() -> Status
              {
                entry.reset();
                PerformanceCounters::record_virtual_inline_cache(false);
                PerformanceCounters::record_operand_resolution(false);
                return entry.begin_virtual_call(dispatch_metadata->id);
              };
              if (entry.state == OperandResolutionState::resolved)
              {
                if (entry.kind != OperandResolutionKind::virtual_call ||
                    !entry.receiver_class.valid() ||
                    !entry.target_method.valid())
                {
                  return fail(ErrorCode::internal_error,
                              "decoded virtual call cache has wrong kind");
                }
                if (entry.receiver_class != dispatch_metadata->id)
                {
                  auto began = begin_for_receiver();
                  if (!began)
                    return std::unexpected(began.error());
                }
                else
                {
                  auto runtime_target = classes_.metadata().find_method(
                      entry.target_method);
                  if (runtime_target == nullptr)
                  {
                    auto began = begin_for_receiver();
                    if (!began)
                      return std::unexpected(began.error());
                  }
                  else
                  {
                    const u64 current_native_generation = natives_.generation();
                    if (!entry.native_binding_cached ||
                        entry.native_generation != current_native_generation)
                    {
                      const NativeMethodBinding native_binding =
                          resolve_native_binding(*runtime_target->owner,
                                                 *runtime_target->method);
                      auto updated = entry.update_native_binding(
                          native_binding.id,
                          native_binding.generation);
                      if (!updated)
                        return std::unexpected(updated.error());
                    }
                    prebound_native_method = entry.target_native_method;
                    inline_target = ResolvedMethod {
                        .owner = runtime_target->owner,
                        .method = runtime_target->method,
                        .runtime = std::move(runtime_target),
                    };
                    PerformanceCounters::record_virtual_inline_cache(true);
                    PerformanceCounters::record_operand_resolution(true);
                  }
                }
              }
              else if (entry.state == OperandResolutionState::failed)
              {
                if (entry.kind != OperandResolutionKind::virtual_call ||
                    !entry.receiver_class.valid() ||
                    !entry.failure.has_value())
                {
                  return fail(ErrorCode::internal_error,
                              "decoded failed virtual call has no error");
                }
                if (entry.receiver_class != dispatch_metadata->id)
                {
                  auto began = begin_for_receiver();
                  if (!began)
                    return std::unexpected(began.error());
                }
                else
                {
                  PerformanceCounters::record_virtual_inline_cache(true);
                  PerformanceCounters::record_operand_resolution(true);
                  PerformanceCounters::record_operand_resolution_failure();
                  auto raised = raise_resolution_error(
                      *entry.failure, opcode_pc);
                  if (!raised)
                    return std::unexpected(raised.error());
                  if (raised->has_value())
                    return std::move(**raised);
                  break;
                }
              }
              else if (entry.state == OperandResolutionState::resolving)
              {
                return fail(ErrorCode::invalid_state,
                            "recursive decoded virtual call resolution");
              }
              else
              {
                PerformanceCounters::record_virtual_inline_cache(false);
                PerformanceCounters::record_operand_resolution(false);
                auto began = entry.begin_virtual_call(dispatch_metadata->id);
                if (!began)
                  return std::unexpected(began.error());
              }
            }
            else
            {
              auto &cache = virtual_call_bindings_[&frame.owner()][
                  static_cast<u32>(*index)];
              virtual_cache = &cache;
              if (cache.valid &&
                  cache.receiver_class == dispatch_metadata->id)
              {
                auto runtime_target = classes_.metadata().find_method(
                    cache.target_method);
                if (runtime_target != nullptr)
                {
                  inline_target = ResolvedMethod {
                      .owner = runtime_target->owner,
                      .method = runtime_target->method,
                      .runtime = std::move(runtime_target),
                  };
                  PerformanceCounters::record_virtual_inline_cache(true);
                }
                else
                {
                  cache.valid = false;
                  PerformanceCounters::record_virtual_inline_cache(false);
                }
              }
              else
              {
                PerformanceCounters::record_virtual_inline_cache(false);
              }
            }
          }

          const auto resolve_target = [&]() -> Result<ResolvedMethod>
          {
            if (inline_target.has_value())
              return *inline_target;
            if (opcode == 0xB7 && reference->name == "<init>")
            {
              return classes_.resolve_declared_method(
                  reference->owner,
                  reference->name,
                  reference->descriptor);
            }
            return classes_.resolve_method(dispatch_class,
                                           reference->name,
                                           reference->descriptor);
          };
          auto target = resolve_target();
          if (!target)
          {
            const OperandResolutionKind resolution_kind =
                virtual_operand_entry != nullptr
                    ? OperandResolutionKind::virtual_call
                    : OperandResolutionKind::direct_call;
            Error linkage_error = stable_linkage_error(
                target.error(), resolution_kind);
            OperandResolutionEntry* failed_entry =
                direct_operand_entry != nullptr
                    ? direct_operand_entry
                    : virtual_operand_entry;
            if (failed_entry != nullptr &&
                failed_entry->state == OperandResolutionState::resolving)
            {
              auto cached = failed_entry->fail_resolution(linkage_error);
              if (!cached)
                return std::unexpected(cached.error());
              PerformanceCounters::record_operand_resolution_failure();
              linkage_error = *failed_entry->failure;
            }
            auto raised = raise_resolution_error(linkage_error, opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          if (direct_operand_entry != nullptr &&
              !inline_target.has_value())
          {
            if (target->runtime == nullptr)
            {
              direct_operand_entry->reset();
            }
            else
            {
              const NativeMethodBinding native_binding =
                  resolve_native_binding(*target->owner, *target->method);
              auto completed = direct_operand_entry->resolve_direct_call(
                  target->runtime->id,
                  native_binding.id,
                  native_binding.generation);
              if (!completed)
                return std::unexpected(completed.error());
              prebound_native_method = native_binding.id;
            }
          }
          if (virtual_operand_entry != nullptr &&
              !inline_target.has_value())
          {
            if (target->runtime == nullptr)
            {
              virtual_operand_entry->reset();
            }
            else
            {
              const NativeMethodBinding native_binding =
                  resolve_native_binding(*target->owner, *target->method);
              auto completed = virtual_operand_entry->resolve_virtual_call(
                  target->runtime->id,
                  native_binding.id,
                  native_binding.generation);
              if (!completed)
                return std::unexpected(completed.error());
              prebound_native_method = native_binding.id;
            }
          }
          if (legacy_direct_cache != nullptr &&
              !inline_target.has_value() && target->runtime != nullptr)
          {
            *legacy_direct_cache = DirectCallCache {
                .target_method = target->runtime->id,
                .valid = true,
            };
          }
          if (virtual_cache != nullptr && !inline_target.has_value() &&
              target->runtime != nullptr)
          {
            *virtual_cache = VirtualCallCache {
                .receiver_class = dispatch_metadata->id,
                .target_method = target->runtime->id,
                .valid = true,
            };
          }
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
          auto prepared = prepare_invocation(
              std::move(*target),
              *arguments,
              !is_static,
              prebound_native_method);
          if (!prepared)
            return std::unexpected(prepared.error());
          nested = std::move(*prepared);
        }
        const bool nested_is_native = nested->native_method.valid() ||
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
        if (!nested_is_native)
        {
          auto range_decoded = try_range_decoder_bit_intrinsic(*nested);
          if (!range_decoded)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return std::unexpected(range_decoded.error());
          }
          if (range_decoded->has_value())
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            if (budget_mode == InstructionBudgetMode::progress_watchdog)
              watchdog_instructions = 0U;
            auto pushed = frame.push(**range_decoded);
            if (!pushed)
              return std::unexpected(pushed.error());
            break;
          }
          auto sorted = try_vector_key_sort_intrinsic(*nested);
          if (!sorted)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return std::unexpected(sorted.error());
          }
          if (*sorted)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            if (budget_mode == InstructionBudgetMode::progress_watchdog)
              watchdog_instructions = 0U;
            break;
          }
          auto intrinsic = try_long_bit_permutation_intrinsic(*nested);
          if (!intrinsic)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return std::unexpected(intrinsic.error());
          }
          if (intrinsic->has_value())
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            if (budget_mode == InstructionBudgetMode::progress_watchdog)
              watchdog_instructions = 0U;
            auto pushed = frame.push(**intrinsic);
            if (!pushed)
              return std::unexpected(pushed.error());
            break;
          }

          if (nested->method.runtime != nullptr &&
              nested->method.owner != nullptr &&
              nested->descriptor != nullptr &&
              !nested->return_override.has_value())
          {
            const u64 remaining_budget = instruction_budget - executed;
            auto jitted = jit_.try_execute(
                nested->method.runtime->id,
                *nested->method.owner,
                *nested->method.method,
                *nested->descriptor,
                nested->arguments,
                nested->has_receiver,
                remaining_budget);
            if (!jitted)
            {
              auto released = release_synchronized_monitor(*nested_monitor);
              if (!released)
                return std::unexpected(released.error());
              return std::unexpected(jitted.error());
            }
            if (jitted->has_value())
            {
              const u64 jit_instructions =
                  static_cast<u64>((*jitted)->bytecode_instructions);
              executed += jit_instructions;
              accounted_instructions = executed;
              auto released = release_synchronized_monitor(*nested_monitor);
              if (!released)
                return std::unexpected(released.error());
              if (budget_mode == InstructionBudgetMode::progress_watchdog)
                watchdog_instructions = 0U;
              if ((*jitted)->return_value.has_value())
              {
                auto pushed = frame.push(*(*jitted)->return_value);
                if (!pushed)
                  return std::unexpected(pushed.error());
              }
              break;
            }
          }
        }
        if (nested_is_native)
        {
          if (std::getenv("PHONEME_TRACE_NETWORK_CALLERS") != nullptr &&
              nested->method.method != nullptr &&
              (nested->method.method->name == "write" ||
               nested->method.method->name == "flush") &&
              nested->method.owner != nullptr)
          {
            const std::string_view native_owner =
                nested->method.owner->name();
            if (native_owner.find("java/io/") != std::string_view::npos ||
                native_owner.find("javax/microedition/io/") !=
                    std::string_view::npos)
            {
              std::fprintf(stderr,
                           "[phoneMENetworkCaller] caller=%s.%s%s bci=%zu "
                           "target=%s.%s%s\n",
                           frame.owner().name().c_str(),
                           frame.method().name.c_str(),
                           frame.method().descriptor.c_str(),
                           opcode_pc,
                           nested->method.owner->name().c_str(),
                           nested->method.method->name.c_str(),
                           nested->method.method->descriptor.c_str());
            }
          }
          const auto native_started = std::chrono::steady_clock::now();
          auto native_result = invoke_native(*nested);
          const auto native_duration =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - native_started);
          if (native_duration >= std::chrono::milliseconds(10) &&
              should_trace_slow_native(nested->method.owner->name(),
                                       nested->method.method->name))
          {
            vm_trace("native",
                     "slow-call java=%u target=%s.%s%s duration_us=%lld ok=%d",
                     static_cast<unsigned>(scheduler_.current_thread_id()),
                     nested->method.owner->name().c_str(),
                     nested->method.method->name.c_str(),
                     nested->method.method->descriptor.c_str(),
                     static_cast<long long>(native_duration.count()),
                     native_result.has_value() ? 1 : 0);
          }
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
                  opcode_pc,
                  native_result.error().message);
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
          if (budget_mode == InstructionBudgetMode::progress_watchdog)
            watchdog_instructions = 0U;
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
          if (nested->descriptor == nullptr)
          {
            auto released = release_synchronized_monitor(*nested_monitor);
            if (!released)
              return std::unexpected(released.error());
            return fail(ErrorCode::internal_error,
                        "nested invocation has no cached descriptor");
          }
          auto next = ExecutionFrame::make(
              std::move(nested->method),
              nested->descriptor->descriptor,
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
          PerformanceCounters::observe_java_call_depth(frames.size());
        }
        break;
      }
      case 0xBA:
      {
        auto index = frame.read_invokedynamic_index();
        if (!index)
          return std::unexpected(index.error());
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
        auto index = frame.read_constant_pool_index();
        if (!index)
          return std::unexpected(index.error());
        auto class_operand = resolve_class_operand(
            frame.runtime_method_id(),
            frame.current_decoded_operand_index(),
            opcode_pc,
            frame.owner(),
            *index,
            true,
            false);
        if (!class_operand)
        {
          auto raised = raise_resolution_error(class_operand.error(), opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }

        std::string legacy_class_name;
        std::string_view class_name;
        std::shared_ptr<const classfile::ClassFile> allocated_class;
        if (*class_operand != nullptr)
        {
          class_name = (*class_operand)->target_class_name;
          allocated_class = (*class_operand)->target_class_file;
        }
        else
        {
          auto resolved_name = frame.owner().class_name_constant(*index);
          if (!resolved_name)
            return std::unexpected(resolved_name.error());
          legacy_class_name = std::move(*resolved_name);
          class_name = legacy_class_name;
          auto loaded = load_linkage_class(class_name);
          if (!loaded)
          {
            auto raised = raise_resolution_error(stable_linkage_error(
                loaded.error(), OperandResolutionKind::class_reference),
                opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          allocated_class = std::move(*loaded);
        }
        if ((allocated_class->access_flags() &
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
        auto initialized = ensure_initialized_from_execution(
            class_name,
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
        auto object = allocate_instance_with_gc(class_name);
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
        auto atype = frame.read_newarray_type();
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
        auto index = frame.read_constant_pool_index();
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
        auto class_operand = resolve_class_operand(
            frame.runtime_method_id(),
            frame.current_decoded_operand_index(),
            opcode_pc,
            frame.owner(),
            *index,
            true,
            true);
        if (!class_operand)
        {
          auto raised = raise_resolution_error(class_operand.error(), opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        std::string legacy_array_name;
        std::string_view array_name;
        if (*class_operand != nullptr)
        {
          array_name = (*class_operand)->target_array_name;
        }
        else
        {
          auto component = frame.owner().class_name_constant(*index);
          if (!component)
            return std::unexpected(component.error());
          auto loaded_component = load_linkage_class(*component);
          if (!loaded_component)
          {
            auto raised = raise_resolution_error(stable_linkage_error(
                loaded_component.error(),
                OperandResolutionKind::class_reference), opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
          if (component->starts_with('['))
            legacy_array_name = '[' + *component;
          else
            legacy_array_name = "[L" + *component + ';';
          array_name = legacy_array_name;
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
        auto index = frame.read_constant_pool_index();
        auto reference = pop_reference(frame);
        if (!index || !reference)
        {
          return fail(ErrorCode::malformed_class,
                      "invalid type-check operands");
        }
        auto class_operand = resolve_class_operand(
            frame.runtime_method_id(),
            frame.current_decoded_operand_index(),
            opcode_pc,
            frame.owner(),
            *index,
            true,
            false);
        if (!class_operand)
        {
          auto raised = raise_resolution_error(class_operand.error(), opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        std::string legacy_target_class;
        std::string_view target_class;
        if (*class_operand != nullptr)
        {
          target_class = (*class_operand)->target_class_name;
        }
        else
        {
          auto resolved_target = frame.owner().class_name_constant(*index);
          if (!resolved_target)
            return std::unexpected(resolved_target.error());
          legacy_target_class = std::move(*resolved_target);
          target_class = legacy_target_class;
          auto loaded_target = load_linkage_class(target_class);
          if (!loaded_target)
          {
            auto raised = raise_resolution_error(stable_linkage_error(
                loaded_target.error(),
                OperandResolutionKind::class_reference), opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
        }

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
                                                 target_class);
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
                                target_class) !=
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
                  opcode_pc,
                  exited.error().message);
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
        auto operands = frame.read_wide_operands();
        if (!operands)
          return std::unexpected(operands.error());
        const u8 widened_opcode = operands->opcode;
        const u16 index = operands->local_index;
        if (widened_opcode >= 0x15 && widened_opcode <= 0x19)
        {
          auto value = frame.local(index);
          if (!value)
            return std::unexpected(value.error());
          if (!load_kind_matches(widened_opcode, value->kind()))
          {
            return fail(ErrorCode::malformed_class,
                        "wide load does not match value kind");
          }
          auto pushed = frame.push(*value);
          if (!pushed)
            return std::unexpected(pushed.error());
        }
        else if (widened_opcode >= 0x36 && widened_opcode <= 0x3A)
        {
          auto value = frame.pop();
          if (!value)
            return std::unexpected(value.error());
          if (!store_kind_matches(widened_opcode, value->kind()))
          {
            return fail(ErrorCode::malformed_class,
                        "wide store does not match value kind");
          }
          auto stored = frame.set_local(index, *value);
          if (!stored)
            return std::unexpected(stored.error());
        }
        else if (widened_opcode == 0x84)
        {
          if (!operands->increment.has_value())
          {
            return fail(ErrorCode::malformed_class,
                        "wide iinc has no decoded increment");
          }
          auto current = frame.local(index);
          if (!current)
            return std::unexpected(current.error());
          auto integer = current->as_int();
          if (!integer)
            return std::unexpected(integer.error());
          const i32 updated = static_cast<i32>(
              static_cast<u32>(*integer) +
              static_cast<u32>(static_cast<i32>(*operands->increment)));
          auto stored = frame.set_local(index, Value::from_int(updated));
          if (!stored)
            return std::unexpected(stored.error());
        }
        else if (widened_opcode == 0xA9)
        {
          auto address_value = frame.local(index);
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
        auto operands = frame.read_multianewarray_operands();
        if (!operands)
          return std::unexpected(operands.error());
        const u8 dimensions = operands->dimensions;
        auto class_operand = resolve_class_operand(
            frame.runtime_method_id(),
            frame.current_decoded_operand_index(),
            opcode_pc,
            frame.owner(),
            operands->constant_pool_index,
            true,
            false);
        if (!class_operand)
        {
          auto raised = raise_resolution_error(class_operand.error(), opcode_pc);
          if (!raised)
            return std::unexpected(raised.error());
          if (raised->has_value())
            return std::move(**raised);
          break;
        }
        std::string legacy_array_class;
        std::string_view array_class;
        if (*class_operand != nullptr)
        {
          array_class = (*class_operand)->target_class_name;
        }
        else
        {
          auto resolved_array_class = frame.owner().class_name_constant(
              operands->constant_pool_index);
          if (!resolved_array_class)
            return std::unexpected(resolved_array_class.error());
          legacy_array_class = std::move(*resolved_array_class);
          array_class = legacy_array_class;
          auto loaded_array_class = load_linkage_class(array_class);
          if (!loaded_array_class)
          {
            auto raised = raise_resolution_error(stable_linkage_error(
                loaded_array_class.error(),
                OperandResolutionKind::class_reference), opcode_pc);
            if (!raised)
              return std::unexpected(raised.error());
            if (raised->has_value())
              return std::move(**raised);
            break;
          }
        }
        usize declared_dimensions = 0;
        while (declared_dimensions < array_class.size() &&
               array_class[declared_dimensions] == '[')
        {
          ++declared_dimensions;
        }
        if (declared_dimensions == 0U ||
            static_cast<usize>(dimensions) > declared_dimensions)
        {
          return fail(ErrorCode::malformed_class,
                      "multianewarray dimensions exceed descriptor rank");
        }

        std::vector<i32> lengths(dimensions);
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

        auto root_default = array_default_value(array_class);
        if (!root_default)
          return std::unexpected(root_default.error());
        auto root = allocate_array_with_gc(
            array_class,
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
          const std::string_view child_descriptor =
              array_class.substr(child_level);
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
        auto offset = frame.read_branch_offset(false);
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
        auto offset = frame.read_branch_offset(true);
        if (!offset)
          return std::unexpected(offset.error());
        auto branched = frame.branch(opcode_pc, *offset);
        if (!branched)
          return std::unexpected(branched.error());
        break;
      }
      case 0xC9:
      {
        auto offset = frame.read_branch_offset(true);
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
