#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "phoneme/media/MediaService.hpp"
#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/graphics/GraphicsStore.hpp"
#include "phoneme/network/ConnectionRegistry.hpp"
#include "phoneme/push/PushRegistry.hpp"
#include "phoneme/runtime/RecordStoreRegistry.hpp"
#include "phoneme/security/PermissionPolicy.hpp"
#include "phoneme/translation/TranslationService.hpp"
#include "phoneme/vm/CanvasBridge.hpp"
#include "phoneme/vm/ClassLayout.hpp"
#include "phoneme/vm/Interpreter.hpp"
#include "phoneme/vm/MediaEventService.hpp"
#include "phoneme/vm/MonitorTable.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"
#include "phoneme/vm/NativeRootScope.hpp"
#include "phoneme/vm/RootSet.hpp"
#include "phoneme/vm/Scheduler.hpp"
#include "phoneme/vm/TimerService.hpp"

namespace phoneme::runtime
{
  class CanvasRuntime;
}

namespace phoneme::vm
{

  struct UiBridgeEvent final
  {
    i32 kind{0};
    i32 component_id{0};
    i32 parent_id{0};
    i32 component_type{0};
    i32 index{0};
    std::array<i32, 4> arguments{};
    i64 value64{0};
    u64 generation{0};
    std::string text;
    std::string detail;
  };

  using UiEventSink = std::function<void(UiBridgeEvent)>;

  enum class MidletSignal : u8
  {
    none,
    destroyed,
    paused,
    resume_requested,
  };

  enum class InstructionBudgetMode : u8
  {
    total,
    progress_watchdog,
  };

  class Machine final
  {
  public:
    struct CharacterTranslationDecision final
    {
      enum class Action : u8
      {
        draw_original,
        draw_translation,
        suppress,
      };

      Action action{Action::draw_original};
      std::shared_ptr<const std::vector<char32_t>> translated;
      i32 x{0};
      i32 y{0};
      i32 anchor{0};
    };

    struct TextTranslationLayoutDecision final
    {
      bool planned{false};
      i32 y{0};
      i32 height{0};
    };

    // Scheduler-owned Thread.run invocations are long-lived. They still yield
    // every VM quantum and observe cancellation, but must not inherit the
    // finite callback/lifecycle budget used to isolate buggy application code.
    static constexpr u64 kLongLivedThreadInstructionBudget =
        std::numeric_limits<u64>::max();

    explicit Machine(ClassRepository &classes,
                     usize maximum_heap_objects = 1'000'000);
    ~Machine();
    void shutdown() noexcept;

    Machine(const Machine &) = delete;
    Machine &operator=(const Machine &) = delete;

    [[nodiscard]] Result<ExecutionResult> invoke_static(
        std::string_view class_name,
        std::string_view method_name,
        std::string_view descriptor,
        std::span<const Value> arguments = {},
        u64 instruction_budget = 10'000'000);

    [[nodiscard]] Result<ExecutionResult> invoke_instance(
        ObjectRef receiver,
        std::string_view declared_class,
        std::string_view method_name,
        std::string_view descriptor,
        std::span<const Value> arguments = {},
        u64 instruction_budget = 10'000'000,
        InstructionBudgetMode budget_mode = InstructionBudgetMode::total);

