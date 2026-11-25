/* Runs the real shro_menu_probe.c against a fake game image built from the CE dumps. */
#include <stdlib.h>
#include "windows.h"
#include "dump_bytes.h"
static BYTE *img;
static int mutate = 0;   /* 1 = corrupt OpenMenu, 2 = apply Lua patch bytes */
void GetLocalTime(SYSTEMTIME *t) { memset(t, 0, sizeof(*t)); }
BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o) { (void)h; (void)o; fwrite(b, 1, n, stdout); *w = n; return 1; }
BOOL FlushFileBuffers(HANDLE h) { (void)h; return 1; }
DWORD GetModuleFileNameW(HMODULE m, wchar_t *p, DWORD n) { (void)m; wcsncpy(p, L"C:\\Game\\KH2.exe", n); return (DWORD)wcslen(p); }
HANDLE CreateFileW(const wchar_t *p, DWORD a, DWORD s, void *sa, DWORD c, DWORD f, HANDLE t) { (void)p;(void)a;(void)s;(void)sa;(void)c;(void)f;(void)t; return (HANDLE)1; }
HMODULE GetModuleHandleW(const wchar_t *n) { (void)n; return img; }
HANDLE CreateThread(void *a, size_t s, LPTHREAD_START_ROUTINE f, LPVOID p, DWORD fl, DWORD *id) { (void)a;(void)s;(void)p;(void)fl;(void)id; f(NULL); return (HANDLE)1; }
BOOL CloseHandle(HANDLE h) { (void)h; return 1; }
void Sleep(DWORD ms) { (void)ms; }
BOOL DisableThreadLibraryCalls(HINSTANCE h) { (void)h; return 1; }
#include "../src/shro_menu_probe.c"
int main(int argc, char **argv) {
    const DWORD size = 0x800000;
    IMAGE_DOS_HEADER *dos; IMAGE_NT_HEADERS64 *nt; IMAGE_SECTION_HEADER *sec;
    size_t i;
    if (argc > 1) mutate = atoi(argv[1]);
    img = calloc(1, size);
    memset(img + 0x1000, 0xCC, size - 0x1000);
    dos = (IMAGE_DOS_HEADER *)img; dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    nt = (IMAGE_NT_HEADERS64 *)(img + 0x80); nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    sec = IMAGE_FIRST_SECTION(nt); sec->VirtualAddress = 0x1000; sec->Misc.VirtualSize = size - 0x1000;
    sec->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    for (i = 0; i < sizeof(dump_bytes) / sizeof(dump_bytes[0]); i++) img[dump_bytes[i][0]] = (BYTE)dump_bytes[i][1];
    memcpy(img + 0x585E59, "epicuser", 8);
    if (mutate == 1) img[0x151770] ^= 0xFF;
    if (mutate == 2) { img[0x157307] = 0x90; img[0x157308] = 0x90; }
    if (mutate == 3) memcpy(img + 0x400000, img + 0x151770, 0x29);   /* duplicate OpenMenu -> ambiguous */
    DllMain(NULL, DLL_PROCESS_ATTACH, NULL);
    return 0;
}
