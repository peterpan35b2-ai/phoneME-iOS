#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::classfile
{

  enum class ConstantKind : u8
  {
    unusable = 0,
    utf8 = 1,
    integer = 3,
    float32 = 4,
    long64 = 5,
    float64 = 6,
    class_ref = 7,
    string_ref = 8,
    field_ref = 9,
    method_ref = 10,
    interface_method_ref = 11,
    name_and_type = 12,
    method_handle = 15,
    method_type = 16,
    dynamic = 17,
    invoke_dynamic = 18,
    module = 19,
    package = 20,
  };

  struct Constant final
  {
    ConstantKind kind{ConstantKind::unusable};
    std::string utf8;
    u16 first{0};
    u16 second{0};
    u64 bits{0};
  };

  struct MemberReference final
  {
    ConstantKind kind{ConstantKind::unusable};
    std::string owner;
    std::string name;
    std::string descriptor;
  };

  struct MethodHandleReference final
  {
    u8 reference_kind{0};
    MemberReference member;
  };

  struct BootstrapMethod final
  {
    u16 method_handle_index{0};
    std::vector<u16> arguments;
  };

  struct InvokeDynamicReference final
  {
    u16 bootstrap_method_index{0};
    std::string name;
    std::string descriptor;
  };

  struct ExceptionHandler final
  {
    u16 start_pc{0};
    u16 end_pc{0};
    u16 handler_pc{0};
    std::string catch_type;
  };

  enum class VerificationTypeKind : u8
  {
    top = 0,
    integer = 1,
    float32 = 2,
    float64 = 3,
    long_integer = 4,
    null_reference = 5,
    uninitialized_this = 6,
    object = 7,
    uninitialized = 8,
  };

  struct VerificationType final
  {
    VerificationTypeKind kind{VerificationTypeKind::top};
    std::string class_name;
    u16 new_instruction_pc{0};
  };

  enum class StackMapFrameKind : u8
  {
    cldc_full,
    same,
    same_locals_one_stack,
    chop,
    append,
    full,
  };

  struct StackMapFrame final
  {
    StackMapFrameKind kind{StackMapFrameKind::full};
    u16 bytecode_offset{0};
    u8 chopped_locals{0};
    std::vector<VerificationType> locals;
    std::vector<VerificationType> stack;
  };

  struct CodeAttribute final
  {
    u16 max_stack{0};
    u16 max_locals{0};
    std::vector<u8> bytecode;
    std::vector<ExceptionHandler> exception_table;
    std::vector<StackMapFrame> stack_map_frames;
  };

  struct Method final
  {
    u16 access_flags{0};
    std::string name;
    std::string descriptor;
    std::optional<CodeAttribute> code;
  };

  struct Field final
  {
    u16 access_flags{0};
    std::string name;
    std::string descriptor;
    std::optional<u16> constant_value_index;
  };

  class ClassFile final
  {
  public:
    [[nodiscard]] static Result<ClassFile> parse(std::span<const u8> bytes);
    [[nodiscard]] static ClassFile builtin(
        std::string name,
        std::string super_name,
        u16 access_flags,
        std::vector<Field> fields = {},
        std::vector<Method> methods = {},
        std::vector<std::string> interfaces = {});

    [[nodiscard]] u16 minor_version() const noexcept { return minor_version_; }
    [[nodiscard]] u16 major_version() const noexcept { return major_version_; }
    [[nodiscard]] u16 access_flags() const noexcept { return access_flags_; }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    [[nodiscard]] const std::string &super_name() const noexcept { return super_name_; }
    [[nodiscard]] const std::vector<std::string> &interfaces() const noexcept
    {
      return interfaces_;
    }
    [[nodiscard]] const std::vector<Field> &fields() const noexcept { return fields_; }
    [[nodiscard]] const std::vector<Method> &methods() const noexcept { return methods_; }
    [[nodiscard]] const std::vector<Constant> &constants() const noexcept
    {
      return constants_;
    }
    [[nodiscard]] const std::vector<BootstrapMethod> &bootstrap_methods() const noexcept
    {
      return bootstrap_methods_;
    }

    [[nodiscard]] Result<const Constant *> constant(u16 index) const;
    [[nodiscard]] Result<std::string> utf8_constant(u16 index) const;
    [[nodiscard]] Result<std::string> class_name_constant(u16 index) const;
    [[nodiscard]] Result<std::string> string_constant(u16 index) const;
    [[nodiscard]] Result<MemberReference> member_reference(u16 index) const;
    [[nodiscard]] Result<MethodHandleReference> method_handle_reference(
        u16 index) const;
    [[nodiscard]] Result<std::string> method_type_descriptor(u16 index) const;
    [[nodiscard]] Result<InvokeDynamicReference> invoke_dynamic_reference(
        u16 index) const;
    [[nodiscard]] Result<const BootstrapMethod *> bootstrap_method(
        u16 index) const;
    [[nodiscard]] const Method *find_method(std::string_view name,
                                            std::string_view descriptor) const noexcept;

  private:
    struct MethodSignatureView final
    {
      std::string_view name;
      std::string_view descriptor;
    };

    struct MethodSignatureKey final
    {
      std::string name;
      std::string descriptor;
    };

    struct MethodSignatureHash final
    {
      using is_transparent = void;

      [[nodiscard]] usize operator()(const MethodSignatureKey &key) const noexcept
      {
        return (*this)(MethodSignatureView{key.name, key.descriptor});
      }

      [[nodiscard]] usize operator()(MethodSignatureView key) const noexcept
      {
        const usize first = std::hash<std::string_view>{}(key.name);
        const usize second = std::hash<std::string_view>{}(key.descriptor);
        return first ^ (second + static_cast<usize>(0x9E3779B9U) +
                        (first << 6U) + (first >> 2U));
      }
    };

    struct MethodSignatureEqual final
    {
      using is_transparent = void;

      [[nodiscard]] bool operator()(const MethodSignatureKey &left,
                                    const MethodSignatureKey &right) const noexcept
      {
        return left.name == right.name &&
               left.descriptor == right.descriptor;
      }

      [[nodiscard]] bool operator()(const MethodSignatureKey &left,
                                    MethodSignatureView right) const noexcept
      {
        return left.name == right.name &&
               left.descriptor == right.descriptor;
      }

      [[nodiscard]] bool operator()(MethodSignatureView left,
                                    const MethodSignatureKey &right) const noexcept
      {
        return (*this)(right, left);
      }
    };

    [[nodiscard]] Status rebuild_method_index();

    u16 minor_version_{0};
    u16 major_version_{0};
    u16 access_flags_{0};
    std::string name_;
    std::string super_name_;
    std::vector<std::string> interfaces_;
    std::vector<Field> fields_;
    std::vector<Method> methods_;
    std::unordered_map<MethodSignatureKey,
                       usize,
                       MethodSignatureHash,
                       MethodSignatureEqual> method_index_;
    std::vector<Constant> constants_;
    std::vector<BootstrapMethod> bootstrap_methods_;
  };

} // namespace phoneme::classfile