    [[nodiscard]] Heap &heap() noexcept { return heap_; }
    [[nodiscard]] const Heap &heap() const noexcept { return heap_; }
    [[nodiscard]] ClassStateRegistry &class_states() noexcept { return states_; }
    [[nodiscard]] ClassRepository &classes() noexcept { return classes_; }
    [[nodiscard]] NativeMethodRegistry &natives() noexcept { return natives_; }
    [[nodiscard]] MonitorTable &monitors() noexcept { return monitors_; }
    [[nodiscard]] Scheduler& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] const Scheduler& scheduler() const noexcept {
      return scheduler_;
    }
    [[nodiscard]] TimerService& timers() noexcept { return timers_; }
    [[nodiscard]] const TimerService& timers() const noexcept { return timers_; }
    [[nodiscard]] Result<ObjectRef> current_java_thread();
    [[nodiscard]] Status initialize_java_thread(ObjectRef thread,
                                                ObjectRef target);
    [[nodiscard]] Status start_java_thread(ObjectRef thread);
    [[nodiscard]] Result<std::optional<Value>> run_java_thread_target(
        ObjectRef thread);
    [[nodiscard]] Result<SchedulerWaitResult> sleep_current_thread(i64 millis);
    [[nodiscard]] Result<SchedulerWaitResult> join_java_thread(
        ObjectRef thread,
        std::optional<i64> millis);
    [[nodiscard]] Status interrupt_java_thread(ObjectRef thread);
    [[nodiscard]] Result<MonitorWaitResult> wait_on_object(
        ObjectRef object,
        std::optional<i64> millis);
    [[nodiscard]] Status notify_object(ObjectRef object, bool all);
    void cooperative_yield();
    [[nodiscard]] bool executing_on_current_thread() const noexcept;
    void request_garbage_collection() noexcept;
    [[nodiscard]] graphics::GraphicsStore& graphics() noexcept {
      return graphics_;
    }
    [[nodiscard]] const graphics::GraphicsStore& graphics() const noexcept {
      return graphics_;
    }
    void configure_translation_service(
        std::shared_ptr<translation::TranslationService> service) noexcept {
      std::scoped_lock lock(translation_service_mutex_);
      translation_service_ = std::move(service);
    }
    [[nodiscard]] std::shared_ptr<translation::TranslationService>
    translation_service() const noexcept {
      std::scoped_lock lock(translation_service_mutex_);
      return translation_service_;
    }
    void begin_character_translation_frame() noexcept;
    void end_character_translation_frame();
    void break_character_translation_run() noexcept;
    [[nodiscard]] CharacterTranslationDecision translate_draw_character(
        u64 graphics_id,
        char32_t character,
        i32 x,
        i32 y,
        i32 anchor,
        i32 font_face,
        i32 font_style,
        i32 font_size);
    [[nodiscard]] TextTranslationLayoutDecision plan_translated_text(
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
        i32 translate_y);
    [[nodiscard]] double next_random_double() noexcept;
    void configure_ui_bridge(i32 app_namespace, UiEventSink sink);
    [[nodiscard]] i32 allocate_ui_component_id() noexcept;
    void emit_ui_event(UiBridgeEvent event);
    [[nodiscard]] Status enqueue_serial_callback(ObjectRef runnable);
    [[nodiscard]] Status pump_serial_callbacks(usize maximum_callbacks = 8U);
    [[nodiscard]] usize pending_serial_callbacks() const noexcept;
    void set_serial_callback_coalescing(bool enabled) noexcept;
    [[nodiscard]] Status schedule_lcdui_alert_timeout(ObjectRef alert,
                                                       i32 timeout_millis);
    void cancel_lcdui_alert_timeout(ObjectRef alert = {}) noexcept;
    [[nodiscard]] Result<std::optional<ObjectRef>> take_due_lcdui_alert();
    [[nodiscard]] Status register_ui_component(i32 component_id,
                                               ObjectRef object);
    void unregister_ui_component(i32 component_id) noexcept;
    [[nodiscard]] Result<ObjectRef> ui_component(i32 component_id) const;
    void configure_canvas_bridge(CanvasBridge* bridge) noexcept {
      canvas_bridge_ = bridge;
    }
    [[nodiscard]] CanvasBridge* canvas_bridge() noexcept {
      return canvas_bridge_;
    }
    [[nodiscard]] const CanvasBridge* canvas_bridge() const noexcept {
      return canvas_bridge_;
    }
    [[nodiscard]] Status collect_garbage();
    [[nodiscard]] RootSet& native_roots() noexcept { return native_roots_; }
    [[nodiscard]] const RootSet& native_roots() const noexcept {
      return native_roots_;
    }
    [[nodiscard]] Result<NativeRootScope> pin_native_root(
        ObjectRef reference = {}) {
      return NativeRootScope::pin(native_roots_, reference);
    }
    [[nodiscard]] Result<NativeRootScope> allocate_pinned_instance(
        std::string_view class_name);
    void append_console(std::u16string_view text);
    [[nodiscard]] const std::u16string& console_output() const noexcept {
      return console_output_;
    }
    void signal_midlet(MidletSignal signal) noexcept;
    [[nodiscard]] MidletSignal consume_midlet_signal() noexcept;
    [[nodiscard]] bool consume_midlet_destroyed_signal() noexcept;
    [[nodiscard]] Result<ObjectRef> class_mirror(
        std::string_view class_name);
    [[nodiscard]] Result<std::string> mirrored_class_name(
        ObjectRef mirror) const;
    [[nodiscard]] Result<bool> object_is_instance(
        ObjectRef object,
        std::string_view target_class);
    [[nodiscard]] Status configure_record_store_root(std::string root);
    [[nodiscard]] runtime::RecordStoreRegistry& record_stores() noexcept {
      return record_stores_;
    }
    [[nodiscard]] Status configure_push_registry(std::string root,
                                                 SuiteId suite_id);
    [[nodiscard]] push::PushRegistry& push_registry() noexcept {
      return push_registry_;
    }
    void set_permission_policy(security::SharedPermissionPolicy policy);
    [[nodiscard]] security::PermissionPolicy& permission_policy() noexcept {
      return *permission_policy_;
    }
    [[nodiscard]] const security::PermissionPolicy& permission_policy() const noexcept {
      return *permission_policy_;
    }
    [[nodiscard]] Status configure_filesystem(std::string sandbox_root,
                                              std::string temporary_root);
    [[nodiscard]] filesystem::FileSystem& filesystem() noexcept {
      return filesystem_;
    }
    [[nodiscard]] const filesystem::FileSystem& filesystem() const noexcept {
      return filesystem_;
    }
    [[nodiscard]] media::MediaService& media() noexcept { return media_; }
    [[nodiscard]] const media::MediaService& media() const noexcept {
      return media_;
    }
    [[nodiscard]] MediaEventService& media_events() noexcept {
      return media_events_;
    }
    [[nodiscard]] const MediaEventService& media_events() const noexcept {
      return media_events_;
    }
    [[nodiscard]] network::ConnectionRegistry& connections() noexcept {
      return connections_;
    }
    [[nodiscard]] const network::ConnectionRegistry& connections() const noexcept {
      return connections_;
    }
    [[nodiscard]] Status configure_network_owner(i32 owner) noexcept;
    [[nodiscard]] Status configure_network_adapter(
        std::shared_ptr<network::AsyncNetworkAdapter> adapter);
    void close_connections() noexcept;
    void set_app_property(std::u16string key, std::u16string value);
    [[nodiscard]] Result<std::optional<std::u16string>> app_property(
        ObjectRef key) const;
    void set_system_property(std::u16string key, std::u16string value);
    [[nodiscard]] std::optional<std::u16string> configured_system_property(
        std::u16string_view key) const;

