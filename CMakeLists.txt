# Copyright (C) 2026 roothunter
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.24)

project(c2t VERSION 0.4.37 LANGUAGES C)

include(FetchContent)
include(CTest)
include(CheckCCompilerFlag)

# Generate dynamic TLS section seed & Manifest nonce per build configuration
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_build_seed.cmake
)

add_custom_target(c2t_build_seed
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_build_seed.cmake
    BYPRODUCTS
        ${CMAKE_CURRENT_BINARY_DIR}/generated/c2t_tls_seed.h
        ${CMAKE_CURRENT_BINARY_DIR}/c2t.manifest
    COMMENT "Generating unique build TLS seed and manifest nonce..."
)

configure_file(src/version.h.in generated/c2t_version.h @ONLY)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC has no per-function target attribute. Keep the AVX2 and AVX-512 kernels
# in dedicated objects, while crypto.c remains at the x64 SSE2 baseline and
# calls either object only after runtime ISA detection.
set(C2T_MSVC_SIMD_SOURCE "")
if(MSVC AND CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    set(C2T_MSVC_SIMD_SOURCE
        src/crypto/crypto_msvc_simd.c
        src/crypto/crypto_msvc_avx512.c
    )
    set_source_files_properties(src/crypto/crypto_msvc_simd.c PROPERTIES
        COMPILE_OPTIONS "/arch:AVX2"
    )
    set_source_files_properties(src/crypto/crypto_msvc_avx512.c PROPERTIES
        COMPILE_OPTIONS "/arch:AVX512"
    )
    add_compile_definitions(C2T_MSVC_X86_SIMD=1)
endif()

# Keep the SVE2 kernel in a dedicated translation unit.  The rest of the
# AArch64 binary therefore retains its baseline ISA and can safely run on
# machines which only provide NEON; crypto.c selects SVE2 after an HWCAP check.
set(C2T_ARM_SVE2_SOURCE "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND
   CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    check_c_compiler_flag("-march=armv8-a+sve2" C2T_COMPILER_HAS_SVE2)
    if(C2T_COMPILER_HAS_SVE2)
        set(C2T_ARM_SVE2_SOURCE src/crypto/crypto_arm_sve2.c)
        set_source_files_properties(src/crypto/crypto_arm_sve2.c PROPERTIES
            COMPILE_OPTIONS "-march=armv8-a+sve2"
        )
        add_compile_definitions(C2T_ARM_SVE2_SIMD=1)
    endif()
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(c2t
    src/main.c
    src/clipboard/clipboard_output.c
    src/config/config.c
    src/config/embedded_config.c
    src/crypto/arena.c
    src/crypto/crypto.c
    ${C2T_MSVC_SIMD_SOURCE}
    ${C2T_ARM_SVE2_SOURCE}
    src/files/files.c
    src/keyboard/keyboard_output.c
    src/logging/logging.c
    src/logging/log_sender.c
    src/runtime/binding.c
    src/runtime/runtime.c
    src/runtime/tls_seed.c
    src/screenshot/screenshot.c
    src/screenshot/screenshot_encoder.c
    src/screenshot/screenshot_output.c
    src/telegram/telegram.c
    src/telegram/telegram_listener.c
    src/win32/win32_api.c
)
add_dependencies(c2t c2t_build_seed)
set_property(SOURCE src/runtime/tls_seed.c PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/c2t_tls_seed.h")
target_compile_features(c2t PRIVATE c_std_23)
target_include_directories(c2t PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(MSVC)
    # Visual Studio 2026 still gates parts of its C11/C23 support.
    target_compile_definitions(c2t PRIVATE
        $<$<COMPILE_LANGUAGE:C>:nullptr=NULL>
    )
    target_compile_options(c2t PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
    )
endif()

option(C2T_ENABLE_PROCESS_MASQUERADE "Enable process name cloaking/spoofing" OFF)
if(C2T_ENABLE_PROCESS_MASQUERADE)
    target_compile_definitions(c2t PRIVATE C2T_ENABLE_PROCESS_MASQUERADE=1)
endif()

option(C2T_ENABLE_NATIVE_TUNING "Enable architecture-specific SIMD hardware tuning (-march=native)" OFF)
if(C2T_ENABLE_NATIVE_TUNING)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(c2t PRIVATE -march=native -mtune=native)
    endif()
endif()

option(C2T_USE_LEGACY_KEYBOARD_HOOK "Use legacy WH_KEYBOARD_LL Windows hook instead of Raw Input" OFF)
if(C2T_USE_LEGACY_KEYBOARD_HOOK)
    target_compile_definitions(c2t PRIVATE C2T_USE_LEGACY_KEYBOARD_HOOK=1)
endif()

# Authenticode Code Signing Configuration (Windows)
option(C2T_ENABLE_SIGNING "Enable Authenticode signing for Windows binary" OFF)
set(C2T_SIGN_CERT_FILE "" CACHE FILEPATH "Path to PKCS#12 (.pfx / .p12) certificate file")
set(C2T_SIGN_CERT_PASSWORD "" CACHE STRING "Password for certificate file")
set(C2T_SIGN_TIMESTAMP_URL "http://timestamp.digicert.com" CACHE STRING "RFC 3161 timestamp server URL")
set(C2T_SIGNTOOL_PATH "" CACHE FILEPATH "Custom path to signtool.exe or osslsigncode")

set_target_properties(c2t PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_compile_options(c2t PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-ffunction-sections;-fdata-sections>
    $<$<AND:$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-fno-unwind-tables;-fno-asynchronous-unwind-tables>
    $<$<AND:$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>,$<C_COMPILER_ID:GNU,Clang>>:-fno-ident>
    $<$<AND:$<CONFIG:MinSizeRel>,$<C_COMPILER_ID:Clang,AppleClang>>:-Oz>
    $<$<AND:$<CONFIG:Release>,$<C_COMPILER_ID:GNU,Clang,AppleClang>>:-O3;-fmerge-all-constants;-fno-math-errno;-fomit-frame-pointer;-ftree-vectorize;-falign-functions=32>
)
target_link_options(c2t PRIVATE
    $<$<AND:$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--gc-sections>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<AND:$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>,$<C_COMPILER_ID:GNU,Clang>>:-s>
    $<$<AND:$<PLATFORM_ID:Windows>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--nxcompat;-Wl,--dynamicbase;-Wl,--high-entropy-va>
)

include(CheckIPOSupported)
check_ipo_supported(RESULT C2T_IPO_SUPPORTED OUTPUT C2T_IPO_ERROR)
if(C2T_IPO_SUPPORTED)
    set_property(TARGET c2t PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    set_property(TARGET c2t PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO is unavailable: ${C2T_IPO_ERROR}")
endif()

include(CheckLinkerFlag)
check_linker_flag(C "-fuse-ld=lld" C2T_LINKER_HAS_LLD)
if(C2T_LINKER_HAS_LLD AND NOT APPLE AND NOT WIN32)
    target_link_options(c2t PRIVATE
        $<$<C_COMPILER_ID:Clang>:-fuse-ld=lld>
    )
endif()

check_linker_flag(C "-Wl,--icf=all" C2T_LINKER_HAS_ICF_ALL)
if(C2T_LINKER_HAS_ICF_ALL)
    target_link_options(c2t PRIVATE
        $<$<AND:$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>,$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--icf=all>
    )
else()
    check_linker_flag(C "-Wl,--icf=safe" C2T_LINKER_HAS_ICF_SAFE)
    if(C2T_LINKER_HAS_ICF_SAFE)
        target_link_options(c2t PRIVATE
            $<$<AND:$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>,$<NOT:$<PLATFORM_ID:Darwin>>,$<C_COMPILER_ID:GNU,Clang>>:-Wl,--icf=safe>
        )
    endif()
endif()

if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/c2t.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/c2t.rc"
        @ONLY
    )
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/c2t.rc" PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/c2t.manifest")
    target_sources(c2t PRIVATE
        src/clipboard/clipboard_windows.c
        src/keyboard/keyboard_windows.c
        src/screenshot/screenshot_windows.c
        src/telegram/telegram_windows.c
        "${CMAKE_CURRENT_BINARY_DIR}/c2t.rc"
    )
    # Windows APIs (user32, winhttp, bcrypt, etc.) are loaded dynamically at runtime via win32_api.c
    # target_link_libraries(c2t PRIVATE user32 winhttp bcrypt)

    if(MSVC)
        # c2t.rc embeds the project's manifest as resource ID 1 already.
        target_link_options(c2t PRIVATE /MANIFEST:NO)
    elseif(MINGW)
        target_link_options(c2t PRIVATE
            -static
            -static-libgcc
            -Wl,--dynamicbase
            -Wl,--nxcompat
            -Wl,--high-entropy-va
            $<$<OR:$<CONFIG:MinSizeRel>,$<CONFIG:Release>>:-Wl,--build-id=uuid>
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCSourceCompiles)
    check_c_source_compiles([[
        #include <features.h>
        #ifdef __GLIBC__
        #error "glibc static binaries may load incompatible host NSS modules"
        #endif
        int main(void) { return 0; }
    ]] C2T_STATIC_LIBC_IS_PORTABLE)

    if(NOT DEFINED C2T_STANDALONE_LINUX)
        if(C2T_STATIC_LIBC_IS_PORTABLE)
            set(C2T_STANDALONE_LINUX_DEFAULT ON)
        else()
            set(C2T_STANDALONE_LINUX_DEFAULT OFF)
        endif()
    else()
        set(C2T_STANDALONE_LINUX_DEFAULT ${C2T_STANDALONE_LINUX})
    endif()

    option(C2T_STANDALONE_LINUX
        "Build a fully static Linux executable (requires musl)" ${C2T_STANDALONE_LINUX_DEFAULT})

    if(C2T_STANDALONE_LINUX AND NOT C2T_STATIC_LIBC_IS_PORTABLE)
        message(FATAL_ERROR
            "The standalone Linux build requires musl. For a dynamic "
            "glibc build, configure with -DC2T_STANDALONE_LINUX=OFF.")
    endif()

    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_DOH ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_FORM_API ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_HSTS ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_HTTP_AUTH ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_MIME ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_PARSEDATE ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_PROGRESS_METER ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_PROXY OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_VERBOSE_STRINGS ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        jitterentropy
        URL https://github.com/smuellerDD/jitterentropy-library/archive/refs/tags/v3.6.3.tar.gz
        URL_HASH SHA256=9b3819e275e6473a94c70cfaae7cc2a87a11f60b862e694745daa5d232b0eea3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(jitterentropy)

    set(OPENSSL_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL REQUIRED)

    FetchContent_Declare(
        curl
        URL https://curl.se/download/curl-8.21.0.tar.xz
        URL_HASH SHA256=aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)

    find_path(XCB_INCLUDE_DIR xcb/xcb.h REQUIRED)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(C2T_DBUS QUIET dbus-1)
    endif()
    target_include_directories(c2t PRIVATE "${XCB_INCLUDE_DIR}")
    if(C2T_DBUS_FOUND)
        target_compile_definitions(c2t PRIVATE C2T_HAS_DBUS_ABI=1)
        target_include_directories(c2t PRIVATE ${C2T_DBUS_INCLUDE_DIRS})
    else()
        message(STATUS "libdbus headers unavailable: native Wayland portal capture disabled")
    endif()
    target_sources(c2t PRIVATE src/clipboard/clipboard_x11.c src/keyboard/keyboard_linux.c src/screenshot/screenshot_linux.c src/telegram/telegram_curl.c)
    target_link_directories(c2t PRIVATE "$<TARGET_FILE_DIR:jitterentropy>")
    target_link_libraries(c2t PRIVATE
        "-Wl,--start-group"
        CURL::libcurl
        xcb Xau Xdmcp pthread ${CMAKE_DL_LIBS}
        OpenSSL::SSL OpenSSL::Crypto
        zstd jitterentropy
        "-Wl,--end-group"
    )
    if(C2T_STANDALONE_LINUX)
        target_link_options(c2t PRIVATE -static)
    endif()
elseif(APPLE)
    enable_language(OBJC)
    find_library(C2T_CURL_LIBRARY curl REQUIRED)
    find_library(C2T_APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(C2T_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_sources(c2t PRIVATE
        src/clipboard/clipboard_macos.m
        src/keyboard/keyboard_macos.m
        src/screenshot/screenshot_macos.m
        src/telegram/telegram_curl.c
    )
    set_source_files_properties(src/clipboard/clipboard_macos.m src/keyboard/keyboard_macos.m src/screenshot/screenshot_macos.m PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
    set_target_properties(c2t PROPERTIES
        MACOSX_DEPLOYMENT_TARGET 12.0
    )
    target_link_libraries(c2t PRIVATE
        "${C2T_CURL_LIBRARY}"
        "${C2T_APPKIT_FRAMEWORK}"
        "${C2T_COREGRAPHICS_FRAMEWORK}"
    )
else()
    message(FATAL_ERROR "Unsupported system: Linux, macOS and Windows are supported")
endif()

if(WIN32)
    set(C2T_BINARY_ANALYZER "${CMAKE_OBJDUMP}")
elseif(APPLE)
    find_program(C2T_BINARY_ANALYZER otool REQUIRED)
else()
    set(C2T_BINARY_ANALYZER "${CMAKE_READELF}")
endif()

add_custom_command(TARGET c2t POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DPLATFORM=${CMAKE_SYSTEM_NAME}"
        "-DEXECUTABLE=$<TARGET_FILE:c2t>"
        "-DANALYZER=${C2T_BINARY_ANALYZER}"
        "-DSTANDALONE=${C2T_STANDALONE_LINUX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_single_file.cmake"
    COMMENT "Verifying the c2t binary layout"
    VERBATIM
)

if(WIN32 AND C2T_ENABLE_SIGNING)
    if(NOT C2T_SIGN_CERT_FILE)
        message(WARNING "C2T_ENABLE_SIGNING is ON but C2T_SIGN_CERT_FILE is not specified")
    else()
        if(NOT C2T_SIGNTOOL_PATH)
            find_program(C2T_SIGNTOOL_EXECUTABLE NAMES signtool signtool.exe osslsigncode)
        else()
            set(C2T_SIGNTOOL_EXECUTABLE "${C2T_SIGNTOOL_PATH}")
        endif()

        if(C2T_SIGNTOOL_EXECUTABLE)
            if(C2T_SIGNTOOL_EXECUTABLE MATCHES "osslsigncode")
                set(SIGN_CMD "${C2T_SIGNTOOL_EXECUTABLE}" sign -pkcs12 "${C2T_SIGN_CERT_FILE}")
                if(C2T_SIGN_CERT_PASSWORD)
                    list(APPEND SIGN_CMD -pass "${C2T_SIGN_CERT_PASSWORD}")
                endif()
                if(C2T_SIGN_TIMESTAMP_URL)
                    list(APPEND SIGN_CMD -ts "${C2T_SIGN_TIMESTAMP_URL}")
                endif()
                list(APPEND SIGN_CMD -in "$<TARGET_FILE:c2t>" -out "$<TARGET_FILE:c2t>.signed")
                add_custom_command(TARGET c2t POST_BUILD
                    COMMAND ${SIGN_CMD}
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:c2t>.signed" "$<TARGET_FILE:c2t>"
                    COMMAND "${CMAKE_COMMAND}" -E remove -f "$<TARGET_FILE:c2t>.signed"
                    COMMENT "Signing c2t executable with osslsigncode"
                    VERBATIM
                )
            else()
                set(SIGN_CMD "${C2T_SIGNTOOL_EXECUTABLE}" sign /fd sha256 /f "${C2T_SIGN_CERT_FILE}")
                if(C2T_SIGN_CERT_PASSWORD)
                    list(APPEND SIGN_CMD /p "${C2T_SIGN_CERT_PASSWORD}")
                endif()
                if(C2T_SIGN_TIMESTAMP_URL)
                    list(APPEND SIGN_CMD /tr "${C2T_SIGN_TIMESTAMP_URL}" /td sha256)
                endif()
                list(APPEND SIGN_CMD "$<TARGET_FILE:c2t>")
                add_custom_command(TARGET c2t POST_BUILD
                    COMMAND ${SIGN_CMD}
                    COMMENT "Signing c2t executable with signtool"
                    VERBATIM
                )
            endif()
        else()
            message(WARNING "Code signing enabled but neither signtool nor osslsigncode was found.")
        endif()
    endif()
endif()

if(BUILD_TESTING AND NOT WIN32)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Threads REQUIRED)
    find_program(C2T_NODE_EXECUTABLE node)
    add_executable(telegram_tests
        src/clipboard/clipboard_output.c
        src/config/config.c
        src/config/embedded_config.c
        src/crypto/arena.c
        src/crypto/crypto.c
        ${C2T_MSVC_SIMD_SOURCE}
        ${C2T_ARM_SVE2_SOURCE}
        src/files/files.c
        src/keyboard/keyboard_output.c
        src/logging/logging.c
        src/logging/log_sender.c
        src/runtime/runtime.c
        src/runtime/tls_seed.c
        src/screenshot/screenshot.c
        src/screenshot/screenshot_encoder.c
        src/screenshot/screenshot_output.c
        src/telegram/telegram.c
        src/telegram/telegram_listener.c
        tests/telegram_test.c
    )
    if(APPLE)
        target_sources(telegram_tests PRIVATE src/keyboard/keyboard_macos.m src/screenshot/screenshot_macos.m)
        set_source_files_properties(src/keyboard/keyboard_macos.m src/screenshot/screenshot_macos.m PROPERTIES
            COMPILE_OPTIONS "-fobjc-arc"
        )
        target_link_libraries(telegram_tests PRIVATE
            "${C2T_APPKIT_FRAMEWORK}"
            "${C2T_COREGRAPHICS_FRAMEWORK}"
        )
    else()
        target_sources(telegram_tests PRIVATE src/keyboard/keyboard_linux.c src/screenshot/screenshot_linux.c)
        target_include_directories(telegram_tests PRIVATE "${XCB_INCLUDE_DIR}")
        target_link_libraries(telegram_tests PRIVATE xcb ${CMAKE_DL_LIBS})
        if(C2T_DBUS_FOUND)
            target_compile_definitions(telegram_tests PRIVATE C2T_HAS_DBUS_ABI=1)
            target_include_directories(telegram_tests PRIVATE ${C2T_DBUS_INCLUDE_DIRS})
        endif()
    endif()
    target_include_directories(telegram_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(telegram_tests PRIVATE c_std_23)
    target_compile_options(telegram_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow>
    )
    target_link_libraries(telegram_tests PRIVATE Threads::Threads)
    if(C2T_ENABLE_PROCESS_MASQUERADE)
        target_compile_definitions(telegram_tests PRIVATE C2T_ENABLE_PROCESS_MASQUERADE=1)
    endif()
    if(C2T_USE_LEGACY_KEYBOARD_HOOK)
        target_compile_definitions(telegram_tests PRIVATE C2T_USE_LEGACY_KEYBOARD_HOOK=1)
    endif()
    add_test(NAME telegram_tests COMMAND telegram_tests)

    add_test(
        NAME provision_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provision_test.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_config.py"
            "$<TARGET_FILE:telegram_tests>"
    )
    add_test(
        NAME runtime_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_test.py"
            "$<TARGET_FILE:c2t>"
    )
    if(C2T_NODE_EXECUTABLE AND NOT APPLE)
        add_test(
            NAME browser_provision_tests
            COMMAND "${C2T_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/browser_provision_test.mjs"
                "$<TARGET_FILE:telegram_tests>"
        )
    endif()
    if(C2T_NODE_EXECUTABLE)
        add_test(NAME browser_platform_tests
            COMMAND "${C2T_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/platforms_test.mjs")
        add_test(NAME browser_archive_tests
            COMMAND "${C2T_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/archive_test.mjs")
    else()
        message(STATUS "Node.js is unavailable: browser provisioning test disabled")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(perf_bench
        src/clipboard/clipboard_output.c
        src/config/config.c
        src/config/embedded_config.c
        src/crypto/arena.c
        src/crypto/crypto.c
        ${C2T_MSVC_SIMD_SOURCE}
        ${C2T_ARM_SVE2_SOURCE}
        src/files/files.c
        src/keyboard/keyboard_output.c
        src/logging/logging.c
        src/logging/log_sender.c
        src/runtime/runtime.c
        src/runtime/tls_seed.c
        src/screenshot/screenshot.c
        src/screenshot/screenshot_encoder.c
        src/screenshot/screenshot_output.c
        src/telegram/telegram.c
        src/telegram/telegram_listener.c
        tests/perf_bench.c
    )
    if(APPLE)
        target_sources(perf_bench PRIVATE
            src/clipboard/clipboard_macos.m
            src/keyboard/keyboard_macos.m
            src/screenshot/screenshot_macos.m
            src/telegram/telegram_curl.c
        )
        set_source_files_properties(src/clipboard/clipboard_macos.m src/keyboard/keyboard_macos.m src/screenshot/screenshot_macos.m PROPERTIES
            COMPILE_OPTIONS "-fobjc-arc"
        )
        target_link_libraries(perf_bench PRIVATE
            "${C2T_CURL_LIBRARY}"
            "${C2T_APPKIT_FRAMEWORK}"
            "${C2T_COREGRAPHICS_FRAMEWORK}"
        )
    elseif(WIN32)
        target_sources(perf_bench PRIVATE
            src/clipboard/clipboard_windows.c
            src/keyboard/keyboard_windows.c
            src/screenshot/screenshot_windows.c
            src/telegram/telegram_windows.c
            src/win32/win32_api.c
        )
        # Windows APIs (user32, winhttp, bcrypt, psapi, etc.) are loaded dynamically at runtime via win32_api.c
        # target_link_libraries(perf_bench PRIVATE psapi bcrypt user32 winhttp)
    else()
        target_sources(perf_bench PRIVATE
            src/clipboard/clipboard_x11.c
            src/keyboard/keyboard_linux.c
            src/screenshot/screenshot_linux.c
            src/telegram/telegram_curl.c
        )
        target_include_directories(perf_bench PRIVATE "${XCB_INCLUDE_DIR}")
        target_link_directories(perf_bench PRIVATE "$<TARGET_FILE_DIR:jitterentropy>")
        target_link_libraries(perf_bench PRIVATE
            "-Wl,--start-group"
            CURL::libcurl
            xcb Xau Xdmcp pthread ${CMAKE_DL_LIBS}
            OpenSSL::SSL OpenSSL::Crypto
            zstd jitterentropy
            "-Wl,--end-group"
        )
        if(C2T_STANDALONE_LINUX)
            target_link_options(perf_bench PRIVATE -static)
        endif()
        if(C2T_DBUS_FOUND)
            target_compile_definitions(perf_bench PRIVATE C2T_HAS_DBUS_ABI=1)
            target_include_directories(perf_bench PRIVATE ${C2T_DBUS_INCLUDE_DIRS})
        endif()
    endif()
    target_include_directories(perf_bench PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(perf_bench PRIVATE c_std_23)
    if(MSVC)
        target_compile_definitions(perf_bench PRIVATE
            $<$<COMPILE_LANGUAGE:C>:nullptr=NULL>
        )
        target_compile_options(perf_bench PRIVATE
            $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
        )
    endif()
    target_compile_options(perf_bench PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-O3>
    )
    add_test(NAME perf_bench COMMAND perf_bench)
endif()
