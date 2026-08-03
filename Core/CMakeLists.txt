cmake_minimum_required(VERSION 3.25)

project(phoneMECore LANGUAGES CXX)

file(GLOB_RECURSE PHONEME_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(phoneMECore STATIC ${PHONEME_CORE_SOURCES})
add_library(phoneME::Core ALIAS phoneMECore)

target_compile_features(phoneMECore PUBLIC cxx_std_23)
target_include_directories(phoneMECore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_definitions(phoneMECore PRIVATE PHONEME_IPHONEOS_ONLY=1)
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
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 16.0
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES
    )
endif()