  private:
    friend class Scheduler;
    friend class runtime::CanvasRuntime;

    struct Invocation final
    {
      ResolvedMethod method;
      std::shared_ptr<const CachedMethodDescriptor> descriptor;
      NativeMethodId native_method;
      std::vector<Value> arguments;
      bool has_receiver{false};
      std::optional<Value> return_override;
    };

    struct CharacterDrawSample final
    {
      u64 graphics_id{0};
      char32_t character{0};
      i32 x{0};
      i32 y{0};
      i32 anchor{0};
      i32 font_face{0};
      i32 font_style{0};
      i32 font_size{0};
      u64 run_generation{0};
    };

    struct CharacterRunPlan final
    {
      usize first_sample{0};
      usize sample_count{0};
      i32 x{0};
      i32 y{0};
      i32 anchor{0};
      std::vector<char32_t> source;
    };

    struct PlannedCharacterSample final
    {
      CharacterDrawSample sample;
      usize run_index{std::numeric_limits<usize>::max()};
      bool first_in_run{false};
    };

    struct TranslatedTextDrawSample final
    {
      u64 graphics_id{0};
      std::vector<char32_t> text;
      i32 x{0};
      i32 y{0};
      i32 anchor{0};
      i32 font_face{0};
      i32 font_style{0};
      i32 font_size{0};
      i32 clip_x{0};
      i32 clip_y{0};
      i32 clip_width{0};
      i32 clip_height{0};
      i32 translate_x{0};
      i32 translate_y{0};
    };

