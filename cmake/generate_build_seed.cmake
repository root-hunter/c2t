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

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR must be specified")
endif()

# Generate high-entropy unique seed for TLS section and PE Manifest resource
string(TIMESTAMP C2T_BUILD_TIME_STAMP "%s%f")
string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" C2T_BUILD_RANDOM1)
string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" C2T_BUILD_RANDOM2)
string(SHA256 C2T_TLS_HASH "c2t-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM1}-${C2T_BUILD_RANDOM2}-${BINARY_DIR}")

# Build byte list for C array (32 bytes = 64 hex characters)
set(C2T_TLS_BYTE_LIST "")
foreach(i RANGE 0 62 2)
    string(SUBSTRING "${C2T_TLS_HASH}" ${i} 2 C2T_HEX_PAIR)
    list(APPEND C2T_TLS_BYTE_LIST "0x${C2T_HEX_PAIR}")
endforeach()
string(JOIN ", " C2T_TLS_SEED_FMT ${C2T_TLS_BYTE_LIST})
set(C2T_BUILD_MANIFEST_NONCE "${C2T_TLS_HASH}")

file(MAKE_DIRECTORY "${BINARY_DIR}/generated")

# Write generated/c2t_tls_seed.h
file(WRITE "${BINARY_DIR}/generated/c2t_tls_seed.h"
"/* Auto-generated dynamic build seed header - DO NOT EDIT */
#ifndef C2T_TLS_SEED_H
#define C2T_TLS_SEED_H

#define C2T_TLS_SEED_DATA { ${C2T_TLS_SEED_FMT} }
#define C2T_BUILD_NONCE_STR \"${C2T_BUILD_MANIFEST_NONCE}\"

#endif /* C2T_TLS_SEED_H */
"
)

# Extract version components if PROJECT_VERSION is set
if(DEFINED PROJECT_VERSION AND PROJECT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    set(PROJECT_VERSION_MAJOR "1")
    set(PROJECT_VERSION_MINOR "0")
    set(PROJECT_VERSION_PATCH "0")
endif()

set(CMAKE_CURRENT_BINARY_DIR "${BINARY_DIR}")
set(CMAKE_CURRENT_SOURCE_DIR "${SOURCE_DIR}")

# Generate dynamic generic manifest without project-specific fingerprints or static signatures
string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" C2T_MANIFEST_ID)
string(RANDOM LENGTH 4 ALPHABET "0123456789" C2T_BUILD_NUM)
string(RANDOM LENGTH 3 ALPHABET "0123456789" C2T_REV_NUM)

set(C2T_MANIFEST_XML
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">
  <assemblyIdentity
    version=\"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${C2T_BUILD_NUM}.${C2T_REV_NUM}\"
    processorArchitecture=\"*\"
    name=\"Microsoft.Windows.Common-Application.${C2T_MANIFEST_ID}\"
    type=\"win32\"/>
  <description>Windows Host Process</description>
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity
        type=\"win32\"
        name=\"Microsoft.Windows.Common-Controls\"
        version=\"6.0.0.0\"
        processorArchitecture=\"*\"
        publicKeyToken=\"6595b64144ccf1df\"
        language=\"*\"/>
    </dependentAssembly>
  </dependency>
  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">
    <application>
      <!-- Windows 10 and Windows 11 -->
      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>
      <!-- Windows 8.1 -->
      <supportedOS Id=\"{1f676c76-80e1-4239-95bb-83d0f6d0da78}\"/>
      <!-- Windows 8 -->
      <supportedOS Id=\"{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}\"/>
      <!-- Windows 7 -->
      <supportedOS Id=\"{35138b9a-5d96-4fbd-8e2d-a2440225f93a}\"/>
      <!-- Windows Vista -->
      <supportedOS Id=\"{e2011457-1546-43c5-a5fe-008deee3d3f0}\"/>
    </application>
  </compatibility>
  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <windowsSettings>
      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>
      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2, PerMonitor</dpiAwareness>
    </windowsSettings>
  </application>
</assembly>
")

file(WRITE "${BINARY_DIR}/c2t.manifest" "${C2T_MANIFEST_XML}")

# Configure c2t.rc
if(EXISTS "${SOURCE_DIR}/src/c2t.rc.in")
    configure_file("${SOURCE_DIR}/src/c2t.rc.in" "${BINARY_DIR}/c2t.rc" @ONLY)
endif()
