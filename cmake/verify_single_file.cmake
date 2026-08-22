# Copyright (C) 2026 Antonio Ricciardi
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

    if(STANDALONE AND program_headers MATCHES "INTERP")
        message(FATAL_ERROR
            "${EXECUTABLE} uses a dynamic interpreter and is not self-contained")
    elseif(NOT STANDALONE AND NOT program_headers MATCHES "INTERP")
        message(FATAL_ERROR
            "${EXECUTABLE} was expected to use the glibc dynamic interpreter")
    endif()

    file(READ "${EXECUTABLE}" executable_bytes HEX)
    if(NOT executable_bytes MATCHES "43325443464700a731d56c92e84bf01d")
        message(FATAL_ERROR
            "${EXECUTABLE} has no post-link configuration region")
    endif()

    if(STANDALONE)
        message(STATUS "Standalone Linux binary verified: no dynamic dependencies")
    else()
        message(STATUS "Dynamic Linux binary verified: glibc interpreter present")
    endif()
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
    set(allowed_system_dlls kernel32.dll msvcrt.dll user32.dll winhttp.dll bcrypt.dll)
    foreach(import IN LISTS imported_dlls)
        string(REGEX REPLACE "DLL Name: [ \t]*" "" dll "${import}")
        string(TOLOWER "${dll}" dll)
        if(NOT dll IN_LIST allowed_system_dlls)
            message(FATAL_ERROR
                "${EXECUTABLE} requires ${dll} and is not self-contained")
        endif()
    endforeach()

    file(READ "${EXECUTABLE}" executable_bytes HEX)
    if(NOT executable_bytes MATCHES "43325443464700a731d56c92e84bf01d")
        message(FATAL_ERROR
            "${EXECUTABLE} has no post-link configuration region")
    endif()

    message(STATUS "Single file verified with embedded configuration section: only Windows system DLLs are required")
elseif(PLATFORM STREQUAL "Darwin")
    execute_process(
        COMMAND "${ANALYZER}" -l "${EXECUTABLE}"
        RESULT_VARIABLE analyzer_result
        OUTPUT_VARIABLE load_commands
        ERROR_VARIABLE analyzer_error
    )
    if(NOT analyzer_result EQUAL 0)
        message(FATAL_ERROR "Unable to analyze ${EXECUTABLE}: ${analyzer_error}")
    endif()
    file(READ "${EXECUTABLE}" executable_bytes HEX)
    if(NOT executable_bytes MATCHES "43325443464700a731d56c92e84bf01d")
        message(FATAL_ERROR
            "${EXECUTABLE} has no configuration region")
    endif()
    if(NOT load_commands MATCHES "name /usr/lib/libSystem.B.dylib")
        message(FATAL_ERROR "${EXECUTABLE} has no macOS system runtime load command")
    endif()
    message(STATUS "macOS executable verified with native system dependencies and configuration section")
else()
    message(FATAL_ERROR "Single-file verification is not implemented for ${PLATFORM}")
endif()
