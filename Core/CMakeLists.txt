cmake_minimum_required(VERSION 3.25)

project(phoneMECore LANGUAGES CXX)

option(PHONEME_ENABLE_VM_PROFILING
       "Enable low-overhead VM performance counters"
       OFF)
option(PHONEME_ENABLE_DECODED_EXECUTION
       "Dispatch verified methods through immutable decoded opcode metadata"
       OFF)

file(GLOB_RECURSE PHONEME_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(phoneMECore STATIC ${PHONEME_CORE_SOURCES})
add_library(phoneME::Core ALIAS phoneMECore)

target_compile_features(phoneMECore PUBLIC cxx_std_23)
target_compile_definitions(phoneMECore PUBLIC
    PHONEME_ENABLE_VM_PROFILING=$<BOOL:${PHONEME_ENABLE_VM_PROFILING}>
    PHONEME_ENABLE_DECODED_EXECUTION=$<BOOL:${PHONEME_ENABLE_DECODED_EXECUTION}>
)
target_include_directories(phoneMECore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(EMSCRIPTEN)
    target_compile_definitions(phoneMECore PRIVATE PHONEME_WEB=1)
else()
    target_compile_definitions(phoneMECore PRIVATE PHONEME_IPHONEOS_ONLY=1)
endif()

target_compile_options(phoneMECore PRIVATE
    -fno-exceptions
    -fno-rtti
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Werror=return-type
)
target_link_libraries(phoneMECore PRIVATE z)
if(EMSCRIPTEN)
    target_compile_options(phoneMECore PRIVATE
        -pthread
        "SHELL:-sUSE_ZLIB=1"
    )
endif()
if(APPLE)
    target_link_libraries(phoneMECore PRIVATE
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework ImageIO"
    )
endif()

set_target_properties(phoneMECore PROPERTIES
    OUTPUT_NAME phoneMECore
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(phoneMECore PROPERTIES
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS iphoneos
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 15.0
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES
    )
endif()

if(EMSCRIPTEN)
    add_executable(phoneMEWeb Web/WebMain.cpp)
    target_compile_features(phoneMEWeb PRIVATE cxx_std_23)
    target_include_directories(phoneMEWeb PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(phoneMEWeb PRIVATE
        -pthread
        "SHELL:-sUSE_ZLIB=1"
    )
    target_link_libraries(phoneMEWeb PRIVATE phoneMECore z)
    target_link_options(phoneMEWeb PRIVATE
        -pthread
        "SHELL:-sUSE_ZLIB=1"
        "SHELL:-lidbfs.js"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_ES6=1"
        "SHELL:-sEXPORT_NAME=createPhoneMEModule"
        "SHELL:-sENVIRONMENT=web,worker"
        "SHELL:-sNO_EXIT_RUNTIME=1"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sWASM_BIGINT=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sINITIAL_MEMORY=134217728"
        "SHELL:-sMAXIMUM_MEMORY=1073741824"
        "SHELL:-sSTACK_SIZE=2097152"
        "SHELL:-sPTHREAD_POOL_SIZE=4"
        "SHELL:-sPTHREAD_POOL_SIZE_STRICT=0"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','HEAPU8','HEAP32','ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
        "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_phoneme_c_api_version','_phoneme_create','_phoneme_destroy','_phoneme_configure','_phoneme_configure_keymap','_phoneme_configure_app_frame_pacing','_phoneme_configure_app_heap','_phoneme_configure_translation','_phoneme_configure_translation_v2','_phoneme_configure_app_translation','_phoneme_configure_app_translation_v2','_phoneme_install_jar','_phoneme_uninstall_suite','_phoneme_set_suite_trust','_phoneme_start_system','_phoneme_start_midlet','_phoneme_set_foreground','_phoneme_pause_midlet','_phoneme_resume_midlet','_phoneme_destroy_midlet','_phoneme_midlet_state','_phoneme_foreground_app_id','_phoneme_midlet_used_memory','_phoneme_stop','_phoneme_suspend','_phoneme_resume','_phoneme_is_running','_phoneme_is_suspended','_phoneme_last_exit_code','_phoneme_send_key','_phoneme_send_pointer','_phoneme_pump_events','_phoneme_copy_frame_rgba','_phoneme_poll_lcdui_event','_phoneme_lcdui_select_command','_phoneme_lcdui_select_list_item_command','_phoneme_lcdui_focus_item','_phoneme_lcdui_activate_item','_phoneme_lcdui_set_text','_phoneme_lcdui_set_choice','_phoneme_lcdui_set_gauge','_phoneme_lcdui_set_date','_phoneme_lcdui_set_scroll_position','_phoneme_copy_lcdui_image_rgba','_phoneme_web_poll_lcdui_event_json','_phoneme_web_error_name']"
    )
    set_target_properties(phoneMEWeb PROPERTIES
        OUTPUT_NAME phoneme
        SUFFIX ".js"
    )
endif()
