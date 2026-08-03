#include "phoneme/runtime/Runtime.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/graphics/Color.hpp"
#include "phoneme/graphics/Graphics.hpp"
#include "phoneme/graphics/Image.hpp"
#include "phoneme/runtime/CanvasRuntime.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/LcduiBridge.hpp"
#include "phoneme/vm/Machine.hpp"

#if defined(__APPLE__)
#include <os/log.h>
#endif

namespace phoneme::runtime {

class ApplicationVM final {
public:
    ApplicationVM(Dimensions dimensions,
                  std::array<i32, 7> keymap,
                  Framebuffer& framebuffer);
    ~ApplicationVM();

    std::recursive_mutex operation_mutex;
    vm::ClassRepository classes;
    vm::Machine machine;
    CanvasRuntime canvas;
    vm::NativeRootScope paint_graphics;
    vm::ObjectRef midlet;
};

namespace {

constexpr usize kImageWidthField = 0;
constexpr usize kImageHeightField = 1;
constexpr usize kImageMutableField = 2;
constexpr usize kGraphicsTargetField = 0;
// Obfuscated Gameloft constructors can synchronously range-decode most of the
// asset pack before startApp. This remains finite launch work, but routinely
// exceeds the generic callback guard used for paint/input handlers.
constexpr u64 kMidletConstructorInstructionBudget = 200'000'000U;
constexpr u64 kMidletLifecycleInstructionBudget = 100'000'000U;

[[nodiscard]] bool is_glu_vendor(std::u16string_view vendor) noexcept {
    constexpr std::u16string_view kPrefix = u"glu";
    if (vendor.size() < kPrefix.size()) return false;
    for (usize index = 0; index < kPrefix.size(); ++index) {
        char16_t character = vendor[index];
        if (character >= u'A' && character <= u'Z') {
            character = static_cast<char16_t>(character - u'A' + u'a');
        }
        if (character != kPrefix[index]) return false;
    }
    return true;
}

void apply_legacy_property_defaults(vm::Machine& machine,
                                    const Suite& suite) {
    const auto vendor = suite.properties.find(u"MIDlet-Vendor");
    if (vendor == suite.properties.end() || !is_glu_vendor(vendor->second)) {
        return;
    }
    if (!suite.properties.contains(u"ClientLogoEnable")) {
        // Several GLU/Metaflow builds call equals() directly on this optional
        // property. Reference devices shipped these builds with the carrier
        // customization defaulted off even when the final JAR manifest omitted
        // the key.
        machine.set_app_property(u"ClientLogoEnable", u"false");
    }
}

[[nodiscard]] Status copy_framebuffer_to_image(
    Framebuffer& framebuffer,
    graphics::Image& image) {
    const FrameSnapshot current = framebuffer.snapshot();
    if (current.dimensions.width != image.width() ||
        current.dimensions.height != image.height() ||
        current.rgba.size() != image.pixels().size() * 4U) {
        return {};
    }

    auto pixels = image.mutable_pixels();
    for (usize index = 0; index < pixels.size(); ++index) {
        const usize offset = index * 4U;
        pixels[index] = graphics::argb(current.rgba[offset + 3U],
                                       current.rgba[offset],
                                       current.rgba[offset + 1U],
                                       current.rgba[offset + 2U]);
    }
    return {};
}

[[nodiscard]] Status reset_canvas_graphics(
    vm::Machine& machine,
    Framebuffer& framebuffer,
    vm::ObjectRef graphics_object,
    Dimensions dimensions,
    vm::CanvasRect clip,
    bool display_target) {
    auto target_value = machine.heap().field(graphics_object,
                                             kGraphicsTargetField);
    if (!target_value) return std::unexpected(target_value.error());
    auto image_object = target_value->as_reference();
    if (!image_object) return std::unexpected(image_object.error());

    auto target = machine.graphics().image(image_object->bits);
    if (!target) return std::unexpected(target.error());
    if ((*target)->width() != dimensions.width ||
        (*target)->height() != dimensions.height) {
        return fail(ErrorCode::invalid_state,
                    "cached Canvas graphics dimensions changed");
    }
    auto copied = copy_framebuffer_to_image(framebuffer, **target);
    if (!copied) return copied;

    auto context = machine.graphics().context(graphics_object.bits);
    if (!context) return std::unexpected(context.error());
    **context = graphics::GraphicsContext {
        .target_key = image_object->bits,
        .display_target = display_target,
    };
    return graphics::set_clip(**context,
                              **target,
                              clip.x,
                              clip.y,
                              clip.width,
                              clip.height);
}

[[nodiscard]] Result<vm::NativeRootScope> prepare_canvas_graphics(
    vm::Machine& machine,
    Framebuffer& framebuffer,
    Dimensions dimensions,
    vm::CanvasRect clip,
    bool display_target,
    vm::NativeRootScope* persistent_cache = nullptr) {
    if (persistent_cache != nullptr && persistent_cache->active()) {
        auto cached = persistent_cache->get();
        if (cached) {
            auto reset = reset_canvas_graphics(machine,
                                               framebuffer,
                                               *cached,
                                               dimensions,
                                               clip,
                                               display_target);
            if (reset) {
                return machine.pin_native_root(*cached);
            }
        }
        (void)persistent_cache->release();
    }

    auto image = graphics::Image::create_mutable(dimensions.width,
                                                  dimensions.height);
    if (!image) return std::unexpected(image.error());
    auto copied = copy_framebuffer_to_image(framebuffer, *image);
    if (!copied) return std::unexpected(copied.error());

    auto image_root = machine.allocate_pinned_instance(
        "javax/microedition/lcdui/Image");
    if (!image_root) return std::unexpected(image_root.error());
    auto image_object = image_root->get();
    if (!image_object) return std::unexpected(image_object.error());
    auto width_stored = machine.heap().set_field(
        *image_object, kImageWidthField, vm::Value::from_int(dimensions.width));
    auto height_stored = machine.heap().set_field(
        *image_object, kImageHeightField, vm::Value::from_int(dimensions.height));
    auto mutable_stored = machine.heap().set_field(
        *image_object, kImageMutableField, vm::Value::from_int(1));
    if (!width_stored) return std::unexpected(width_stored.error());
    if (!height_stored) return std::unexpected(height_stored.error());
    if (!mutable_stored) return std::unexpected(mutable_stored.error());
    auto image_attached = machine.graphics().attach_image(
        image_object->bits, std::move(*image));
    if (!image_attached) return std::unexpected(image_attached.error());

    auto graphics_root = machine.allocate_pinned_instance(
        "javax/microedition/lcdui/Graphics");
    if (!graphics_root) return std::unexpected(graphics_root.error());
    auto graphics_object = graphics_root->get();
    if (!graphics_object) return std::unexpected(graphics_object.error());
    auto target_stored = machine.heap().set_field(
        *graphics_object,
        kGraphicsTargetField,
        vm::Value::from_reference(*image_object));
    if (!target_stored) return std::unexpected(target_stored.error());
    auto context_attached = machine.graphics().attach_context(
        graphics_object->bits, image_object->bits, display_target);
    if (!context_attached) return std::unexpected(context_attached.error());

    auto reset = reset_canvas_graphics(machine,
                                       framebuffer,
                                       *graphics_object,
                                       dimensions,
                                       clip,
                                       display_target);
    if (!reset) return std::unexpected(reset.error());

    if (persistent_cache != nullptr) {
        auto pinned = machine.pin_native_root(*graphics_object);
        if (!pinned) return std::unexpected(pinned.error());
        *persistent_cache = std::move(*pinned);
    }
    return std::move(*graphics_root);
}

[[nodiscard]] Status publish_canvas_graphics(vm::Machine& machine,
                                             Framebuffer& framebuffer,
                                             vm::ObjectRef graphics_object) {
    if (graphics_object.is_null()) return {};
    auto target_value = machine.heap().field(graphics_object,
                                             kGraphicsTargetField);
    if (!target_value) return std::unexpected(target_value.error());
    auto image_object = target_value->as_reference();
    if (!image_object) return std::unexpected(image_object.error());
    auto image = machine.graphics().image(image_object->bits);
    if (!image) return std::unexpected(image.error());

    std::vector<u8> rgba((*image)->pixels().size() * 4U);
    usize offset = 0;
    for (graphics::Pixel pixel : (*image)->pixels()) {
        rgba[offset++] = graphics::red(pixel);
        rgba[offset++] = graphics::green(pixel);
        rgba[offset++] = graphics::blue(pixel);
        rgba[offset++] = graphics::alpha(pixel);
    }
    return framebuffer.replace(
        Dimensions {(*image)->width(), (*image)->height()}, rgba);
}

[[nodiscard]] bool is_directory(const std::string& path) noexcept {
    struct stat status {};
    return !path.empty() && ::stat(path.c_str(), &status) == 0 &&
           S_ISDIR(status.st_mode);
}

[[nodiscard]] bool is_regular_file(const std::string& path) noexcept {
    struct stat status {};
    return !path.empty() && ::stat(path.c_str(), &status) == 0 &&
           S_ISREG(status.st_mode);
}

void append_utf8(std::string& output, std::u16string_view text) {
    for (usize index = 0; index < text.size(); ++index) {
        u32 code_point = static_cast<u16>(text[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < text.size()) {
            const u32 low = static_cast<u16>(text[index + 1U]);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U +
                    ((code_point - 0xD800U) << 10U) +
                    (low - 0xDC00U);
                ++index;
            }
        }
        if (code_point <= 0x7FU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(
                0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(
                0x80U | (code_point & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(
                0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(
                0x80U | (code_point & 0x3FU)));
        }
    }
}

[[nodiscard]] Result<std::string> describe_throwable(
    vm::Machine& machine,
    vm::ObjectRef throwable,
    usize depth = 0U) {
    auto class_name = machine.heap().class_name(throwable);
    if (!class_name) return std::unexpected(class_name.error());
    std::string description = *class_name;

    auto message_value = machine.heap().field(throwable, 0U);
    if (message_value) {
        auto message = message_value->as_reference();
        if (message && !message->is_null()) {
            auto text = machine.heap().string_value(*message);
            if (text && !text->empty()) {
                description += ": ";
                append_utf8(description, *text);
            }
        }
    }

    if (depth < 3U) {
        auto cause_value = machine.heap().field(throwable, 1U);
        if (cause_value) {
            auto cause = cause_value->as_reference();
            if (cause && !cause->is_null() && *cause != throwable) {
                auto nested = describe_throwable(machine, *cause, depth + 1U);
                if (nested) description += " caused by " + *nested;
            }
        }
    }
    return description;
}

[[nodiscard]] Status require_normal_completion(
    vm::Machine& machine,
    const vm::ExecutionResult& result,
    std::string_view lifecycle_phase) {
    if (result.completed_normally()) {
        return {};
    }
    if (!result.throwable.has_value()) {
        return fail(ErrorCode::internal_error,
                    std::string(lifecycle_phase) +
                        " ended abnormally without a throwable");
    }
    auto throwable = describe_throwable(machine, *result.throwable);
    if (!throwable) {
        return std::unexpected(throwable.error());
    }
    std::string message = std::string(lifecycle_phase) +
        " threw an uncaught Java exception: " + *throwable;
    if (!result.exception_context.empty()) {
        message += " from " + result.exception_context;
    }
    return fail(ErrorCode::java_exception, std::move(message));
}

} // namespace

ApplicationVM::ApplicationVM(Dimensions dimensions,
                             std::array<i32, 7> keymap,
                             Framebuffer& framebuffer)
    : machine(classes), canvas(machine, dimensions, keymap) {
    machine.configure_canvas_bridge(&canvas);
    CanvasRenderHooks hooks;
    hooks.acquire_paint_graphics = [this, &framebuffer](
        vm::Machine& target_machine,
        vm::ObjectRef,
        Dimensions target_dimensions,
        vm::CanvasRect repaint_region) {
        return prepare_canvas_graphics(target_machine,
                                       framebuffer,
                                       target_dimensions,
                                       repaint_region,
                                       true,
                                       &paint_graphics);
    };
    hooks.commit_paint = [&framebuffer](
        vm::Machine& target_machine,
        vm::ObjectRef,
        vm::ObjectRef graphics,
        vm::CanvasRect) {
        return publish_canvas_graphics(target_machine,
                                       framebuffer,
                                       graphics);
    };
    hooks.acquire_game_graphics = [&framebuffer](
        vm::Machine& target_machine,
        vm::ObjectRef,
        Dimensions target_dimensions) {
        return prepare_canvas_graphics(
            target_machine,
            framebuffer,
            target_dimensions,
            vm::CanvasRect {0, 0,
                            target_dimensions.width,
                            target_dimensions.height},
            false);
    };
    hooks.flush_game_graphics = [&framebuffer](
        vm::Machine& target_machine,
        vm::ObjectRef,
        vm::ObjectRef graphics,
        vm::CanvasRect) {
        return publish_canvas_graphics(target_machine,
                                       framebuffer,
                                       graphics);
    };
    canvas.configure_render_hooks(std::move(hooks));
}

ApplicationVM::~ApplicationVM() {
    machine.shutdown();
    machine.configure_canvas_bridge(nullptr);
}

Runtime::Runtime() : input_queue_(1'024), ui_queue_(1'024) {}

Runtime::~Runtime() { stop(); }

Status Runtime::configure(std::string runtime_home,
                          std::string optional_class_archive) {
    if (!is_directory(runtime_home)) {
        return fail(ErrorCode::invalid_argument,
                    "runtime home is not an accessible directory");
    }
    if (!optional_class_archive.empty() &&
        !is_regular_file(optional_class_archive)) {
        return fail(ErrorCode::invalid_argument,
                    "optional class archive is not an accessible file");
    }

    std::scoped_lock lock(mutex_);
    if (running_) {
        return fail(ErrorCode::already_running,
                    "runtime cannot be reconfigured while running");
    }
    runtime_home_ = std::move(runtime_home);
    optional_class_archive_ = std::move(optional_class_archive);
    permission_policies_.clear();
    configured_ = true;
    last_exit_code_ = 0;
    return {};
}

Status Runtime::configure_keymap(std::array<i32, 7> keymap) {
    std::vector<std::shared_ptr<ApplicationVM>> application_vms;
    {
        std::scoped_lock lock(mutex_);
        keymap_ = keymap;
        application_vms.reserve(apps_.size());
        for (const auto& [id, app] : apps_) {
            (void)id;
            if (app.vm != nullptr) application_vms.push_back(app.vm);
        }
    }
    for (const auto& vm : application_vms) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        vm->canvas.set_keymap(keymap);
    }
    return {};
}

Status Runtime::configure_input_capabilities(bool pointer_events,
                                             bool pointer_motion,
                                             bool repeat_events) {
    std::vector<std::shared_ptr<ApplicationVM>> application_vms;
    {
        std::scoped_lock lock(mutex_);
        pointer_events_supported_ = pointer_events;
        pointer_motion_supported_ = pointer_events && pointer_motion;
        repeat_events_supported_ = repeat_events;
        application_vms.reserve(apps_.size());
        for (const auto& [id, app] : apps_) {
            (void)id;
            if (app.vm != nullptr) application_vms.push_back(app.vm);
        }
    }
    for (const auto& vm : application_vms) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        vm->canvas.set_input_capabilities(pointer_events,
                                          pointer_events && pointer_motion,
                                          repeat_events);
    }
    return {};
}

Status Runtime::configure_permission_prompt(
    security::PermissionPromptCallback prompt) {
    std::scoped_lock lock(mutex_);
    if (running_) {
        return fail(ErrorCode::already_running,
                    "permission prompt cannot change while running");
    }
    permission_prompt_ = std::move(prompt);
    permission_policies_.clear();
    return {};
}

Status Runtime::set_suite_trust(SuiteId suite_id,
                                security::SuiteTrust trust) {
    if (!suite_id.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "suite trust requires a valid suite ID");
    }
    std::scoped_lock lock(mutex_);
    if (suite_store_.find(suite_id) == nullptr) {
        return fail(ErrorCode::invalid_argument,
                    "suite trust references an unknown suite");
    }
    for (const auto& [app_id, app] : apps_) {
        (void)app_id;
        if (app.suite_id == suite_id && app.vm != nullptr &&
            app.state != AppState::destroyed) {
            return fail(ErrorCode::invalid_state,
                        "suite trust cannot change while its MIDlet is alive");
        }
    }
    suite_trust_.insert_or_assign(suite_id.value, trust);
    permission_policies_.erase(suite_id.value);
    return {};
}

Result<SuiteId> Runtime::install_jar(const std::string& jar_path) {
    if (!is_regular_file(jar_path)) {
        return fail(ErrorCode::invalid_argument,
                    "JAR path is not an accessible regular file");
    }

    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::not_configured,
                    "runtime must be configured before installing a suite");
    }
    // Suite installation mutates only the persistent SuiteStore. Each live
    // MIDlet owns an immutable copy of its Suite and its own class repository,
    // so adding another JAR is safe while unrelated applications are running.
    return suite_store_.install(jar_path);
}