    struct PlannedTranslatedTextSample final
    {
      TranslatedTextDrawSample sample;
      i32 y{0};
      i32 height{0};
    };

    enum class OperandResolutionState : u8
    {
      unresolved,
      resolving,
      resolved,
      failed,
    };

    enum class OperandResolutionKind : u8
    {
      none,
      field,
      direct_call,
    };

    struct OperandResolutionEntry final
    {
      OperandResolutionState state{OperandResolutionState::unresolved};
      OperandResolutionKind kind{OperandResolutionKind::none};
      std::shared_ptr<const FieldLocation> field;
      MethodId target_method;
      std::optional<Error> failure;
    };

    struct DirectCallCache final
    {
      MethodId target_method;
      bool valid{false};
    };

    struct VirtualCallCache final
    {
      ClassId receiver_class;
      MethodId target_method;
      bool valid{false};
    };

    struct LambdaBinding final
    {
      std::string interface_name;
      std::string sam_name;
      std::string sam_descriptor;
      std::string instantiated_descriptor;
      u8 implementation_kind{0};
      classfile::MemberReference implementation;
      std::vector<std::string> marker_interfaces;
      std::vector<std::string> bridge_descriptors;
      usize captured_count{0};
    };

    [[nodiscard]] Status initialize_system_streams();
    [[nodiscard]] Result<ExecutionResult> execute(
        Invocation invocation,
        u64 instruction_budget,
        InstructionBudgetMode budget_mode = InstructionBudgetMode::total);
    [[nodiscard]] Result<Invocation> prepare_invocation(
        ResolvedMethod method,
        std::span<const Value> arguments,
        bool has_receiver);
    void refresh_metadata_bindings_if_needed() noexcept;
    [[nodiscard]] Result<OperandResolutionEntry*> operand_resolution_entry(
        MethodId method_id,
        u32 operand_index);
    [[nodiscard]] Result<std::optional<ObjectRef>> ensure_initialized(
        std::string_view class_name,
        u64 instruction_budget);
    [[nodiscard]] Result<std::optional<ObjectRef>>
    ensure_default_interfaces_initialized(
        const classfile::ClassFile &owner,
        u64 instruction_budget);
    [[nodiscard]] Result<std::optional<Value>> invoke_native(
        const Invocation &invocation);
    [[nodiscard]] Result<ObjectRef> intern_string(
        std::string_view modified_utf8);
    [[nodiscard]] Result<ObjectRef> create_throwable(
        std::string_view class_name);
    [[nodiscard]] Result<ObjectRef> create_throwable(
        std::string_view class_name,
        std::string_view message);
    [[nodiscard]] Result<Value> load_constant(
        const classfile::ClassFile &owner,
        u16 index,
        bool category_two_only);
    [[nodiscard]] Result<std::optional<ObjectRef>> acquire_synchronized_monitor(
        const Invocation &invocation);
    [[nodiscard]] Status release_synchronized_monitor(
        std::optional<ObjectRef> monitor);
    [[nodiscard]] Status enter_monitor(ObjectRef monitor);
    [[nodiscard]] Status enter_external_execution();
    [[nodiscard]] bool try_enter_external_execution() noexcept;
    void leave_external_execution() noexcept;
    [[nodiscard]] u32 suspend_execution_for_blocking() noexcept;
    void resume_execution_after_blocking(u32 depth) noexcept;
    void publish_execution_roots(u32 invocation_depth,
                                 const std::vector<ObjectRef>& roots);
    void clear_execution_roots(u32 invocation_depth) noexcept;
    [[nodiscard]] Result<LambdaBinding> resolve_lambda_binding(
        const classfile::ClassFile &owner,
        u16 invoke_dynamic_index);
    [[nodiscard]] Result<Invocation> prepare_lambda_invocation(
        ObjectRef receiver,
        const LambdaBinding &binding,
        std::span<const Value> invocation_arguments,
        std::optional<ObjectRef> constructor_receiver = std::nullopt);
    void prune_lambda_bindings();

