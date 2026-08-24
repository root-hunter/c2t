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

#ifndef C2T_WIN32_API_H
#define C2T_WIN32_API_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <winhttp.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic function pointer typedefs */
typedef DWORD(WINAPI *pfn_GetWindowThreadProcessId)(HWND hWnd,
                                                    LPDWORD lpdwProcessId);
typedef BOOL(WINAPI *pfn_OpenClipboard)(HWND hWndNewOwner);
typedef BOOL(WINAPI *pfn_CloseClipboard)(VOID);
typedef HANDLE(WINAPI *pfn_GetClipboardData)(UINT uFormat);
typedef BOOL(WINAPI *pfn_IsClipboardFormatAvailable)(UINT format);
typedef BOOL(WINAPI *pfn_EmptyClipboard)(VOID);
typedef HANDLE(WINAPI *pfn_SetClipboardData)(UINT uFormat, HANDLE hMem);
typedef BOOL(WINAPI *pfn_AddClipboardFormatListener)(HWND hwnd);
typedef BOOL(WINAPI *pfn_RemoveClipboardFormatListener)(HWND hwnd);
typedef HWND(WINAPI *pfn_GetForegroundWindow)(VOID);
typedef int(WINAPI *pfn_GetWindowTextW)(HWND hWnd, LPWSTR lpString,
                                        int nMaxCount);
typedef HWND(WINAPI *pfn_CreateWindowExW)(DWORD dwExStyle, LPCWSTR lpClassName,
                                          LPCWSTR lpWindowName, DWORD dwStyle,
                                          int X, int Y, int nWidth, int nHeight,
                                          HWND hWndParent, HMENU hMenu,
                                          HINSTANCE hInstance, LPVOID lpParam);
typedef BOOL(WINAPI *pfn_DestroyWindow)(HWND hWnd);
typedef ATOM(WINAPI *pfn_RegisterClassW)(const WNDCLASSW *lpWndClass);
typedef BOOL(WINAPI *pfn_UnregisterClassW)(LPCWSTR lpClassName,
                                           HINSTANCE hInstance);
typedef LRESULT(WINAPI *pfn_DefWindowProcW)(HWND hWnd, UINT Msg, WPARAM wParam,
                                            LPARAM lParam);
typedef BOOL(WINAPI *pfn_PeekMessageW)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                       UINT wMsgFilterMax, UINT wRemoveMsg);
typedef BOOL(WINAPI *pfn_TranslateMessage)(const MSG *lpMsg);
typedef LRESULT(WINAPI *pfn_DispatchMessageW)(const MSG *lpMsg);
typedef BOOL(WINAPI *pfn_ShowWindow)(HWND hWnd, int nCmdShow);
typedef HHOOK(WINAPI *pfn_SetWindowsHookExW)(int idHook, HOOKPROC lpfn,
                                             HINSTANCE hmod, DWORD dwThreadId);
typedef BOOL(WINAPI *pfn_UnhookWindowsHookEx)(HHOOK hhk);
typedef LRESULT(WINAPI *pfn_CallNextHookEx)(HHOOK hhk, int nCode,
                                            WPARAM wParam, LPARAM lParam);
typedef SHORT(WINAPI *pfn_VkKeyScanW)(WCHAR ch);
typedef UINT(WINAPI *pfn_MapVirtualKeyW)(UINT uCode, UINT uMapType);
typedef UINT(WINAPI *pfn_SendInput)(UINT cInputs, LPINPUT pInputs, int cbSize);
typedef BOOL(WINAPI *pfn_GetMessageW)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                      UINT wMsgFilterMax);
typedef BOOL(WINAPI *pfn_PostThreadMessageW)(DWORD idThread, UINT Msg,
                                             WPARAM wParam, LPARAM lParam);