Status Runtime::start_system() {
    std::scoped_lock lock(mutex_);
    if (!configured_) {
        return fail(ErrorCode::not_configured,
                    "runtime must be configured before startup");
    }
    if (running_) {
        return {};
    }
    running_ = true;
    suspended_ = false;
    last_exit_code_ = 0;
    return {};
}

Status Runtime::start_midlet(SuiteId suite_id,
                             std::string main_class,
                             AppId app_id,
                             Dimensions dimensions) {
    if (!suite_id.valid() || !app_id.valid() || main_class.empty() ||
        !dimensions.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "invalid MIDlet launch arguments");
    }

    Suite suite;
    std::string runtime_home;
    std::string optional_class_archive;
    std::array<i32, 7> keymap {};
    bool pointer_events_supported = true;
    bool pointer_motion_supported = true;
    bool repeat_events_supported = true;
    security::SharedPermissionPolicy permission_policy;
    security::SuiteTrust suite_trust = security::SuiteTrust::untrusted;
    security::PermissionPromptCallback permission_prompt;
    u64 lifecycle_token = 0;
    {
        std::unique_lock lock(mutex_);
        auto running = require_running_unlocked();
        if (!running) return running;
        if (apps_.contains(app_id.value)) {
            return fail(ErrorCode::invalid_state,
                        "application ID is already in use");
        }
        const Suite* stored_suite = suite_store_.find(suite_id);
        if (stored_suite == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "suite ID does not exist");
        }
        suite = *stored_suite;
        runtime_home = runtime_home_;
        optional_class_archive = optional_class_archive_;
        keymap = keymap_;
        pointer_events_supported = pointer_events_supported_;
        pointer_motion_supported = pointer_motion_supported_;
        repeat_events_supported = repeat_events_supported_;
        permission_prompt = permission_prompt_;
        if (const auto existing = permission_policies_.find(suite_id.value);
            existing != permission_policies_.end()) {
            permission_policy = existing->second;
        }
        if (const auto trust = suite_trust_.find(suite_id.value);
            trust != suite_trust_.end()) {
            suite_trust = trust->second;
        }

        lifecycle_token = ++sequence_;
        apps_.insert_or_assign(app_id.value, App {
            .id = app_id,
            .suite_id = suite_id,
            .main_class = main_class,
            .dimensions = dimensions,
            .state = AppState::none,
            .generation = sequence_,
            .lifecycle_token = lifecycle_token,
            .lifecycle_busy = true,
            .vm = nullptr,
        });
    }

    std::shared_ptr<ApplicationVM> application_vm;
    const auto fail_start = [&](Error error) -> Status {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app != nullptr && app->lifecycle_token == lifecycle_token) {
            app->state = AppState::error;
            app->lifecycle_busy = false;
            if (application_vm != nullptr) app->vm = application_vm;
            app->generation = ++sequence_;
            last_exit_code_ = -1;
        }
        return std::unexpected(std::move(error));
    };

    if (permission_policy == nullptr) {
        auto candidate = std::make_shared<security::PermissionPolicy>();
        auto configured = candidate->configure(
            security::PermissionPolicyConfig {
                .suite_id = suite_id,
                .trust = suite_trust,
                .persistence_path = runtime_home + "/security/" +
                    std::to_string(suite_id.value) + ".permissions",
                .declared_permissions = suite.declared_permissions,
                .required_permissions =
                    suite.declared_required_permissions,
                .optional_permissions =
                    suite.declared_optional_permissions,
                // The iOS host marks locally imported JARs as trusted so they
                // run with the emulator's compatibility domain. A number of
                // real-world unsigned MIDlets (including Nicknso) declare only
                // an unrelated optional permission and still open sockets at
                // runtime. Treating that partial list as an allow-list blocks
                // Connector.open() before the TCP adapter is reached. phoneME's
                // unidentified/compatibility domain grants from the domain
                // policy instead of limiting access to the manifest list.
                .enforce_declared_permissions =
                    suite.has_permission_declarations &&
                    suite_trust != security::SuiteTrust::trusted,
                .trusted_default_allow = true,
                .prompt = std::move(permission_prompt),
            });
        if (!configured) return fail_start(configured.error());
        std::unique_lock lock(mutex_);
        const auto [iterator, inserted] = permission_policies_.emplace(
            suite_id.value, candidate);
        (void)inserted;
        permission_policy = iterator->second;
    }

    application_vm = std::make_shared<ApplicationVM>(
        dimensions, keymap, framebuffer_);
    application_vm->canvas.set_input_capabilities(
        pointer_events_supported,
        pointer_motion_supported,
        repeat_events_supported);
    application_vm->machine.set_permission_policy(permission_policy);
    application_vm->machine.configure_ui_bridge(
        app_id.value,
        [this](vm::UiBridgeEvent event) {
            ui_queue_.push(UiEvent {
                .kind = event.kind,
                .component_id = event.component_id,
                .parent_id = event.parent_id,
                .component_type = event.component_type,
                .index = event.index,
                .arguments = event.arguments,
                .value64 = event.value64,
                .generation = event.generation,
                .text = std::move(event.text),
                .detail = std::move(event.detail),
            });
        });
    auto configured = application_vm->machine.configure_network_owner(
        app_id.value);
    if (!configured) return fail_start(configured.error());
    configured = application_vm->machine.configure_record_store_root(
        runtime_home + "/rms/" + std::to_string(suite_id.value));
    if (!configured) return fail_start(configured.error());
    configured = application_vm->machine.configure_push_registry(
        runtime_home + "/push", suite_id);
    if (!configured) return fail_start(configured.error());
    configured = application_vm->machine.configure_filesystem(
        runtime_home + "/files/" + std::to_string(suite_id.value),
        runtime_home + "/tmp/" + std::to_string(suite_id.value) + "/" +
            std::to_string(app_id.value));
    if (!configured) return fail_start(configured.error());

    auto classpath = application_vm->classes.add_archive(suite.jar_path);
    if (!classpath) return fail_start(classpath.error());
    if (!optional_class_archive.empty()) {
        classpath = application_vm->classes.add_archive(optional_class_archive);
        if (!classpath) return fail_start(classpath.error());
    }
    for (const auto& [key, value] : suite.properties) {
        application_vm->machine.set_app_property(key, value);
    }
    apply_legacy_property_defaults(application_vm->machine, suite);
    auto is_midlet = application_vm->classes.is_assignable(
        main_class, "javax/microedition/midlet/MIDlet");
    if (!is_midlet) return fail_start(is_midlet.error());
    if (!*is_midlet) {
        return fail_start(Error::make(
            ErrorCode::invalid_argument,
            "MIDlet main class does not extend MIDlet"));
    }

    auto receiver = application_vm->machine.class_states().allocate_instance(
        application_vm->machine.heap(), main_class);
    if (!receiver) return fail_start(receiver.error());
    application_vm->midlet = *receiver;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->lifecycle_token != lifecycle_token) {
            return fail(ErrorCode::invalid_state,
                        "MIDlet launch reservation was superseded");
        }
        app->vm = application_vm;
    }

    std::scoped_lock application_operation(application_vm->operation_mutex);
    application_vm->machine.scheduler().set_host_foreground(true);
    auto launch_foregrounded = application_vm->canvas.set_host_foreground(true);
    if (!launch_foregrounded) return fail_start(launch_foregrounded.error());
    auto constructor = application_vm->machine.invoke_instance(
        *receiver, main_class, "<init>", "()V", {},
        kMidletConstructorInstructionBudget);
    if (!constructor) return fail_start(constructor.error());
    auto completion = require_normal_completion(application_vm->machine,
                                                *constructor,
                                                "MIDlet constructor");
    if (!completion) return fail_start(completion.error());

    auto started = application_vm->machine.invoke_instance(
        *receiver, main_class, "startApp", "()V", {},
        kMidletLifecycleInstructionBudget);
    if (!started) return fail_start(started.error());
    completion = require_normal_completion(application_vm->machine,
                                           *started,
                                           "MIDlet startApp");
    if (!completion) return fail_start(completion.error());

    const vm::MidletSignal launch_signal =
        application_vm->machine.consume_midlet_signal();
    std::shared_ptr<ApplicationVM> previous_vm;
    AppId previous_id;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->vm != application_vm ||
            app->lifecycle_token != lifecycle_token) {
            return fail(ErrorCode::invalid_state,
                        "MIDlet launch was superseded");
        }
        if (launch_signal != vm::MidletSignal::destroyed &&
            foreground_app_id_.valid() && foreground_app_id_ != app_id) {
            previous_id = foreground_app_id_;
            const App* previous = find_app_unlocked(previous_id);
            if (previous != nullptr) previous_vm = previous->vm;
        }
    }

    if (previous_vm != nullptr) {
        std::scoped_lock previous_operation(previous_vm->operation_mutex);
        auto hidden = previous_vm->canvas.set_host_foreground(false);
        if (!hidden) return fail_start(hidden.error());
        auto pumped = previous_vm->canvas.pump();
        if (!pumped) return fail_start(pumped.error());
        previous_vm->machine.scheduler().set_host_foreground(false);
    }
    if (launch_signal == vm::MidletSignal::paused) {
        application_vm->machine.media().suspend();
    } else if (launch_signal != vm::MidletSignal::destroyed) {
        application_vm->machine.media().resume();
    }
    if (launch_signal != vm::MidletSignal::destroyed) {
        application_vm->machine.scheduler().set_host_foreground(true);
        auto resized = framebuffer_.resize(dimensions);
        if (!resized) return fail_start(resized.error());
        auto foregrounded = application_vm->canvas.set_host_foreground(true);
        if (!foregrounded) return fail_start(foregrounded.error());
        auto pumped = application_vm->canvas.pump();
        if (!pumped) return fail_start(pumped.error());
    }

    u64 event_generation = 0;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->vm != application_vm ||
            app->lifecycle_token != lifecycle_token) {
            return fail(ErrorCode::invalid_state,
                        "MIDlet launch was superseded before commit");
        }
        app->lifecycle_busy = false;
        if (launch_signal == vm::MidletSignal::destroyed) {
            app->state = AppState::destroyed;
            app->vm.reset();
        } else if (launch_signal == vm::MidletSignal::paused) {
            app->state = AppState::paused;
            foreground_app_id_ = app_id;
        } else {
            app->state = AppState::active;
            foreground_app_id_ = app_id;
        }
        app->generation = ++sequence_;
        event_generation = sequence_;
        last_exit_code_ = 0;
    }

    ui_queue_.push(UiEvent {
        .kind = 1,
        .component_id = app_id.value,
        .generation = event_generation,
        .detail = launch_signal == vm::MidletSignal::destroyed
            ? "MIDlet notified destruction from startApp"
            : (launch_signal == vm::MidletSignal::paused
                   ? "MIDlet notified pause from startApp"
                   : "MIDlet constructor and startApp completed in the C++ VM"),
    });
    return {};
}

