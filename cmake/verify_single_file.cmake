if(NOT DEFINED EXECUTABLE OR NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable to verify not found: ${EXECUTABLE}")
endif()

if(NOT DEFINED ANALYZER OR NOT EXISTS "${ANALYZER}")
    message(FATAL_ERROR "Binary analyzer not found: ${ANALYZER}")
endif()

if(PLATFORM STREQUAL "Linux")
    execute_process(
        COMMAND "${ANALYZER}" -l "${EXECUTABLE}"
        RESULT_VARIABLE analyzer_result
        OUTPUT_VARIABLE program_headers
        ERROR_VARIABLE analyzer_error
    )
    if(NOT analyzer_result EQUAL 0)
        message(FATAL_ERROR "Unable to analyze ${EXECUTABLE}: ${analyzer_error}")
    endif()

    if(program_headers MATCHES "INTERP")
        message(FATAL_ERROR
            "${EXECUTABLE} uses a dynamic interpreter and is not self-contained")
    endif()

    execute_process(
        COMMAND "${ANALYZER}" -S "${EXECUTABLE}"
        RESULT_VARIABLE section_result
        OUTPUT_VARIABLE sections
        ERROR_VARIABLE section_error
    )
    if(NOT section_result EQUAL 0 OR NOT sections MATCHES "\\.c2tcfg")
        message(FATAL_ERROR
            "${EXECUTABLE} has no post-link configuration section: ${section_error}")
    endif()

    message(STATUS "Single file verified with embedded configuration section: no dynamic Linux dependencies")
elseif(PLATFORM STREQUAL "Windows")
    execute_process(
        COMMAND "${ANALYZER}" -p "${EXECUTABLE}"
        RESULT_VARIABLE analyzer_result
        OUTPUT_VARIABLE pe_headers
        ERROR_VARIABLE analyzer_error
    )
    if(NOT analyzer_result EQUAL 0)
        message(FATAL_ERROR "Unable to analyze ${EXECUTABLE}: ${analyzer_error}")
    endif()

    string(REGEX MATCHALL "DLL Name: [^\r\n]+" imported_dlls "${pe_headers}")
    set(allowed_system_dlls kernel32.dll msvcrt.dll user32.dll winhttp.dll)
    foreach(import IN LISTS imported_dlls)
        string(REGEX REPLACE "DLL Name: [ \t]*" "" dll "${import}")
        string(TOLOWER "${dll}" dll)
        if(NOT dll IN_LIST allowed_system_dlls)
            message(FATAL_ERROR
                "${EXECUTABLE} requires ${dll} and is not self-contained")
        endif()
    endforeach()

    execute_process(
        COMMAND "${ANALYZER}" -h "${EXECUTABLE}"
        RESULT_VARIABLE section_result
        OUTPUT_VARIABLE sections
        ERROR_VARIABLE section_error
    )
    if(NOT section_result EQUAL 0 OR NOT sections MATCHES "\\.c2tcfg")
        message(FATAL_ERROR
            "${EXECUTABLE} has no post-link configuration section: ${section_error}")
    endif()

    message(STATUS "Single file verified with embedded configuration section: only Windows system DLLs are required")
else()
    message(FATAL_ERROR "Single-file verification is not implemented for ${PLATFORM}")
endif()
