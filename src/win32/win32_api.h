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
#define _WIN32_WINNT 0x0600
#include <windows.h>
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