Status Runtime::set_foreground(AppId app_id, Dimensions dimensions) {
    // app_id == 0 is the host-level "detach visible application" operation.
    // It is intentionally not MIDlet.pauseApp(): sockets, timers and audio may
    // remain alive, but Canvas rendering is disabled and the VM scheduler moves
    // to its shared low-duty background gate.
    if (!app_id.valid()) {
        std::shared_ptr<ApplicationVM> hidden_vm;
        AppId hidden_id;
        u64 hidden_token = 0;
        {
            std::unique_lock lock(mutex_);
            auto running = require_running_unlocked();
            if (!running) return running;
            if (!foreground_app_id_.valid()) {
                framebuffer_.clear();
                return {};
            }
            hidden_id = foreground_app_id_;
            App* hidden = find_app_unlocked(hidden_id);
            if (hidden == nullptr || hidden->vm == nullptr ||
                hidden->state == AppState::destroyed) {
                foreground_app_id_ = {};
                framebuffer_.clear();
                return {};
            }
            if (hidden->lifecycle_busy) {
                return fail(ErrorCode::invalid_state,
                            "foreground application is busy");
            }
            hidden->lifecycle_busy = true;
            hidden_token = hidden->lifecycle_token = ++sequence_;
            hidden_vm = hidden->vm;
        }

        const auto fail_hide = [&](Error error) -> Status {
            std::unique_lock lock(mutex_);
            App* hidden = find_app_unlocked(hidden_id);
            if (hidden != nullptr && hidden->vm == hidden_vm &&
                hidden->lifecycle_token == hidden_token) {
                hidden->state = AppState::error;
                hidden->lifecycle_busy = false;
                hidden->generation = ++sequence_;
            }
            last_exit_code_ = -1;
            return std::unexpected(std::move(error));
        };

        {
            std::scoped_lock hidden_operation(hidden_vm->operation_mutex);
            auto hidden = hidden_vm->canvas.set_host_foreground(false);
            if (!hidden) return fail_hide(hidden.error());
            auto pumped = hidden_vm->canvas.pump();
            if (!pumped) return fail_hide(pumped.error());
            hidden_vm->machine.scheduler().set_host_foreground(false);
        }

        std::unique_lock lock(mutex_);
        App* hidden = find_app_unlocked(hidden_id);
        if (hidden == nullptr || hidden->vm != hidden_vm ||
            hidden->lifecycle_token != hidden_token) {
            return fail(ErrorCode::invalid_state,
                        "foreground detach was superseded");
        }
        hidden->lifecycle_busy = false;
        hidden->generation = ++sequence_;
        foreground_app_id_ = {};
        framebuffer_.clear();
        last_exit_code_ = 0;
        return {};
    }
    if (!dimensions.valid()) {
        return fail(ErrorCode::invalid_argument,
                    "invalid foreground application dimensions");
    }

    std::shared_ptr<ApplicationVM> target_vm;
    std::shared_ptr<ApplicationVM> previous_vm;
    AppId previous_id;
    u64 target_token = 0;
    u64 previous_token = 0;
    {
        std::unique_lock lock(mutex_);
        auto running = require_running_unlocked();
        if (!running) return running;
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->state == AppState::destroyed ||
            app->vm == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "foreground application does not exist");
        }
        if (app->lifecycle_busy) {
            return fail(ErrorCode::invalid_state,
                        "foreground application is busy");
        }
        app->lifecycle_busy = true;
        target_token = app->lifecycle_token = ++sequence_;
        target_vm = app->vm;

        if (foreground_app_id_.valid() && foreground_app_id_ != app_id) {
            previous_id = foreground_app_id_;
            App* previous = find_app_unlocked(previous_id);
            if (previous != nullptr && previous->vm != nullptr) {
                if (previous->lifecycle_busy) {
                    app->lifecycle_busy = false;
                    return fail(ErrorCode::invalid_state,
                                "previous foreground application is busy");
                }
                previous->lifecycle_busy = true;
                previous_token = previous->lifecycle_token = ++sequence_;
                previous_vm = previous->vm;
            }
        }
    }

    const auto finish_failure = [&](Error error,
                                    bool previous_failed) -> Status {
        std::unique_lock lock(mutex_);
        App* target = find_app_unlocked(app_id);
        if (target != nullptr && target->vm == target_vm &&
            target->lifecycle_token == target_token) {
            target->state = AppState::error;
            target->lifecycle_busy = false;
            target->generation = ++sequence_;
        }
        if (previous_vm != nullptr) {
            App* previous = find_app_unlocked(previous_id);
            if (previous != nullptr && previous->vm == previous_vm &&
                previous->lifecycle_token == previous_token) {
                if (previous_failed) {
                    previous->state = AppState::error;
                    previous->generation = ++sequence_;
                }
                previous->lifecycle_busy = false;
            }
        }
        last_exit_code_ = -1;
        return std::unexpected(std::move(error));
    };

    std::scoped_lock target_operation(target_vm->operation_mutex);
    std::unique_lock<std::recursive_mutex> previous_operation;
    if (previous_vm != nullptr) {
        previous_operation = std::unique_lock<std::recursive_mutex>(
            previous_vm->operation_mutex);
        auto hidden = previous_vm->canvas.set_host_foreground(false);
        if (!hidden) return finish_failure(hidden.error(), true);
        auto pumped = previous_vm->canvas.pump();
        if (!pumped) return finish_failure(pumped.error(), true);
        previous_vm->machine.scheduler().set_host_foreground(false);
    }
    target_vm->machine.scheduler().set_host_foreground(true);
    auto canvas_resized = target_vm->canvas.set_dimensions(dimensions);
    if (!canvas_resized) {
        return finish_failure(canvas_resized.error(), false);
    }
    auto resized = framebuffer_.resize(dimensions);
    if (!resized) return finish_failure(resized.error(), false);
    auto foregrounded = target_vm->canvas.set_host_foreground(true);
    if (!foregrounded) return finish_failure(foregrounded.error(), false);
    auto pumped = target_vm->canvas.pump();
    if (!pumped) return finish_failure(pumped.error(), false);

    std::unique_lock lock(mutex_);
    App* target = find_app_unlocked(app_id);
    if (target == nullptr || target->vm != target_vm ||
        target->lifecycle_token != target_token) {
        return fail(ErrorCode::invalid_state,
                    "foreground switch was superseded");
    }
    target->dimensions = dimensions;
    target->generation = ++sequence_;
    target->lifecycle_busy = false;
    if (previous_vm != nullptr) {
        App* previous = find_app_unlocked(previous_id);
        if (previous != nullptr && previous->vm == previous_vm &&
            previous->lifecycle_token == previous_token) {
            previous->lifecycle_busy = false;
        }
    }
    foreground_app_id_ = app_id;
    last_exit_code_ = 0;
    return {};
}