typedef BOOL(WINAPI *pfn_RegisterRawInputDevices)(
    PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
typedef UINT(WINAPI *pfn_GetRawInputData)(HRAWINPUT hRawInput, UINT uiCommand,
                                           LPVOID pData, PUINT pcbSize,
                                           UINT cbSizeHeader);
typedef SHORT(WINAPI *pfn_GetAsyncKeyState)(int vKey);
typedef HKL(WINAPI *pfn_GetKeyboardLayout)(DWORD idThread);
typedef SHORT(WINAPI *pfn_GetKeyState)(int nVirtKey);
typedef int(WINAPI *pfn_ToUnicodeEx)(UINT wVirtKey, UINT wScanCode,
                                     const BYTE *lpKeyState, LPWSTR pwszBuff,
                                     int cchBuff, UINT wFlags, HKL dwhkl);
typedef ATOM(WINAPI *pfn_RegisterClassExW)(const WNDCLASSEXW *lpWndClassEx);
typedef DWORD(WINAPI *pfn_MsgWaitForMultipleObjects)(
    DWORD nCount, const HANDLE *pHandles, BOOL bWaitAll, DWORD dwMilliseconds,
    DWORD dwWakeMask);
typedef HDC(WINAPI *pfn_GetDC)(HWND hWnd);
typedef int(WINAPI *pfn_ReleaseDC)(HWND hWnd, HDC hDC);
typedef int(WINAPI *pfn_GetSystemMetrics)(int nIndex);
typedef BOOL(WINAPI *pfn_EnumDisplayMonitors)(HDC hdc, LPCRECT lprcClip,
                                              MONITORENUMPROC lpfnEnum,
                                              LPARAM dwData);
typedef BOOL(WINAPI *pfn_GetMonitorInfoA)(HMONITOR hMonitor,
                                         LPMONITORINFO lpmi);

/* GDI32 APIs */
typedef HDC(WINAPI *pfn_CreateCompatibleDC)(HDC hdc);
typedef HBITMAP(WINAPI *pfn_CreateCompatibleBitmap)(HDC hdc, int cx, int cy);
typedef HGDIOBJ(WINAPI *pfn_SelectObject)(HDC hdc, HGDIOBJ h);
typedef BOOL(WINAPI *pfn_BitBlt)(HDC hdc, int x, int y, int cx, int cy,
                                 HDC hdcSrc, int x1, int y1, DWORD rop);
typedef int(WINAPI *pfn_GetDIBits)(HDC hdc, HBITMAP hbm, UINT start, UINT cLines,
                                   LPVOID lpvBits, LPBITMAPINFO lpbmi,
                                   UINT usage);
typedef BOOL(WINAPI *pfn_DeleteObject)(HGDIOBJ ho);
typedef BOOL(WINAPI *pfn_DeleteDC)(HDC hdc);

/* Kernel32 APIs */
typedef BOOL(WINAPI *pfn_CreateProcessA)(
    LPCSTR lpApplicationName, LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
    DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
typedef HANDLE(WINAPI *pfn_OpenProcess)(DWORD dwDesiredAccess,
                                         BOOL bInheritHandle, DWORD dwProcessId);
typedef BOOL(WINAPI *pfn_TerminateProcess)(HANDLE hProcess, UINT uExitCode);
typedef BOOL(WINAPI *pfn_GetExitCodeProcess)(HANDLE hProcess,
                                              LPDWORD lpExitCode);
typedef BOOL(WINAPI *pfn_CloseHandle)(HANDLE hObject);
typedef HANDLE(WINAPI *pfn_CreateMutexA)(
    LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner,
    LPCSTR lpName);
typedef BOOL(WINAPI *pfn_ReleaseMutex)(HANDLE hMutex);
typedef HANDLE(WINAPI *pfn_OpenMutexA)(DWORD dwDesiredAccess,
                                       BOOL bInheritHandle, LPCSTR lpName);
typedef HANDLE(WINAPI *pfn_CreateEventA)(
    LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
    BOOL bInitialState, LPCSTR lpName);
typedef BOOL(WINAPI *pfn_SetEvent)(HANDLE hEvent);
typedef BOOL(WINAPI *pfn_ResetEvent)(HANDLE hEvent);
typedef HANDLE(WINAPI *pfn_OpenEventA)(DWORD dwDesiredAccess,
                                       BOOL bInheritHandle, LPCSTR lpName);
typedef DWORD(WINAPI *pfn_WaitForSingleObject)(HANDLE hHandle,
                                                DWORD dwMilliseconds);
typedef VOID(WINAPI *pfn_Sleep)(DWORD dwMilliseconds);
typedef ULONGLONG(WINAPI *pfn_GetTickCount64)(VOID);
typedef DWORD(WINAPI *pfn_GetCurrentProcessId)(VOID);
typedef BOOL(WINAPI *pfn_ProcessIdToSessionId)(DWORD dwProcessId,
                                                DWORD *pSessionId);
typedef BOOL(WINAPI *pfn_CreateDirectoryA)(
    LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
typedef HANDLE(WINAPI *pfn_CreateFileA)(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
typedef HANDLE(WINAPI *pfn_CreateFileW)(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
typedef DWORD(WINAPI *pfn_GetFileAttributesW)(LPCWSTR lpFileName);
typedef BOOL(WINAPI *pfn_ReadFile)(HANDLE hFile, LPVOID lpBuffer,
                                    DWORD nNumberOfBytesToRead,
                                    LPDWORD lpNumberOfBytesRead,
                                    LPOVERLAPPED lpOverlapped);
typedef BOOL(WINAPI *pfn_WriteFile)(HANDLE hFile, LPCVOID lpBuffer,
                                     DWORD nNumberOfBytesToWrite,
                                     LPDWORD lpNumberOfBytesWritten,
                                     LPOVERLAPPED lpOverlapped);
typedef BOOL(WINAPI *pfn_GetFileSizeEx)(HANDLE hFile,
                                         PLARGE_INTEGER lpFileSize);
typedef BOOL(WINAPI *pfn_DeleteFileA)(LPCSTR lpFileName);
typedef BOOL(WINAPI *pfn_MoveFileExA)(LPCSTR lpExistingFileName,
                                       LPCSTR lpNewFileName, DWORD dwFlags);
typedef HWND(WINAPI *pfn_GetConsoleWindow)(VOID);
typedef BOOL(WINAPI *pfn_FreeConsole)(VOID);
typedef BOOL(WINAPI *pfn_SetConsoleCtrlHandler)(PHANDLER_ROUTINE HandlerRoutine,
                                                BOOL Add);
typedef BOOL(WINAPI *pfn_SetConsoleTitleA)(LPCSTR lpConsoleTitle);
typedef int(WINAPI *pfn_WideCharToMultiByte)(
    UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar,
    LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar,
    LPBOOL lpUsedDefaultChar);
typedef int(WINAPI *pfn_MultiByteToWideChar)(UINT CodePage, DWORD dwFlags,
                                              LPCCH lpMultiByteStr,
                                              int cbMultiByte,
                                              LPWSTR lpWideCharStr,
                                              int cchWideChar);
typedef BOOL(WINAPI *pfn_QueryFullProcessImageNameA)(HANDLE hProcess,
                                                     DWORD dwFlags,
                                                     LPSTR lpExeName,
                                                     PDWORD pdwSize);
typedef BOOL(WINAPI *pfn_QueryFullProcessImageNameW)(HANDLE hProcess,
                                                     DWORD dwFlags,
                                                     LPWSTR lpExeName,
                                                     PDWORD pdwSize);
typedef LPVOID(WINAPI *pfn_GlobalLock)(HGLOBAL hMem);
typedef BOOL(WINAPI *pfn_GlobalUnlock)(HGLOBAL hMem);
typedef SIZE_T(WINAPI *pfn_GlobalSize)(HGLOBAL hMem);
typedef HGLOBAL(WINAPI *pfn_GlobalAlloc)(UINT uFlags, SIZE_T dwBytes);
typedef HGLOBAL(WINAPI *pfn_GlobalFree)(HGLOBAL hMem);
typedef DWORD(WINAPI *pfn_GetModuleFileNameA)(HMODULE hModule, LPSTR lpFilename,
                                               DWORD nSize);
typedef DWORD(WINAPI *pfn_GetModuleFileNameW)(HMODULE hModule,
                                               LPWSTR lpFilename, DWORD nSize);
typedef HMODULE(WINAPI *pfn_GetModuleHandleA)(LPCSTR lpModuleName);
typedef HMODULE(WINAPI *pfn_GetModuleHandleW)(LPCWSTR lpModuleName);
typedef HMODULE(WINAPI *pfn_LoadLibraryA)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI *pfn_LoadLibraryW)(LPCWSTR lpLibFileName);
typedef FARPROC(WINAPI *pfn_GetProcAddress)(HMODULE hModule, LPCSTR lpProcName);
typedef VOID(WINAPI *pfn_GetSystemTime)(LPSYSTEMTIME lpSystemTime);
typedef HANDLE(WINAPI *pfn_GetCurrentProcess)(VOID);
typedef BOOL(WINAPI *pfn_SetProcessWorkingSetSize)(
    HANDLE hProcess, SIZE_T dwMinimumWorkingSetSize,
    SIZE_T dwMaximumWorkingSetSize);
typedef BOOL(WINAPI *pfn_GetProcessWorkingSetSize)(
    HANDLE hProcess, PSIZE_T lpMinimumWorkingSetSize,
    PSIZE_T lpMaximumWorkingSetSize);
typedef BOOL(WINAPI *pfn_VirtualLock)(LPVOID lpAddress, SIZE_T dwSize);
typedef BOOL(WINAPI *pfn_VirtualUnlock)(LPVOID lpAddress, SIZE_T dwSize);
typedef VOID(WINAPI *pfn_InitializeCriticalSection)(
    LPCRITICAL_SECTION lpCriticalSection);
typedef VOID(WINAPI *pfn_EnterCriticalSection)(
    LPCRITICAL_SECTION lpCriticalSection);
typedef VOID(WINAPI *pfn_LeaveCriticalSection)(
    LPCRITICAL_SECTION lpCriticalSection);
typedef VOID(WINAPI *pfn_DeleteCriticalSection)(
    LPCRITICAL_SECTION lpCriticalSection);
typedef HANDLE(WINAPI *pfn_CreateThread)(
    LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
    DWORD dwCreationFlags, LPDWORD lpThreadId);
typedef DWORD(WINAPI *pfn_GetCurrentThreadId)(VOID);
typedef int(WINAPI *pfn_GetLocaleInfoA)(LCID Locale, LCTYPE LCType,
                                         LPSTR lpLCData, int cchData);
typedef DWORD(WINAPI *pfn_GetLastError)(VOID);
typedef HRESULT(WINAPI *pfn_WerSetFlags)(DWORD dwFlags);
typedef BOOL(WINAPI *pfn_SetDefaultDllDirectories)(DWORD DirectoryFlags);
typedef BOOL(WINAPI *pfn_CreateDirectoryW)(
    LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
typedef HANDLE(WINAPI *pfn_FindFirstFileW)(
    LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
typedef BOOL(WINAPI *pfn_FindNextFileW)(
    HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
typedef BOOL(WINAPI *pfn_FindClose)(HANDLE hFindFile);
typedef BOOL(WINAPI *pfn_FreeLibrary)(HMODULE hLibModule);
typedef BOOL(WINAPI *pfn_HeapSetInformation)(
    HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass,
    PVOID HeapInformation, SIZE_T HeapInformationLength);
typedef VOID(WINAPI *pfn_InitializeConditionVariable)(
    PCONDITION_VARIABLE ConditionVariable);
typedef BOOL(WINAPI *pfn_SleepConditionVariableCS)(
    PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection,
    DWORD dwMilliseconds);
typedef VOID(WINAPI *pfn_WakeConditionVariable)(
    PCONDITION_VARIABLE ConditionVariable);


/* Advapi32 & BCrypt APIs */
typedef BOOLEAN(WINAPI *pfn_RtlGenRandom)(PVOID RandomBuffer,
                                           ULONG RandomBufferLength);
typedef NTSTATUS(WINAPI *pfn_BCryptOpenAlgorithmProvider)(
    BCRYPT_ALG_HANDLE *phAlgorithm, LPCWSTR pszAlgId,
    LPCWSTR pszImplementation, ULONG dwFlags);
typedef NTSTATUS(WINAPI *pfn_BCryptGenRandom)(BCRYPT_ALG_HANDLE hAlgorithm,
                                               PUCHAR pbBuffer, ULONG cbBuffer,
                                               ULONG dwFlags);
typedef NTSTATUS(WINAPI *pfn_BCryptCloseAlgorithmProvider)(
    BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags);

/* Shell32 APIs */
typedef HRESULT(WINAPI *pfn_SHGetFolderPathW)(HWND hwnd, int csidl,
                                               HANDLE hToken, DWORD dwFlags,
                                               LPWSTR pszPath);
typedef UINT(WINAPI *pfn_DragQueryFileW)(HDROP hDrop, UINT iFile,
                                         LPWSTR lpszFile, UINT cch);

/* WinHTTP APIs */
typedef HINTERNET(WINAPI *pfn_WinHttpOpen)(LPCWSTR pszAgent, DWORD dwAccessType,
                                           LPCWSTR pszProxy,
                                           LPCWSTR pszProxyBypass,
                                           DWORD dwFlags);
typedef HINTERNET(WINAPI *pfn_WinHttpConnect)(HINTERNET hSession,
                                              LPCWSTR pswzServerName,
                                              INTERNET_PORT nServerPort,
                                              DWORD dwReserved);
typedef HINTERNET(WINAPI *pfn_WinHttpOpenRequest)(
    HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName,
    LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR *ppwszAcceptTypes,
    DWORD dwFlags);
typedef BOOL(WINAPI *pfn_WinHttpSendRequest)(
    HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength,
    LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength,
    DWORD_PTR dwContext);
typedef BOOL(WINAPI *pfn_WinHttpReceiveResponse)(HINTERNET hRequest,
                                                 LPVOID lpReserved);
typedef BOOL(WINAPI *pfn_WinHttpQueryDataAvailable)(
    HINTERNET hRequest, LPDWORD lpdwNumberOfBytesAvailable);
typedef BOOL(WINAPI *pfn_WinHttpReadData)(HINTERNET hRequest, LPVOID lpBuffer,
                                           DWORD dwNumberOfBytesToRead,
                                           LPDWORD lpdwNumberOfBytesRead);
typedef BOOL(WINAPI *pfn_WinHttpWriteData)(HINTERNET hRequest,
                                            LPCVOID lpBuffer,
                                            DWORD dwNumberOfBytesToWrite,
                                            LPDWORD lpdwNumberOfBytesWritten);
typedef BOOL(WINAPI *pfn_WinHttpQueryHeaders)(HINTERNET hRequest,
                                               DWORD dwInfoLevel,
                                               LPCWSTR pwszName,
                                               LPVOID lpBuffer,
                                               LPDWORD lpdwBufferLength,
                                               LPDWORD lpdwIndex);
typedef BOOL(WINAPI *pfn_WinHttpCloseHandle)(HINTERNET hInternet);
typedef BOOL(WINAPI *pfn_WinHttpSetTimeouts)(HINTERNET hInternet,
                                             int nResolveTimeout,
                                             int nConnectTimeout,
                                             int nSendTimeout,
                                             int nReceiveTimeout);
typedef BOOL(WINAPI *pfn_WinHttpSetOption)(HINTERNET hInternet, DWORD dwOption,
                                           LPVOID lpBuffer,
                                           DWORD dwBufferLength);

/* GDI+ and OLE32 APIs for hardware-accelerated image encoding */
typedef enum {
  Ok = 0,
  GenericError = 1,
  InvalidParameter = 2,
  OutOfMemory = 3,
  ObjectBusy = 4,
  InsufficientBuffer = 5,
  NotImplemented = 6,
  Win32Error = 7,
  WrongState = 8,
  Aborted = 9,
  FileNotFound = 10,
  ValueOverflow = 11,
  AccessDenied = 12,
  UnknownImageFormat = 13,
  PropertyNotFound = 14,
  PropertyNotSupported = 15,
  ProfileNotFound = 16
} GpStatus;

typedef struct {
  UINT32 GdiplusVersion;
  void *DebugEventCallback;
  BOOL SuppressBackgroundThread;
  BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
  GUID Guid;
  ULONG NumberOfValues;
  ULONG Type;
  VOID *Value;
} EncoderParameter;

typedef struct {
  UINT Count;
  EncoderParameter Parameter[1];
} EncoderParameters;

typedef void GpImage;
typedef void GpBitmap;

typedef GpStatus(WINAPI *pfn_GdiplusStartup)(ULONG_PTR *token,
                                              const GdiplusStartupInput *input,
                                              void *output);
typedef VOID(WINAPI *pfn_GdiplusShutdown)(ULONG_PTR token);
typedef GpStatus(WINAPI *pfn_GdipCreateBitmapFromGdiDib)(
    const BITMAPINFO *gdiBitmapInfo, VOID *gdiBitmapData, GpBitmap **bitmap);
typedef GpStatus(WINAPI *pfn_GdipSaveImageToStream)(
    GpImage *image, void *stream, const GUID *clsidEncoder,
    const EncoderParameters *encoderParams);
typedef GpStatus(WINAPI *pfn_GdipDisposeImage)(GpImage *image);

typedef HRESULT(WINAPI *pfn_CreateStreamOnHGlobal)(HGLOBAL hGlobal,
                                                    BOOL fDeleteOnRelease,
                                                    void **ppstm);
typedef HRESULT(WINAPI *pfn_GetHGlobalFromStream)(void *pstm,
                                                  HGLOBAL *phglobal);

typedef struct {
  /* user32 */
  pfn_GetWindowThreadProcessId GetWindowThreadProcessId;
  pfn_OpenClipboard OpenClipboard;
  pfn_CloseClipboard CloseClipboard;
  pfn_GetClipboardData GetClipboardData;
  pfn_IsClipboardFormatAvailable IsClipboardFormatAvailable;
  pfn_EmptyClipboard EmptyClipboard;
  pfn_SetClipboardData SetClipboardData;
  pfn_AddClipboardFormatListener AddClipboardFormatListener;
  pfn_RemoveClipboardFormatListener RemoveClipboardFormatListener;
  pfn_GetForegroundWindow GetForegroundWindow;
  pfn_GetWindowTextW GetWindowTextW;
  pfn_CreateWindowExW CreateWindowExW;
  pfn_DestroyWindow DestroyWindow;
  pfn_RegisterClassW RegisterClassW;
  pfn_UnregisterClassW UnregisterClassW;
  pfn_DefWindowProcW DefWindowProcW;
  pfn_PeekMessageW PeekMessageW;
  pfn_TranslateMessage TranslateMessage;
  pfn_DispatchMessageW DispatchMessageW;
  pfn_ShowWindow ShowWindow;
  pfn_SetWindowsHookExW SetWindowsHookExW;
  pfn_UnhookWindowsHookEx UnhookWindowsHookEx;
  pfn_CallNextHookEx CallNextHookEx;
  pfn_VkKeyScanW VkKeyScanW;
  pfn_MapVirtualKeyW MapVirtualKeyW;
  pfn_SendInput SendInput;
  pfn_GetMessageW GetMessageW;
  pfn_PostThreadMessageW PostThreadMessageW;
  pfn_RegisterRawInputDevices RegisterRawInputDevices;
  pfn_GetRawInputData GetRawInputData;
  pfn_GetAsyncKeyState GetAsyncKeyState;
  pfn_GetKeyboardLayout GetKeyboardLayout;
  pfn_GetKeyState GetKeyState;
  pfn_ToUnicodeEx ToUnicodeEx;
  pfn_RegisterClassExW RegisterClassExW;
  pfn_MsgWaitForMultipleObjects MsgWaitForMultipleObjects;
  pfn_GetDC GetDC;
  pfn_ReleaseDC ReleaseDC;
  pfn_GetSystemMetrics GetSystemMetrics;
  pfn_EnumDisplayMonitors EnumDisplayMonitors;
  pfn_GetMonitorInfoA GetMonitorInfoA;

  /* gdi32 */
  pfn_CreateCompatibleDC CreateCompatibleDC;
  pfn_CreateCompatibleBitmap CreateCompatibleBitmap;
  pfn_SelectObject SelectObject;
  pfn_BitBlt BitBlt;
  pfn_GetDIBits GetDIBits;
  pfn_DeleteObject DeleteObject;
  pfn_DeleteDC DeleteDC;

  /* kernel32 */
  pfn_CreateProcessA CreateProcessA;
  pfn_OpenProcess OpenProcess;
  pfn_TerminateProcess TerminateProcess;
  pfn_GetExitCodeProcess GetExitCodeProcess;
  pfn_CloseHandle CloseHandle;
  pfn_CreateMutexA CreateMutexA;
  pfn_ReleaseMutex ReleaseMutex;
  pfn_OpenMutexA OpenMutexA;
  pfn_CreateEventA CreateEventA;
  pfn_SetEvent SetEvent;
  pfn_ResetEvent ResetEvent;
  pfn_OpenEventA OpenEventA;
  pfn_WaitForSingleObject WaitForSingleObject;
  pfn_Sleep Sleep;
  pfn_GetTickCount64 GetTickCount64;
  pfn_GetCurrentProcessId GetCurrentProcessId;
  pfn_ProcessIdToSessionId ProcessIdToSessionId;
  pfn_CreateDirectoryA CreateDirectoryA;
  pfn_CreateFileA CreateFileA;
  pfn_CreateFileW CreateFileW;
  pfn_GetFileAttributesW GetFileAttributesW;
  pfn_ReadFile ReadFile;
  pfn_WriteFile WriteFile;
  pfn_GetFileSizeEx GetFileSizeEx;
  pfn_DeleteFileA DeleteFileA;
  pfn_MoveFileExA MoveFileExA;
  pfn_GetConsoleWindow GetConsoleWindow;
  pfn_FreeConsole FreeConsole;
  pfn_SetConsoleCtrlHandler SetConsoleCtrlHandler;
  pfn_SetConsoleTitleA SetConsoleTitleA;
  pfn_WideCharToMultiByte WideCharToMultiByte;
  pfn_MultiByteToWideChar MultiByteToWideChar;
  pfn_QueryFullProcessImageNameA QueryFullProcessImageNameA;
  pfn_QueryFullProcessImageNameW QueryFullProcessImageNameW;
  pfn_GlobalLock GlobalLock;
  pfn_GlobalUnlock GlobalUnlock;
  pfn_GlobalSize GlobalSize;
  pfn_GlobalAlloc GlobalAlloc;
  pfn_GlobalFree GlobalFree;
  pfn_GetModuleFileNameA GetModuleFileNameA;
  pfn_GetModuleFileNameW GetModuleFileNameW;
  pfn_GetModuleHandleA GetModuleHandleA;
  pfn_GetModuleHandleW GetModuleHandleW;
  pfn_LoadLibraryA LoadLibraryA;
  pfn_LoadLibraryW LoadLibraryW;
  pfn_GetProcAddress GetProcAddress;
  pfn_GetSystemTime GetSystemTime;
  pfn_GetCurrentProcess GetCurrentProcess;
  pfn_SetProcessWorkingSetSize SetProcessWorkingSetSize;
  pfn_GetProcessWorkingSetSize GetProcessWorkingSetSize;
  pfn_VirtualLock VirtualLock;
  pfn_VirtualUnlock VirtualUnlock;
  pfn_InitializeCriticalSection InitializeCriticalSection;
  pfn_EnterCriticalSection EnterCriticalSection;
  pfn_LeaveCriticalSection LeaveCriticalSection;
  pfn_DeleteCriticalSection DeleteCriticalSection;
  pfn_CreateThread CreateThread;
  pfn_GetCurrentThreadId GetCurrentThreadId;
  pfn_GetLocaleInfoA GetLocaleInfoA;
  pfn_GetLastError GetLastError;
  pfn_CreateDirectoryW CreateDirectoryW;
  pfn_FindFirstFileW FindFirstFileW;
  pfn_FindNextFileW FindNextFileW;
  pfn_FindClose FindClose;
  pfn_FreeLibrary FreeLibrary;
  pfn_HeapSetInformation HeapSetInformation;
  pfn_InitializeConditionVariable InitializeConditionVariable;
  pfn_SleepConditionVariableCS SleepConditionVariableCS;
  pfn_WakeConditionVariable WakeConditionVariable;


  /* advapi32 / bcrypt */
  pfn_RtlGenRandom RtlGenRandom;
  pfn_BCryptOpenAlgorithmProvider BCryptOpenAlgorithmProvider;
  pfn_BCryptGenRandom BCryptGenRandom;
  pfn_BCryptCloseAlgorithmProvider BCryptCloseAlgorithmProvider;

  /* shell32 */
  pfn_SHGetFolderPathW SHGetFolderPathW;
  pfn_DragQueryFileW DragQueryFileW;

  /* winhttp */
  pfn_WinHttpOpen WinHttpOpen;
  pfn_WinHttpConnect WinHttpConnect;
  pfn_WinHttpOpenRequest WinHttpOpenRequest;
  pfn_WinHttpSendRequest WinHttpSendRequest;
  pfn_WinHttpReceiveResponse WinHttpReceiveResponse;
  pfn_WinHttpQueryDataAvailable WinHttpQueryDataAvailable;
  pfn_WinHttpReadData WinHttpReadData;
  pfn_WinHttpWriteData WinHttpWriteData;
  pfn_WinHttpQueryHeaders WinHttpQueryHeaders;
  pfn_WinHttpCloseHandle WinHttpCloseHandle;
  pfn_WinHttpSetTimeouts WinHttpSetTimeouts;
  pfn_WinHttpSetOption WinHttpSetOption;

  /* iphlpapi */
  pfn_GetAdaptersAddresses GetAdaptersAddresses;

  /* wer */
  pfn_WerSetFlags WerSetFlags;
  pfn_SetDefaultDllDirectories SetDefaultDllDirectories;

  /* gdiplus & ole32 hardware-accelerated imaging */
  pfn_GdiplusStartup GdiplusStartup;
  pfn_GdiplusShutdown GdiplusShutdown;
  pfn_GdipCreateBitmapFromGdiDib GdipCreateBitmapFromGdiDib;
  pfn_GdipSaveImageToStream GdipSaveImageToStream;
  pfn_GdipDisposeImage GdipDisposeImage;
  pfn_CreateStreamOnHGlobal CreateStreamOnHGlobal;
  pfn_GetHGlobalFromStream GetHGlobalFromStream;
} c2t_win32_api_t;

extern c2t_win32_api_t g_c2t_win32;

void c2t_win32_api_init(void);
HMODULE c2t_win32_get_module_peb(const wchar_t *module_name);
void c2t_win32_xor_decode(char *dest, const unsigned char *src, size_t len,
                          unsigned char key);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* C2T_WIN32_API_H */