    ClassRepository &classes_;
    ClassStateRegistry states_;
    Heap heap_;
    NativeMethodRegistry natives_;
    MonitorTable monitors_;
    mutable std::recursive_mutex execution_mutex_;
    graphics::GraphicsStore graphics_;
    mutable std::mutex translation_service_mutex_;
    std::shared_ptr<translation::TranslationService> translation_service_;
    std::vector<CharacterDrawSample> character_translation_capture_;
    std::vector<PlannedCharacterSample> character_translation_plan_samples_;
    std::vector<CharacterRunPlan> character_translation_runs_;
    usize character_translation_replay_index_{0};
    u64 character_translation_run_generation_{0};
    bool character_translation_frame_active_{false};
    bool character_translation_replay_valid_{true};
    std::vector<TranslatedTextDrawSample> translated_text_capture_;
    std::vector<PlannedTranslatedTextSample> translated_text_plan_;
    usize translated_text_replay_index_{0};
    bool translated_text_replay_valid_{true};
    runtime::RecordStoreRegistry record_stores_;
    security::SharedPermissionPolicy permission_policy_;
    push::PushRegistry push_registry_;
    filesystem::FileSystem filesystem_;
    media::MediaService media_;
    network::ConnectionRegistry connections_;
    std::unordered_map<std::u16string, ObjectRef> interned_strings_;
    std::unordered_map<std::string, ObjectRef> class_mirrors_;
    std::unordered_map<i32, ObjectRef> ui_components_;
    std::unordered_map<u64, LambdaBinding> lambda_bindings_;
    u64 metadata_binding_generation_ {0U};
    u64 native_binding_generation_ {0U};
    std::unordered_map<const classfile::Method*, NativeMethodId> native_bindings_;
    std::unordered_map<
        MethodId,
        std::vector<OperandResolutionEntry>,
        MetadataIdHash<MethodId>> operand_resolution_tables_;
    std::unordered_map<
        const classfile::ClassFile*,
        std::unordered_map<u32, std::shared_ptr<const FieldLocation>>>
        field_bindings_;
    std::unordered_map<
        const classfile::ClassFile*,
        std::unordered_map<u32, DirectCallCache>>
        direct_call_bindings_;
    std::unordered_map<
        const classfile::ClassFile*,
        std::unordered_map<u32, VirtualCallCache>>
        virtual_call_bindings_;
    RootSet native_roots_;
    mutable std::mutex serial_callbacks_mutex_;
    std::deque<NativeRootScope> serial_callbacks_;
    bool serial_callback_coalescing_ {false};
    mutable std::mutex lcd_ui_alert_timeout_mutex_;
    std::optional<NativeRootScope> lcd_ui_alert_timeout_root_;
    std::chrono::steady_clock::time_point lcd_ui_alert_timeout_deadline_ {};
    ObjectRef emergency_out_of_memory_error_ {};
    std::unordered_set<std::string> initialized_classes_;
    std::unordered_set<std::string> initializing_classes_;
    std::unordered_set<std::string> erroneous_classes_;
    std::unordered_map<std::u16string, std::u16string> app_properties_;
    std::unordered_map<std::u16string, std::u16string> system_properties_;
    u64 random_state_{0x9E3779B97F4A7C15ULL};
    std::atomic<MidletSignal> midlet_signal_{MidletSignal::none};
    UiEventSink ui_event_sink_;
    CanvasBridge* canvas_bridge_{nullptr};
    i32 next_ui_component_id_{1};
    u64 ui_generation_{0};
    bool system_streams_initialized_{false};
    std::u16string console_output_;
    std::atomic_bool gc_requested_{false};
    std::atomic_bool shutdown_started_{false};
    TimerService timers_;
    Scheduler scheduler_;
    MediaEventService media_events_;
  };

} // namespace phoneme::vm