Status Runtime::pause_midlet(AppId app_id) {
    std::shared_ptr<ApplicationVM> vm;
    std::string main_class;
    bool was_foreground = false;
    u64 lifecycle_token = 0;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->state == AppState::destroyed ||
            app->vm == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "application does not exist");
        }
        if (app->state == AppState::paused) return {};
        if (app->lifecycle_busy) {
            return fail(ErrorCode::invalid_state,
                        "application lifecycle operation is already running");
        }
        app->lifecycle_busy = true;
        lifecycle_token = app->lifecycle_token = ++sequence_;
        vm = app->vm;
        main_class = app->main_class;
        was_foreground = foreground_app_id_ == app_id;
    }

    const auto fail_lifecycle = [&](Error error) -> Status {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app != nullptr && app->vm == vm &&
            app->lifecycle_token == lifecycle_token) {
            app->state = AppState::error;
            app->lifecycle_busy = false;
            app->generation = ++sequence_;
            last_exit_code_ = -1;
        }
        return std::unexpected(std::move(error));
    };

    std::scoped_lock vm_operation(vm->operation_mutex);
    auto paused = vm->machine.invoke_instance(vm->midlet,
                                               main_class,
                                               "pauseApp",
                                               "()V");
    if (!paused) return fail_lifecycle(paused.error());
    auto completion = require_normal_completion(vm->machine,
                                                *paused,
                                                "MIDlet pauseApp");
    if (!completion) return fail_lifecycle(completion.error());

    const vm::MidletSignal signal = vm->machine.consume_midlet_signal();
    if (was_foreground && signal != vm::MidletSignal::resume_requested) {
        auto hidden = vm->canvas.set_host_foreground(false);
        if (!hidden) return fail_lifecycle(hidden.error());
        auto pumped = vm->canvas.pump();
        if (!pumped) return fail_lifecycle(pumped.error());
        vm->machine.scheduler().set_host_foreground(false);
    }
    if (signal == vm::MidletSignal::resume_requested) {
        vm->machine.media().resume();
    } else if (signal != vm::MidletSignal::destroyed) {
        vm->machine.media().suspend();
    }

    std::unique_lock lock(mutex_);
    App* app = find_app_unlocked(app_id);
    if (app == nullptr || app->vm != vm ||
        app->lifecycle_token != lifecycle_token) {
        return fail(ErrorCode::invalid_state,
                    "application lifecycle operation was superseded");
    }
    app->lifecycle_busy = false;
    if (signal == vm::MidletSignal::destroyed) {
        app->state = AppState::destroyed;
        app->vm.reset();
        if (foreground_app_id_ == app_id) {
            foreground_app_id_ = {};
            framebuffer_.clear();
        }
    } else if (signal == vm::MidletSignal::resume_requested) {
        app->state = AppState::active;
    } else {
        app->state = AppState::paused;
    }
    app->generation = ++sequence_;
    return {};
}

