/* Minimal stand-in for <windows.h>, enough to compile and run shro_menu_probe.c on Linux. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef uint8_t BYTE; typedef uint16_t WORD; typedef uint32_t DWORD; typedef int32_t INT32; typedef int64_t INT64;
typedef int BOOL; typedef void *HANDLE; typedef void *HINSTANCE; typedef void *LPVOID; typedef void *HMODULE;
#define TRUE 1
#define WINAPI
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define DLL_PROCESS_ATTACH 1
#define _TRUNCATE ((size_t)-1)
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef struct { WORD e_magic; BYTE pad[58]; int32_t e_lfanew; } IMAGE_DOS_HEADER;
typedef struct { WORD Machine, NumberOfSections; DWORD TimeDateStamp, PointerToSymbolTable, NumberOfSymbols; WORD SizeOfOptionalHeader, Characteristics; } IMAGE_FILE_HEADER;
typedef struct { BYTE raw[240]; } IMAGE_OPTIONAL_HEADER64;
typedef struct { DWORD Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER64 OptionalHeader; } IMAGE_NT_HEADERS64;
typedef struct { BYTE Name[8]; union { DWORD VirtualSize; } Misc; DWORD VirtualAddress, SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers; WORD NumberOfRelocations, NumberOfLinenumbers; DWORD Characteristics; } IMAGE_SECTION_HEADER;
#define IMAGE_FIRST_SECTION(nt) ((IMAGE_SECTION_HEADER *)((BYTE *)&(nt)->OptionalHeader + (nt)->FileHeader.SizeOfOptionalHeader))
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
void GetLocalTime(SYSTEMTIME *t);
BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o);
BOOL FlushFileBuffers(HANDLE h);
DWORD GetModuleFileNameW(HMODULE m, wchar_t *p, DWORD n);
HANDLE CreateFileW(const wchar_t *p, DWORD a, DWORD s, void *sa, DWORD c, DWORD f, HANDLE t);
HMODULE GetModuleHandleW(const wchar_t *n);
HANDLE CreateThread(void *a, size_t s, LPTHREAD_START_ROUTINE f, LPVOID p, DWORD fl, DWORD *id);
BOOL CloseHandle(HANDLE h);
void Sleep(DWORD ms);
BOOL DisableThreadLibraryCalls(HINSTANCE h);
#define _snprintf_s(b, n, t, ...) snprintf(b, n, __VA_ARGS__)
#define _vsnprintf_s(b, n, t, f, a) vsnprintf(b, n, f, a)
#define strcat_s(d, n, s) strncat(d, s, (n) - strlen(d) - 1)
#define wcscat_s(d, n, s) wcsncat(d, s, (n) - wcslen(d) - 1)
#define sscanf_s sscanf
/* additions for step 2 */
typedef uint64_t UINT64; typedef uintptr_t UINT_PTR; typedef size_t SIZE_T; typedef const void *LPCVOID;
typedef struct { void *lpMinimumApplicationAddress; DWORD dwAllocationGranularity; } SYSTEM_INFO;
typedef struct { void *BaseAddress; DWORD State; } MEMORY_BASIC_INFORMATION;
#define MEM_FREE 0x10000
#define MEM_RESERVE 0x2000
#define MEM_COMMIT 0x1000
#define PAGE_EXECUTE_READWRITE 0x40
void GetSystemInfo(SYSTEM_INFO *si);
SIZE_T VirtualQuery(LPCVOID a, MEMORY_BASIC_INFORMATION *m, SIZE_T n);
void *VirtualAlloc(void *a, SIZE_T s, DWORD t, DWORD p);
BOOL VirtualProtect(void *a, SIZE_T s, DWORD p, DWORD *old);
BOOL FlushInstructionCache(HANDLE h, LPCVOID a, SIZE_T s);
HANDLE GetCurrentProcess(void);
typedef int16_t INT16;
/* additions for the settings file (v1.6) */
#define GENERIC_READ 0x80000000
#define OPEN_EXISTING 3
#define CREATE_NEW 1
BOOL ReadFile(HANDLE h, void *b, DWORD n, DWORD *got, void *o);
#define strcpy_s(d, n, s) (strncpy(d, s, (n) - 1), (d)[(n) - 1] = 0)
#define strncpy_s(d, n, s, t) (strncpy(d, s, (n) - 1), (d)[(n) - 1] = 0)
