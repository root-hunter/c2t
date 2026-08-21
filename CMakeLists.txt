cmake_minimum_required(VERSION 3.24)

project(c2t VERSION 0.1.6 LANGUAGES C)

include(FetchContent)
include(CTest)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

add_executable(c2t
    src/main.c
    src/clipboard/clipboard_output.c
    src/config/config.c
    src/config/embedded_config.c
    src/files/files.c
    src/logging/logging.c
    src/telegram/telegram.c
)
target_compile_features(c2t PRIVATE c_std_11)
target_compile_options(c2t PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-ffunction-sections;-fdata-sections>
)
target_link_options(c2t PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
    $<$<AND:$<CONFIG:MinSizeRel>,$<C_COMPILER_ID:GNU,Clang>>:-s>
)

include(CheckIPOSupported)
check_ipo_supported(RESULT C2T_IPO_SUPPORTED OUTPUT C2T_IPO_ERROR)
if(C2T_IPO_SUPPORTED)
    set_property(TARGET c2t PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
else()
    message(STATUS "IPO/LTO is unavailable: ${C2T_IPO_ERROR}")
endif()

if(WIN32)
    target_sources(c2t PRIVATE src/clipboard/clipboard_windows.c src/telegram/telegram_windows.c)
    target_link_libraries(c2t PRIVATE user32 winhttp)

    if(MINGW)
        target_link_options(c2t PRIVATE -static -static-libgcc)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(C2T_STANDALONE_LINUX
        "Build a fully static Linux executable (requires musl)" ON)
    if(C2T_STANDALONE_LINUX)
        include(CheckCSourceCompiles)
        check_c_source_compiles([[
            #include <features.h>
            #ifdef __GLIBC__
            #error "glibc static binaries may load incompatible host NSS modules"
            #endif
            int main(void) { return 0; }
        ]] C2T_STATIC_LIBC_IS_PORTABLE)
        if(NOT C2T_STATIC_LIBC_IS_PORTABLE)
            message(FATAL_ERROR
                "The standalone Linux build requires musl. For a dynamic "
                "glibc build, configure with -DC2T_STANDALONE_LINUX=OFF.")
        endif()
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
    set(CURL_DISABLE_PROXY ON CACHE BOOL "" FORCE)
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
        URL https://curl.se/download/curl-8.18.0.tar.xz
        URL_HASH SHA256=40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)

    find_path(XCB_INCLUDE_DIR xcb/xcb.h REQUIRED)
    target_include_directories(c2t PRIVATE "${XCB_INCLUDE_DIR}")
    target_sources(c2t PRIVATE src/clipboard/clipboard_x11.c src/telegram/telegram_curl.c)
    target_link_directories(c2t PRIVATE "$<TARGET_FILE_DIR:jitterentropy>")
    target_link_libraries(c2t PRIVATE
        "-Wl,--start-group"
        CURL::libcurl
        xcb Xau Xdmcp pthread
        OpenSSL::SSL OpenSSL::Crypto
        zstd jitterentropy
        "-Wl,--end-group"
    )
    if(C2T_STANDALONE_LINUX)
        target_link_options(c2t PRIVATE -static)
    endif()
else()
    message(FATAL_ERROR "Unsupported system: only Linux and Windows are supported")
endif()

if(WIN32)
    set(C2T_BINARY_ANALYZER "${CMAKE_OBJDUMP}")
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

if(BUILD_TESTING AND NOT WIN32)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(C2T_NODE_EXECUTABLE node)
    add_executable(telegram_tests
        src/clipboard/clipboard_output.c
        src/config/config.c
        src/config/embedded_config.c
        src/files/files.c
        src/logging/logging.c
        src/telegram/telegram.c
        tests/telegram_test.c
    )
    target_include_directories(telegram_tests PRIVATE src)
    target_compile_features(telegram_tests PRIVATE c_std_11)
    target_compile_options(telegram_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    target_link_libraries(telegram_tests PRIVATE pthread)
    add_test(NAME telegram_tests COMMAND telegram_tests)
    add_test(
        NAME provision_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provision_test.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_config.py"
            "$<TARGET_FILE:telegram_tests>"
    )
    if(C2T_NODE_EXECUTABLE)
        add_test(
            NAME browser_provision_tests
            COMMAND "${C2T_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/browser_provision_test.mjs"
                "$<TARGET_FILE:telegram_tests>"
        )
    else()
        message(STATUS "Node.js is unavailable: browser provisioning test disabled")
    endif()
endif()