Status Runtime::resume_midlet(AppId app_id) {
    std::shared_ptr<ApplicationVM> vm;
    std::string main_class;
    bool was_foreground = false;
    u64 lifecycle_token = 0;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr || app->state == AppState::destroyed ||
            app->vm == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "application does not exist");
        }
        if (app->state == AppState::error) {
            return fail(ErrorCode::invalid_state,
                        "application cannot resume from the error state");
        }
        if (app->state == AppState::active) return {};
        if (app->lifecycle_busy) {
            return fail(ErrorCode::invalid_state,
                        "application lifecycle operation is already running");
        }
        app->lifecycle_busy = true;
        lifecycle_token = app->lifecycle_token = ++sequence_;
        vm = app->vm;
        main_class = app->main_class;
        was_foreground = foreground_app_id_ == app_id;
    }

    const auto fail_lifecycle = [&](Error error) -> Status {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app != nullptr && app->vm == vm &&
            app->lifecycle_token == lifecycle_token) {
            app->state = AppState::error;
            app->lifecycle_busy = false;
            app->generation = ++sequence_;
            last_exit_code_ = -1;
        }
        return std::unexpected(std::move(error));
    };

    std::scoped_lock vm_operation(vm->operation_mutex);
    auto resumed = vm->machine.invoke_instance(vm->midlet,
                                                main_class,
                                                "startApp",
                                                "()V");
    if (!resumed) return fail_lifecycle(resumed.error());
    auto completion = require_normal_completion(vm->machine,
                                                *resumed,
                                                "MIDlet startApp");
    if (!completion) return fail_lifecycle(completion.error());

    const vm::MidletSignal signal = vm->machine.consume_midlet_signal();
    const bool active = signal != vm::MidletSignal::destroyed &&
                        signal != vm::MidletSignal::paused;
    if (signal == vm::MidletSignal::paused) {
        vm->machine.media().suspend();
    } else if (signal != vm::MidletSignal::destroyed) {
        vm->machine.media().resume();
    }
    if (was_foreground && signal != vm::MidletSignal::destroyed) {
        vm->machine.scheduler().set_host_foreground(active);
        auto visible = vm->canvas.set_host_foreground(active);
        if (!visible) return fail_lifecycle(visible.error());
        auto pumped = vm->canvas.pump();
        if (!pumped) return fail_lifecycle(pumped.error());
    }

    std::unique_lock lock(mutex_);
    App* app = find_app_unlocked(app_id);
    if (app == nullptr || app->vm != vm ||
        app->lifecycle_token != lifecycle_token) {
        return fail(ErrorCode::invalid_state,
                    "application lifecycle operation was superseded");
    }
    app->lifecycle_busy = false;
    if (signal == vm::MidletSignal::destroyed) {
        app->state = AppState::destroyed;
        app->vm.reset();
        if (foreground_app_id_ == app_id) {
            foreground_app_id_ = {};
            framebuffer_.clear();
        }
    } else if (signal == vm::MidletSignal::paused) {
        app->state = AppState::paused;
    } else {
        app->state = AppState::active;
    }
    app->generation = ++sequence_;
    return {};
}

