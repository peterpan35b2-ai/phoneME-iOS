#include "PhoneMECore.h"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string_view bounded_text(const char* value,
                                            std::size_t capacity) noexcept {
    if (value == nullptr || capacity == 0U) return {};
    const void* terminator = std::memchr(value, '\0', capacity);
    const std::size_t size = terminator == nullptr
        ? capacity
        : static_cast<const char*>(terminator) - value;
    return std::string_view(value, size);
}

void append_json_string(std::string& destination, std::string_view value) {
    destination.push_back('"');
    constexpr char kHex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': destination += "\\\""; break;
        case '\\': destination += "\\\\"; break;
        case '\b': destination += "\\b"; break;
        case '\f': destination += "\\f"; break;
        case '\n': destination += "\\n"; break;
        case '\r': destination += "\\r"; break;
        case '\t': destination += "\\t"; break;
        default:
            if (byte < 0x20U) {
                destination += "\\u00";
                destination.push_back(kHex[(byte >> 4U) & 0x0FU]);
                destination.push_back(kHex[byte & 0x0FU]);
            } else {
                destination.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    destination.push_back('"');
}

void append_key(std::string& destination, std::string_view key) {
    append_json_string(destination, key);
    destination.push_back(':');
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char*
phoneme_web_poll_lcdui_event_json(PhoneMERuntimeRef runtime) {
    static thread_local std::string json;
    PhoneMELCDUIEvent event {};
    if (phoneme_poll_lcdui_event(runtime, &event) == 0) return nullptr;

    json.clear();
    json.reserve(512U + bounded_text(event.text, sizeof(event.text)).size() +
                 bounded_text(event.detail, sizeof(event.detail)).size());
    json.push_back('{');

    append_key(json, "kind");
    json += std::to_string(event.kind);
    append_key(json += ',', "componentId");
    json += std::to_string(event.component_id);
    append_key(json += ',', "parentId");
    json += std::to_string(event.parent_id);
    append_key(json += ',', "componentType");
    json += std::to_string(event.component_type);
    append_key(json += ',', "index");
    json += std::to_string(event.index);
    append_key(json += ',', "arg0");
    json += std::to_string(event.arg0);
    append_key(json += ',', "arg1");
    json += std::to_string(event.arg1);
    append_key(json += ',', "arg2");
    json += std::to_string(event.arg2);
    append_key(json += ',', "arg3");
    json += std::to_string(event.arg3);
    append_key(json += ',', "value64");
    json += std::to_string(event.value64);
    append_key(json += ',', "generation");
    json += std::to_string(event.generation);
    append_key(json += ',', "text");
    append_json_string(json, bounded_text(event.text, sizeof(event.text)));
    append_key(json += ',', "detail");
    append_json_string(json, bounded_text(event.detail, sizeof(event.detail)));
    json.push_back('}');
    return json.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char*
phoneme_web_error_name(std::int32_t code) noexcept {
    switch (code) {
    case PHONEME_OK: return "OK";
    case PHONEME_ERROR_INVALID_ARGUMENT: return "Đối số không hợp lệ";
    case PHONEME_ERROR_ALREADY_RUNNING: return "Hệ thống đã chạy";
    case PHONEME_ERROR_NOT_CONFIGURED: return "Core chưa được cấu hình";
    case PHONEME_ERROR_THREAD_CREATE: return "Không tạo được luồng WebAssembly";
    case PHONEME_ERROR_NOT_RUNNING: return "Hệ thống chưa chạy";
    case PHONEME_ERROR_SYSTEM_START: return "Không khởi động được hệ thống J2ME";
    case PHONEME_ERROR_INSTALL: return "Không cài đặt được JAR";
    case PHONEME_ERROR_UNSUPPORTED: return "Tính năng chưa được hỗ trợ trên web";
    case PHONEME_ERROR_IO: return "Lỗi đọc/ghi dữ liệu";
    case PHONEME_ERROR_MALFORMED_INPUT: return "JAR hoặc dữ liệu không hợp lệ";
    default: return "Lỗi phoneME không xác định";
    }
}

int main() {
    return 0;
}
