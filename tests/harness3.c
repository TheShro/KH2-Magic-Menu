/* Step 3 test: installs all three hooks into a fake game image (CE-dump bytes) and runs the
   shipped caves and the game's real OpenMenu bytes natively, with the Windows x64 ABI. */
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "windows.h"
#include "dump_bytes.h"
#define IMG_SIZE 0x3000000
static BYTE *img;
void GetLocalTime(SYSTEMTIME *t) { memset(t, 0, sizeof(*t)); }
static const char *g_test_ini = NULL; static size_t g_test_ini_pos = 0; static char g_ini_written[20000]; static int g_ini_writes = 0;
BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o) { if (h == (HANDLE)3) { strncat(g_ini_written, (const char *)b, n); g_ini_writes++; *w = n; return 1; } (void)h; (void)o; fputs("    log: ", stdout); fwrite((const char *)b + 15, 1, n - 15, stdout); *w = n; return 1; }
BOOL FlushFileBuffers(HANDLE h) { (void)h; return 1; }
DWORD GetModuleFileNameW(HMODULE m, wchar_t *p, DWORD n) { (void)m; wcsncpy(p, L"C:\\Game\\KH2.exe", n); return (DWORD)wcslen(p); }
/* settings file: g_test_ini = contents (NULL = no file); a default written by the module lands in g_ini_written */
HANDLE CreateFileW(const wchar_t *p, DWORD a, DWORD s, void *sa, DWORD c, DWORD f, HANDLE t) { (void)s;(void)sa;(void)f;(void)t;
    if (wcsstr(p, L".ini")) { if (a == GENERIC_READ) { if (!g_test_ini) return INVALID_HANDLE_VALUE; g_test_ini_pos = 0; return (HANDLE)2; } (void)c; g_ini_written[0] = 0; return (HANDLE)3; }
    return (HANDLE)1; }
BOOL ReadFile(HANDLE h, void *b, DWORD n, DWORD *got, void *o) { size_t l = strlen(g_test_ini) - g_test_ini_pos; (void)h; (void)o; if (l > n) l = n; memcpy(b, g_test_ini + g_test_ini_pos, l); g_test_ini_pos += l; *got = (DWORD)l; return 1; }

HMODULE GetModuleHandleW(const wchar_t *n) { (void)n; return img; }
HANDLE CreateThread(void *a, size_t s, LPTHREAD_START_ROUTINE f, LPVOID p, DWORD fl, DWORD *id) { (void)a;(void)s;(void)p;(void)fl;(void)id; f(NULL); return (HANDLE)1; }
BOOL CloseHandle(HANDLE h) { (void)h; return 1; }
void Sleep(DWORD ms) { (void)ms; }
BOOL DisableThreadLibraryCalls(HINSTANCE h) { (void)h; return 1; }
void GetSystemInfo(SYSTEM_INFO *si) { si->lpMinimumApplicationAddress = (void *)0x10000; si->dwAllocationGranularity = 0x10000; }
SIZE_T VirtualQuery(LPCVOID a, MEMORY_BASIC_INFORMATION *m, SIZE_T n) { (void)a; (void)m; (void)n; return 0; }
void *VirtualAlloc(void *a, SIZE_T s, DWORD t, DWORD p) { (void)a;(void)s;(void)t;(void)p; return NULL; }
BOOL VirtualProtect(void *a, SIZE_T s, DWORD p, DWORD *old) { (void)a;(void)s;(void)p; *old = 0; return 1; }
BOOL FlushInstructionCache(HANDLE h, LPCVOID a, SIZE_T s) { (void)h;(void)a;(void)s; return 1; }
HANDLE GetCurrentProcess(void) { return (HANDLE)1; }
#define SMM_TEST_ALLOC
#define SMM_GAME_ABI __attribute__((ms_abi))
static BYTE *smm_alloc_near(BYTE *target, SIZE_T size) { (void)target; (void)size; return img + 0xF00000; }
#include "../src/shro_magic_menu.c"