Status Runtime::destroy_midlet(AppId app_id) {
    std::shared_ptr<ApplicationVM> vm;
    std::string main_class;
    bool was_foreground = false;
    u64 lifecycle_token = 0;
    {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "application does not exist");
        }
        if (app->state == AppState::destroyed || app->vm == nullptr) {
            return {};
        }
        if (app->lifecycle_busy) {
            return fail(ErrorCode::invalid_state,
                        "application lifecycle operation is already running");
        }
        app->lifecycle_busy = true;
        lifecycle_token = app->lifecycle_token = ++sequence_;
        vm = app->vm;
        main_class = app->main_class;
        was_foreground = foreground_app_id_ == app_id;
    }

    const auto fail_lifecycle = [&](Error error) -> Status {
        std::unique_lock lock(mutex_);
        App* app = find_app_unlocked(app_id);
        if (app != nullptr && app->vm == vm &&
            app->lifecycle_token == lifecycle_token) {
            app->state = AppState::error;
            app->lifecycle_busy = false;
            app->generation = ++sequence_;
            last_exit_code_ = -1;
        }
        return std::unexpected(std::move(error));
    };

    std::scoped_lock vm_operation(vm->operation_mutex);
    if (was_foreground) {
        auto hidden = vm->canvas.set_host_foreground(false);
        if (!hidden) return fail_lifecycle(hidden.error());
        auto pumped = vm->canvas.pump();
        if (!pumped) return fail_lifecycle(pumped.error());
        vm->machine.scheduler().set_host_foreground(false);
    }

    const vm::Value unconditional = vm::Value::from_int(1);
    auto destroyed = vm->machine.invoke_instance(
        vm->midlet,
        main_class,
        "destroyApp",
        "(Z)V",
        std::span<const vm::Value>(&unconditional, 1));
    if (!destroyed) return fail_lifecycle(destroyed.error());
    if (!destroyed->completed_normally()) {
        if (!destroyed->throwable.has_value()) {
            return fail_lifecycle(fail(
                ErrorCode::internal_error,
                "MIDlet destroyApp failed without a Java throwable").error());
        }
        auto throwable_class = vm->machine.heap().class_name(
            *destroyed->throwable);
        if (!throwable_class) {
            return fail_lifecycle(throwable_class.error());
        }
        std::string diagnostic =
            "[Lifecycle] forced destroy ignored " + *throwable_class;
        if (!destroyed->exception_context.empty()) {
            diagnostic += " from " + destroyed->exception_context;
        }
        diagnostic.push_back('\n');
        std::u16string utf16;
        utf16.reserve(diagnostic.size());
        for (const char character : diagnostic) {
            utf16.push_back(static_cast<char16_t>(
                static_cast<unsigned char>(character)));
        }
        vm->machine.append_console(utf16);
    }

    std::unique_lock lock(mutex_);
    App* app = find_app_unlocked(app_id);
    if (app == nullptr || app->vm != vm ||
        app->lifecycle_token != lifecycle_token) {
        return fail(ErrorCode::invalid_state,
                    "application lifecycle operation was superseded");
    }
    app->state = AppState::destroyed;
    app->lifecycle_busy = false;
    app->generation = ++sequence_;
    app->vm.reset();
    if (foreground_app_id_ == app_id) {
        foreground_app_id_ = {};
        framebuffer_.clear();
    }
    return {};
}

Status Runtime::set_push_background_policy(
    SuiteId suite_id,
    push::BackgroundPolicy policy) {
    std::string push_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "runtime must be configured before PushRegistry access");
        }
        if (!suite_id.valid() || suite_store_.find(suite_id) == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "PushRegistry suite ID does not exist");
        }
        push_root = runtime_home_ + "/push";
    }

    push::PushRegistry registry;
    auto configured = registry.configure(std::move(push_root), suite_id);
    if (!configured) return configured;
    return registry.set_background_policy(policy);
}

Status Runtime::notify_push_connection_available(
    SuiteId suite_id,
    std::string connection,
    i64 received_at_millis) {
    std::string push_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "runtime must be configured before PushRegistry access");
        }
        if (!suite_id.valid() || suite_store_.find(suite_id) == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "PushRegistry suite ID does not exist");
        }
        push_root = runtime_home_ + "/push";
    }

    push::PushRegistry registry;
    auto configured = registry.configure(std::move(push_root), suite_id);
    if (!configured) return configured;
    return registry.notify_connection_available(connection,
                                                 received_at_millis);
}

Status Runtime::notify_push_connection_available(
    SuiteId suite_id,
    std::string connection,
    std::string source_address,
    i64 received_at_millis) {
    std::string push_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "runtime must be configured before PushRegistry access");
        }
        if (!suite_id.valid() || suite_store_.find(suite_id) == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "PushRegistry suite ID does not exist");
        }
        push_root = runtime_home_ + "/push";
    }

    push::PushRegistry registry;
    auto configured = registry.configure(std::move(push_root), suite_id);
    if (!configured) return configured;
    return registry.notify_connection_available(connection,
                                                 source_address,
                                                 received_at_millis);
}

Result<std::vector<push::LaunchRequest>>
Runtime::poll_push_launch_requests(
    SuiteId suite_id,
    i64 now_millis,
    bool background_execution_granted,
    usize limit) {
    std::string push_root;
    bool suite_is_foreground = false;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "runtime must be configured before PushRegistry access");
        }
        if (!suite_id.valid() || suite_store_.find(suite_id) == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "PushRegistry suite ID does not exist");
        }
        push_root = runtime_home_ + "/push";
        if (!suspended_ && foreground_app_id_.valid()) {
            const App* foreground = find_app_unlocked(foreground_app_id_);
            suite_is_foreground = foreground != nullptr &&
                                  foreground->suite_id == suite_id &&
                                  foreground->state == AppState::active;
        }
    }

    push::PushRegistry registry;
    auto configured = registry.configure(std::move(push_root), suite_id);
    if (!configured) return std::unexpected(configured.error());
    return registry.eligible_launch_requests(now_millis,
                                             suite_is_foreground,
                                             background_execution_granted,
                                             limit);
}

Status Runtime::acknowledge_push_launch_request(
    SuiteId suite_id,
    u64 request_id) {
    std::string push_root;
    {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
            return fail(ErrorCode::not_configured,
                        "runtime must be configured before PushRegistry access");
        }
        if (!suite_id.valid() || suite_store_.find(suite_id) == nullptr) {
            return fail(ErrorCode::invalid_argument,
                        "PushRegistry suite ID does not exist");
        }
        push_root = runtime_home_ + "/push";
    }

    push::PushRegistry registry;
    auto configured = registry.configure(std::move(push_root), suite_id);
    if (!configured) return configured;
    return registry.acknowledge_launch_request(request_id);
}

AppState Runtime::app_state(AppId app_id) const noexcept {
    std::scoped_lock lock(mutex_);
    const App* app = find_app_unlocked(app_id);
    return app == nullptr ? AppState::none : app->state;
}

AppId Runtime::foreground_app_id() const noexcept {
    std::scoped_lock lock(mutex_);
    return foreground_app_id_;
}

i64 Runtime::app_used_memory(AppId app_id) const noexcept {
    std::shared_ptr<ApplicationVM> vm;
    usize estimated = 0;
    {
        std::scoped_lock lock(mutex_);
        const App* app = find_app_unlocked(app_id);
        if (app == nullptr) return -1;
        estimated = sizeof(App) + app->main_class.capacity();
        vm = app->vm;
    }
    if (vm != nullptr) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        const vm::HeapStats heap_stats = vm->machine.heap().stats();
        if (heap_stats.estimated_bytes >
            std::numeric_limits<usize>::max() - estimated) {
            return std::numeric_limits<i64>::max();
        }
        estimated += heap_stats.estimated_bytes;
        const usize canvas_bytes = vm->canvas.estimated_bytes();
        if (canvas_bytes > std::numeric_limits<usize>::max() - estimated) {
            return std::numeric_limits<i64>::max();
        }
        estimated += canvas_bytes;
    }
    if (estimated > static_cast<usize>(std::numeric_limits<i64>::max())) {
        return std::numeric_limits<i64>::max();
    }
    return static_cast<i64>(estimated);
}

std::string Runtime::app_console_output(AppId app_id) const {
    std::shared_ptr<ApplicationVM> application_vm;
    {
        std::scoped_lock lock(mutex_);
        const App* app = find_app_unlocked(app_id);
        if (app == nullptr) return {};
        application_vm = app->vm;
    }
    if (application_vm == nullptr) return {};

    std::u16string console;
    {
        std::scoped_lock vm_operation(application_vm->operation_mutex);
        console = application_vm->machine.console_output();
    }
    std::string utf8;
    utf8.reserve(console.size());
    append_utf8(utf8, console);
    return utf8;
}

void Runtime::stop() noexcept {
    std::vector<App> stopping_apps;
    {
        std::unique_lock lock(mutex_);
        stopping_apps.reserve(apps_.size());
        for (auto& [id, app] : apps_) {
            (void)id;
            app.state = AppState::destroyed;
            app.lifecycle_busy = true;
            app.lifecycle_token = ++sequence_;
            app.generation = sequence_;
            stopping_apps.push_back(app);
        }
        apps_.clear();
        permission_policies_.clear();
        input_queue_.clear();
        ui_queue_.clear();
        framebuffer_.clear();
        foreground_app_id_ = {};
        running_ = false;
        suspended_ = false;
        last_exit_code_ = 0;
    }

    // Never run Java or destroy a Machine while holding the Runtime state
    // mutex. MIDlet cleanup is best-effort during host shutdown; Machine's
    // scheduler cancellation guarantees a busy Java worker cannot hang here.
    const vm::Value unconditional = vm::Value::from_int(1);
    for (App& app : stopping_apps) {
        auto application_vm = std::move(app.vm);
        if (application_vm == nullptr) continue;
        {
            std::scoped_lock vm_operation(application_vm->operation_mutex);
            (void)application_vm->canvas.set_host_foreground(false);
            (void)application_vm->canvas.pump();
            application_vm->machine.scheduler().set_host_foreground(false);
            (void)application_vm->machine.invoke_instance(
                application_vm->midlet,
                app.main_class,
                "destroyApp",
                "(Z)V",
                std::span<const vm::Value>(&unconditional, 1));
        }
        // The VM owns operation_mutex. Its final shared_ptr must be released
        // only after the lock guard is gone; otherwise the guard unlocks a
        // mutex that was destroyed together with the VM.
        application_vm.reset();
    }
}

void Runtime::suspend() noexcept {
    std::vector<std::shared_ptr<ApplicationVM>> application_vms;
    std::shared_ptr<ApplicationVM> foreground_vm;
    AppId foreground_id;
    {
        std::unique_lock lock(mutex_);
        if (!running_ || suspended_) return;
        suspended_ = true;
        foreground_id = foreground_app_id_;
        const App* foreground = find_app_unlocked(foreground_id);
        if (foreground != nullptr) foreground_vm = foreground->vm;
        application_vms.reserve(apps_.size());
        for (const auto& [id, app] : apps_) {
            (void)id;
            if (app.vm != nullptr && app.state != AppState::destroyed) {
                application_vms.push_back(app.vm);
            }
        }
    }

    if (foreground_vm != nullptr) {
        std::scoped_lock foreground_operation(foreground_vm->operation_mutex);
        auto hidden = foreground_vm->canvas.set_host_foreground(false);
        auto pumped = hidden ? foreground_vm->canvas.pump()
                             : Status(std::unexpected(hidden.error()));
        if (!pumped) {
            std::unique_lock lock(mutex_);
            App* foreground = find_app_unlocked(foreground_id);
            if (foreground != nullptr && foreground->vm == foreground_vm) {
                mark_canvas_failure_unlocked(*foreground, pumped.error());
            }
        }
    }
    for (const auto& vm : application_vms) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        vm->machine.scheduler().set_host_foreground(false);
        vm->machine.media().suspend();
    }
}

void Runtime::resume() noexcept {
    std::vector<std::shared_ptr<ApplicationVM>> active_vms;
    std::shared_ptr<ApplicationVM> foreground_vm;
    AppId foreground_id;
    {
        std::unique_lock lock(mutex_);
        if (!running_ || !suspended_) return;
        suspended_ = false;
        active_vms.reserve(apps_.size());
        for (const auto& [id, app] : apps_) {
            (void)id;
            if (app.vm != nullptr && app.state == AppState::active) {
                active_vms.push_back(app.vm);
            }
        }
        foreground_id = foreground_app_id_;
        const App* foreground = find_app_unlocked(foreground_id);
        if (foreground != nullptr && foreground->vm != nullptr &&
            (foreground->state == AppState::active ||
             foreground->state == AppState::paused)) {
            foreground_vm = foreground->vm;
        }
    }

    for (const auto& vm : active_vms) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        vm->machine.media().resume();
    }
    if (foreground_vm != nullptr) {
        std::scoped_lock foreground_operation(foreground_vm->operation_mutex);
        foreground_vm->machine.scheduler().set_host_foreground(true);
        auto shown = foreground_vm->canvas.set_host_foreground(true);
        auto pumped = shown ? foreground_vm->canvas.pump()
                            : Status(std::unexpected(shown.error()));
        if (!pumped) {
            std::unique_lock lock(mutex_);
            App* foreground = find_app_unlocked(foreground_id);
            if (foreground != nullptr && foreground->vm == foreground_vm) {
                mark_canvas_failure_unlocked(*foreground, pumped.error());
            }
        }
    }
}

bool Runtime::is_running() const noexcept {
    std::scoped_lock lock(mutex_);
    return running_;
}

bool Runtime::is_suspended() const noexcept {
    std::scoped_lock lock(mutex_);
    return suspended_;
}

i32 Runtime::last_exit_code() const noexcept {
    std::scoped_lock lock(mutex_);
    return last_exit_code_;
}

void Runtime::send_key(i32 key_code, bool pressed) {
    {
        std::unique_lock lock(mutex_);
        if (!running_ || suspended_) return;
        App* app = find_app_unlocked(foreground_app_id_);
        if (app == nullptr || app->vm == nullptr ||
            app->state != AppState::active) {
            return;
        }
        input_queue_.push(InputEvent {
            .kind = InputKind::key,
            .app_id = app->id,
            .app_generation = app->generation,
            .first = mapped_key_code_unlocked(key_code),
            .second = pressed ? 1 : 0,
            .sequence = ++sequence_,
        });
    }
    dispatch_input();
}