static int fails = 0;
static void check(int cond, const char *msg) { printf("%s %s\n", cond ? "PASS" : "FAIL", msg); if (!cond) fails++; }
static DWORD r32(DWORD off) { DWORD v; memcpy(&v, img + off, 4); return v; }
static void w32(DWORD off, DWORD v) { memcpy(img + off, &v, 4); }
static void w16(DWORD off, WORD v) { memcpy(img + off, &v, 2); }
static void put(DWORD off, const BYTE *b, int n) { memcpy(img + off, b, n); }

#define S 0x1572F9
/* thunk: mov rax, obj ; jmp special site */
static UINT64 press_with_obj(UINT64 obj) {
    BYTE t[15]; INT32 rel;
    t[0] = 0x48; t[1] = 0xB8; memcpy(t + 2, &obj, 8); t[10] = 0xE9;
    rel = (INT32)((S + SMM_SPECIAL_OFF) - (0xE10000 + 15)); memcpy(t + 11, &rel, 4);
    put(0xE10000, t, 15);
    return ((UINT64 (*)(void))(img + 0xE10000))();
}
static void reset_state(void) {
    img[SMM_BTLTYP] = 0; img[SMM_SHOULDERS] = 0; img[SMM_WORLD] = 0x04;
    w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0); img[SMM_PAUSE_BLOCK - 2] = 0; w16(SMM_PAUSE_BLOCK, 3); w16(SMM_PAUSE_BLOCK + 2, 6); w16(SMM_PAUSE_BLOCK + 4, 0x8AB4);
}