void Runtime::send_pointer(i32 x, i32 y, i32 action) {
    {
        std::unique_lock lock(mutex_);
        if (!running_ || suspended_) return;
        App* app = find_app_unlocked(foreground_app_id_);
        if (app == nullptr || app->vm == nullptr ||
            app->state != AppState::active) {
            return;
        }
        input_queue_.push(InputEvent {
            .kind = InputKind::pointer,
            .app_id = app->id,
            .app_generation = app->generation,
            .first = x,
            .second = y,
            .third = action,
            .sequence = ++sequence_,
        });
    }
    dispatch_input();
}

FrameMetadata Runtime::frame_metadata() {
    std::shared_ptr<ApplicationVM> vm;
    AppId app_id;
    u64 generation = 0;
    {
        std::unique_lock lock(mutex_);
        if (running_ && !suspended_) {
            app_id = foreground_app_id_;
            const App* app = find_app_unlocked(app_id);
            if (app != nullptr && app->vm != nullptr &&
                (app->state == AppState::active ||
                 app->state == AppState::paused)) {
                vm = app->vm;
                generation = app->generation;
            }
        }
    }
    if (vm != nullptr) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        auto pumped = vm->canvas.pump();
        if (!pumped) {
            std::unique_lock lock(mutex_);
            App* app = find_app_unlocked(app_id);
            if (app != nullptr && app->vm == vm &&
                app->generation == generation) {
                mark_canvas_failure_unlocked(*app, pumped.error());
            }
        }
    }
    return framebuffer_.metadata();
}

FrameMetadata Runtime::copy_current_frame_rgba(
    std::span<u8> destination) const noexcept {
    return framebuffer_.copy_rgba(destination);
}

FrameSnapshot Runtime::frame_snapshot() {
    (void)frame_metadata();
    return framebuffer_.snapshot();
}

std::optional<UiEvent> Runtime::poll_ui_event() { return ui_queue_.pop(); }

void Runtime::ui_select_command(i32 command_id) {
    push_ui_action(100, command_id, 0, 0);
}

void Runtime::ui_focus_item(i32 component_id) {
    push_ui_action(101, component_id, 0, 0);
}

void Runtime::ui_activate_item(i32 component_id) {
    push_ui_action(102, component_id, 0, 0);
}

void Runtime::ui_set_text(i32 component_id,
                          std::string text,
                          i32 caret_position) {
    push_ui_action(103, component_id, caret_position, 0, std::move(text));
}

void Runtime::ui_set_choice(i32 component_id,
                            i32 element_index,
                            bool selected) {
    push_ui_action(104, component_id, element_index, selected ? 1 : 0);
}

void Runtime::ui_set_gauge(i32 component_id, i32 value) {
    push_ui_action(105, component_id, value, 0);
}

void Runtime::ui_set_date(i32 component_id, i64 unix_seconds) {
    push_ui_action(106, component_id, 0, unix_seconds);
}

void Runtime::ui_set_scroll_position(i32 position) {
    push_ui_action(107, 0, position, 0);
}

App* Runtime::find_app_unlocked(AppId app_id) noexcept {
    const auto iterator = apps_.find(app_id.value);
    return iterator == apps_.end() ? nullptr : &iterator->second;
}

const App* Runtime::find_app_unlocked(AppId app_id) const noexcept {
    const auto iterator = apps_.find(app_id.value);
    return iterator == apps_.end() ? nullptr : &iterator->second;
}

Status Runtime::require_running_unlocked() const {
    if (!running_) {
        return fail(ErrorCode::not_running, "runtime is not running");
    }
    return {};
}

i32 Runtime::mapped_key_code_unlocked(i32 host_key_code) const noexcept {
    switch (host_key_code) {
    case -1: return keymap_[0];
    case -2: return keymap_[1];
    case -3: return keymap_[2];
    case -4: return keymap_[3];
    case -5: return keymap_[4];
    case -6: return keymap_[5];
    case -7: return keymap_[6];
    default: return host_key_code;
    }
}

void Runtime::dispatch_input() {
    while (auto event = input_queue_.pop()) {
        std::shared_ptr<ApplicationVM> vm;
        {
            std::unique_lock lock(mutex_);
            if (!running_ || suspended_) return;
            const App* app = find_app_unlocked(event->app_id);
            if (app == nullptr || app->vm == nullptr ||
                app->generation != event->app_generation ||
                foreground_app_id_ != event->app_id ||
                app->state != AppState::active) {
                continue;
            }
            vm = app->vm;
        }

        std::scoped_lock vm_operation(vm->operation_mutex);
        if (event->kind == InputKind::key) {
            vm->canvas.enqueue_host_key(event->first,
                                        event->second != 0,
                                        event->sequence);
        } else {
            vm->canvas.enqueue_pointer(event->first,
                                       event->second,
                                       event->third,
                                       event->sequence);
        }
        auto pumped = vm->canvas.pump();
        if (!pumped) {
            std::unique_lock lock(mutex_);
            App* app = find_app_unlocked(event->app_id);
            if (app != nullptr && app->vm == vm &&
                app->generation == event->app_generation) {
                mark_canvas_failure_unlocked(*app, pumped.error());
            }
            return;
        }
    }
}

void Runtime::mark_canvas_failure_unlocked(App& app, const Error& error) {
    char message[1536] {};
    std::snprintf(message,
                  sizeof(message),
                  "phoneME app %d Canvas dispatcher failed: %s%s%s",
                  app.id.value,
                  error.java_exception_class.empty()
                      ? ""
                      : error.java_exception_class.c_str(),
                  error.java_exception_class.empty() ? "" : ": ",
                  error.message.c_str());
    std::fprintf(stderr, "%s\n", message);
#if defined(__APPLE__)
    os_log_error(OS_LOG_DEFAULT, "%{public}s", message);
#endif

    app.state = AppState::error;
    app.generation = ++sequence_;
    last_exit_code_ = -1;
    ui_queue_.push(UiEvent {
        .kind = -1,
        .component_id = app.id.value,
        .generation = sequence_,
        .detail = "Canvas dispatcher failed: " + error.message,
    });
}

void Runtime::push_ui_action(i32 kind,
                             i32 component_id,
                             i32 first,
                             i64 value64,
                             std::string text) {
    std::shared_ptr<ApplicationVM> vm;
    AppId app_id;
    u64 app_generation = 0;
    {
        std::unique_lock lock(mutex_);
        if (!running_ || suspended_) return;
        app_id = foreground_app_id_;
        const App* app = find_app_unlocked(app_id);
        if (app != nullptr && app->vm != nullptr &&
            (app->state == AppState::active ||
             app->state == AppState::paused)) {
            vm = app->vm;
            app_generation = app->generation;
        }
    }

    if (vm != nullptr) {
        std::scoped_lock vm_operation(vm->operation_mutex);
        auto handled = vm::handle_lcdui_action(vm->machine,
                                              kind,
                                              component_id,
                                              first,
                                              value64,
                                              text);
        if (handled) {
            auto pumped = vm->canvas.pump();
            if (!pumped) {
                std::unique_lock lock(mutex_);
                App* app = find_app_unlocked(app_id);
                if (app != nullptr && app->vm == vm &&
                    app->generation == app_generation) {
                    mark_canvas_failure_unlocked(*app, pumped.error());
                }
            }
            return;
        }
        std::unique_lock lock(mutex_);
        ui_queue_.push(UiEvent {
            .kind = kind,
            .component_id = component_id,
            .arguments = {first, 0, 0, 0},
            .value64 = value64,
            .generation = ++sequence_,
            .text = std::move(text),
            .detail = handled.error().message,
        });
        return;
    }

    std::unique_lock lock(mutex_);
    ui_queue_.push(UiEvent {
        .kind = kind,
        .component_id = component_id,
        .arguments = {first, 0, 0, 0},
        .value64 = value64,
        .generation = ++sequence_,
        .text = std::move(text),
    });
}

} // namespace phoneme::runtime