static void build_image(int mutation) {
    IMAGE_DOS_HEADER *dos; IMAGE_NT_HEADERS64 *nt; IMAGE_SECTION_HEADER *sec; size_t i;
    img = mmap(NULL, IMG_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(img + 0x1000, 0xCC, 0x7FF000);
    dos = (IMAGE_DOS_HEADER *)img; dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    nt = (IMAGE_NT_HEADERS64 *)(img + 0x80); nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    sec = IMAGE_FIRST_SECTION(nt); sec->VirtualAddress = 0x1000; sec->Misc.VirtualSize = 0x7FF000;
    sec->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    for (i = 0; i < sizeof(dump_bytes) / sizeof(dump_bytes[0]); i++) img[dump_bytes[i][0]] = (BYTE)dump_bytes[i][1];
    memcpy(img + 0x585E59, mutation == 3 ? "KH2Jxxxx" : "epicuser", 8);
    { BYTE stub[7] = { 0x89, 0x0D, 0, 0, 0, 0, 0xC3 }; INT32 d = (INT32)(0xE00000 - (0x151480 + 6)); memcpy(stub + 2, &d, 4); put(0x151480, stub, 7); }
    if (mutation == 1) { img[0x157307] = 0x90; img[0x157308] = 0x90; }   /* Lua patch already applied */
    if (mutation == 2) { img[0x15732A] = 0x31; }                          /* loop site not as expected */
    reset_state();
}

int main(int argc, char **argv) {
    int mutation = argc > 1 ? atoi(argv[1]) : 0;
    UINT64 X = 0x00007FF600C0FFEEULL;
    build_image(mutation);
    DllMain(NULL, DLL_PROCESS_ATTACH, NULL);
    printf("----\n");
    if (mutation == 2) {
        check(img[0x157304] == 0x48 && img[0x157312] == 0xBA && img[0x15732A] == 0x31,
              "mutation: one site wrong -> no hook installed anywhere, all sites untouched");
        return fails;
    }
    if (mutation == 3) {
        check(img[0x157304] == 0x48 && img[0x157312] == 0xBA, "mutation: not Steam Global -> no hooks");
        return fails;
    }
    check(img[0x157304] == 0xE9 && img[0x157312] == 0xE9 && img[0x15732A] == 0xE9, "all three sites now jump to caves");

    /* stubs after the sites, so the paths can be called safely */
    { BYTE b[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 }; put(S + SMM_NORMAL_OFF, b, 6); }   /* normal path -> 1 */
    { BYTE b[1] = { 0xC3 }; put(S + SMM_MISSION_OFF, b, 1); }                               /* special path -> rax */
    { BYTE b[3] = { 0x89, 0xD0, 0xC3 }; put(S + SMM_START_BACK, b, 3); }                     /* start back -> edx */
    { BYTE b[6] = { 0xB8, 0x34, 0x12, 0x00, 0x00, 0xC3 }; put(S + SMM_LOOP_BACK, b, 6); }    /* loop back -> 0x1234 */

    printf("-- special-pause decision\n");
    reset_state();                                   check(press_with_obj(0) == 1, "no special pause -> normal path");
    reset_state();                                   check(press_with_obj(X) == X, "special pause, out of battle -> kept (rax intact)");
    reset_state(); img[SMM_BTLTYP] = 2;              check(press_with_obj(X) == 1, "forced battle -> normal path (party menu)");
    reset_state(); img[SMM_BTLTYP] = 1;              check(press_with_obj(X) == 1, "regular battle -> normal path");
    reset_state(); img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0x02;
                                                     check(press_with_obj(X) == 1, "forced battle + R2 held -> normal path (custom pause)");
    reset_state(); img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0x03;
                                                     check(press_with_obj(X) == 1, "forced battle + L2+R2 -> normal");
    reset_state(); img[SMM_BTLTYP] = 2; w32(SMM_CUT_FLAGS, 5);
                                                     check(press_with_obj(X) == X, "battle + cutscene bit -> kept");
    reset_state(); img[SMM_BTLTYP] = 2; w16(SMM_CUT_PAUSE, 4);
                                                     check(press_with_obj(X) == X, "battle + cutscene pause block -> kept");
    reset_state(); img[SMM_BTLTYP] = 2; img[SMM_WORLD] = 0x0F;
                                                     check(press_with_obj(X) == X, "World Map -> kept");
    reset_state(); img[SMM_BTLTYP] = 1; img[SMM_PAUSE_BLOCK - 2] = 1; w16(SMM_PAUSE_BLOCK, 4); w16(SMM_PAUSE_BLOCK + 2, 1); w16(SMM_PAUSE_BLOCK + 4, 0x8AAF);
                                                     check(press_with_obj(X) == X, "battle + Lua anywhere pause armed -> kept");

    printf("-- R1 + Start\n");
    reset_state(); w32(0x900724, 0xFFFFFFFF); w32(0xE00000, 0);
    check(((int (*)(void))(img + S + SMM_START_OFF))() == -1 && r32(0x900724) == 0xFFFFFFFF, "Start alone -> normal (edx=-1)");
    reset_state(); img[SMM_SHOULDERS] = 0x08; img[0x9006B0] = 0; w32(0x900724, 0xFFFFFFFF); w32(0xE00000, 0);
    check(((int (*)(void))(img + S + SMM_START_OFF))() == 0x1234, "R1 + Start -> handled, rejoins via the loop hook");
    check(r32(0x900724) == 3 && r32(0xE00000) == 3, "  real OpenMenu set currentmenu=3, follow-up got 3");

    printf("-- main loop (party menu flag)\n");
    reset_state(); img[SMM_MENU_BLOCK] = 6; img[SMM_BTLTYP] = 2;
    check(((int (*)(void))(img + S + SMM_LOOP_OFF))() == 0x1234 && img[SMM_MENU_BLOCK] == 0, "in battle: block byte cleared, loop continues");
    reset_state(); img[SMM_MENU_BLOCK] = 6; img[SMM_WORLD] = 0x0F;
    ((int (*)(void))(img + S + SMM_LOOP_OFF))();
    check(img[SMM_MENU_BLOCK] == 6, "World Map: block byte left alone");
    reset_state(); w32(SMM_CUT_FLAGS, 5); ((int (*)(void))(img + S + SMM_LOOP_OFF))();
    reset_state(); ((int (*)(void))(img + S + SMM_LOOP_OFF))();

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails;
}
