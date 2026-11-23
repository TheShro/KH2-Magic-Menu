/* Step 4 test: maps the REAL game exe (path in argv[1]) into a fake process image, then runs the
   real module: full-code signature scan, hook install, the shipped caves, the game's real OpenMenu,
   and the Load combo sequence. Windows x64 calling convention modelled with ms_abi. */
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "windows.h"
#define IMG_SIZE 0x2E00000
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
static BYTE *smm_alloc_near(BYTE *target, SIZE_T size) { (void)target; (void)size; return img + 0x2D00000; }
#include "../src/shro_magic_menu.c"

static int fails = 0;
static void check(int cond, const char *msg) { printf("%s %s\n", cond ? "PASS" : "FAIL", msg); if (!cond) fails++; }
static DWORD r32(DWORD off) { DWORD v; memcpy(&v, img + off, 4); return v; }
static WORD r16(DWORD off) { WORD v; memcpy(&v, img + off, 2); return v; }
static void w32(DWORD off, DWORD v) { memcpy(img + off, &v, 4); }
static void w16(DWORD off, WORD v) { memcpy(img + off, &v, 2); }
static void put(DWORD off, const BYTE *b, int n) { memcpy(img + off, b, n); }
#define S 0x1572F9
#define REC 0x2C80000          /* follow-up stub records its argument here */
static int call_at(DWORD off) { return ((int (*)(void))(img + off))(); }

static void map_exe(const char *path) {
    FILE *f = fopen(path, "rb"); long n; BYTE *d; DWORD pe, i; WORD nsec, opt;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); d = malloc(n); fread(d, 1, n, f); fclose(f);
    img = mmap(NULL, IMG_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(&pe, d + 0x3C, 4); memcpy(&nsec, d + pe + 6, 2); memcpy(&opt, d + pe + 20, 2);
    memcpy(img, d, 0x1000);                                         /* headers */
    for (i = 0; i < nsec; i++) {
        BYTE *sh = d + pe + 24 + opt + i * 40; DWORD vs, va, rs, rp;
        memcpy(&vs, sh + 8, 4); memcpy(&va, sh + 12, 4); memcpy(&rs, sh + 16, 4); memcpy(&rp, sh + 20, 4);
        memcpy(img + va, d + rp, rs < vs || vs == 0 ? rs : vs);
    }
    free(d);
}

static UINT64 press_with_obj(UINT64 obj) {
    BYTE t[15]; INT32 rel;
    t[0] = 0x48; t[1] = 0xB8; memcpy(t + 2, &obj, 8); t[10] = 0xE9;
    rel = (INT32)((S + SMM_SPECIAL_OFF) - (0xE10000 + 15)); memcpy(t + 11, &rel, 4);
    put(0xE10000, t, 15);
    return ((UINT64 (*)(void))(img + 0xE10000))();
}
/* stand-in for the game's special-pause lookup (its helpers need real game state): highest active slot */
static void * __attribute__((ms_abi)) test_lookup(void) { int k; for (k = 4; k >= 0; k--) if (img[0x2A114A0 + k * 0x40]) return img + 0x2A114A0 + k * 0x40; return NULL; }
static uint64_t r64(DWORD off) { uint64_t v; memcpy(&v, img + off, 8); return v; }

int main(int argc, char **argv) {
    int mutation = argc > 2 ? atoi(argv[2]) : 0, i;
    setvbuf(stdout, NULL, _IONBF, 0);
    map_exe(argv[1]);
    { BYTE stub[7] = { 0x89, 0x0D, 0, 0, 0, 0, 0xC3 }; INT32 d = (INT32)(REC - (0x151480 + 6)); memcpy(stub + 2, &d, 4); put(0x151480, stub, 7); }
    /* special-pause routine stub (first bytes only; the jmp at +0x7D stays for the cross-check):
       records rcx, rdx and slot[+2] (option count) at call time */
    { BYTE st[24] = { 0x48,0x89,0x0D,0,0,0,0, 0x48,0x89,0x15,0,0,0,0, 0x8A,0x41,0x02, 0x88,0x05,0,0,0,0, 0xC3 };
      INT32 d1 = (INT32)((REC + 16) - (0x2E4C20 + 7)), d2 = (INT32)((REC + 24) - (0x2E4C20 + 14)), d3 = (INT32)((REC + 32) - (0x2E4C20 + 23));
      memcpy(st + 3, &d1, 4); memcpy(st + 10, &d2, 4); memcpy(st + 19, &d3, 4); put(0x2E4C20, st, 24); }
    if (mutation == 1) img[0x31DF8B] = 0x02;
    img[SMM_WORLD] = 0x04;
    /* "original" values to see restored */
    w32(0x8EC5B0, 0x3F800000); w16(0x2AB275A, 3);

    DllMain(NULL, DLL_PROCESS_ATTACH, NULL);
    printf("----\n");
    check(img[0x157304] == 0xE9 && img[0x157312] == 0xE9 && img[0x15732A] == 0xE9, "real exe: all three hooks installed");
    if (mutation) check(g_pause_ok == 0, "mutation: Load case changed -> custom pause unavailable, other hooks still in");
    else check(g_pause_ok && g_slot3 == 0x2A11560 && g_screen_cur == 0x744C10 &&
               (BYTE *)g_special_open == img + 0x2E4C20, "custom pause addresses derived: 2E4C20 / 2A11560 / 744C10");

    { BYTE b[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 }; put(S + SMM_NORMAL_OFF, b, 6); }
    { BYTE b[1] = { 0xC3 }; put(S + SMM_MISSION_OFF, b, 1); }
    { BYTE b[3] = { 0x89, 0xD0, 0xC3 }; put(S + SMM_START_BACK, b, 3); }
    { BYTE b[6] = { 0xB8, 0x34, 0x12, 0x00, 0x00, 0xC3 }; put(S + SMM_LOOP_BACK, b, 6); }

    img[SMM_SHOULDERS] = 0x0C; img[0x9006B0] = 0; w32(0x900724, 0xFFFFFFFF); w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == -1 && r32(REC) == 0, "L1+R1 + Start -> normal Start (combo removed)");
    img[SMM_SHOULDERS] = 0x08; img[0x9006B0] = 0; w32(0x900724, 0xFFFFFFFF); w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r32(0x900724) == 3 && r32(REC) == 3, "R1 + Start -> save point menu (3)");
    img[SMM_SHOULDERS] = 0x04; img[0x9006B0] = 0; w32(0x900724, 0xFFFFFFFF); w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == -1 && r32(REC) == 0, "L1 alone + Start -> normal Start");

    img[SMM_SHOULDERS] = 0x02; w16(0x744C10, 0xFFFF); memset(img + REC + 16, 0, 24); w32(0x900724, 0xFFFFFFFF);
    if (mutation) {
        check(call_at(S + SMM_START_OFF) == -1, "mutation: R2 + Start -> normal Start");
    
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
        return fails;
    }
    check(call_at(S + SMM_START_OFF) == 0x1234, "R2 + Start (field) -> handled");
    check(r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560) && r64(REC + 24) == 0, "  special-pause routine called with slot 3, 0");
    check(img[REC + 32] == 4, "  slot 3 filled before the call (count 4)");
    check(r16(0x2AB275A) == 4 && r32(0x8EC5B0) == 0x44960000 && r16(0x2A115A2) == 2, "  KSX's other tables + loadingheart written (as the Lua does)");
    check(r32(0x900724) == 0xFFFFFFFF, "  no OpenMenu call");
    for (i = 0; i < 5; i++) call_at(S + SMM_LOOP_OFF);
    check(img[0x2A11560] == 0 && r16(0x2A11562) == 4 && g_pause_phase == 1, "  right after opening: slot disarmed (no stale Start), buttons kept");
    w16(0x744C10, 9); for (i = 0; i < 5; i++) call_at(S + SMM_LOOP_OFF);
    w16(0x744C10, 1); for (i = 0; i < 5; i++) call_at(S + SMM_LOOP_OFF);
    check(r16(0x2A11562) == 4 && r16(0x2A1156C) == 0x000A && g_pause_phase == 2, "  pause open (main page, then load screen): values kept");
    w16(0x744C10, 0xFFFF); call_at(S + SMM_LOOP_OFF);
    check(img[0x2A11560] == 0 && r16(0x2A11562) == 0 && r16(0x2AB275A) == 3 && r32(0x8EC5B0) == 0x3F800000 && g_pause_phase == 0,
          "  pause closed -> everything put back to the originals");

    w16(0x744C10, 0xFFFF);
    call_at(S + SMM_START_OFF);
    for (i = 0; i < 310; i++) call_at(S + SMM_LOOP_OFF);
    check(g_pause_phase == 0 && img[0x2A11560] == 0 && r32(0x8EC5B0) == 0x3F800000, "pause never starts -> restored after 120 frames");

    /* the game owns slot 3 (mission pause): use free slot 2, then slot 0, never touch slot 3 */
    { BYTE m[0x40]; for (i = 0; i < 0x40; i++) m[i] = (BYTE)(i * 7 + 1); m[0] = 1;
      memcpy(img + 0x2A11560, m, 0x40); memset(img + 0x2A114A0, 0, 0x40); memset(img + 0x2A11520, 0, 0x40);
      memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11520),
            "slot 3 in use by the game -> custom pause in free slot 2");
      check(memcmp(img + 0x2A11560, m, 0x40) == 0, "  the game's slot 3 untouched");
      check(img[0x2A11520] == 0 && r16(0x2A11522) == 4 && r16(0x2A11524) == 0x0001, "  slot 2 filled, disarmed after the call");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      for (i = 0; i < 0x40 && img[0x2A11520 + i] == 0; i++) {}
      check(g_pause_phase == 0 && i == 0x40 && memcmp(img + 0x2A11560, m, 0x40) == 0, "  closed -> slot 2 back to all zero, slot 3 still the game's");
      /* quick re-press: no lockout, the pending restore happens on the spot */
      memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF);
      check(call_at(S + SMM_START_OFF) == 0x1234 && g_pause_phase == 1, "R2 + Start again -> opens again");
      check(call_at(S + SMM_START_OFF) == 0x1234 && g_pause_phase == 1 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11520),
            "  and again straight away (no lockout: previous one tidied up first)");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      for (i = 0; i < 0x40 && img[0x2A11520 + i] == 0; i++) {}
      check(i == 0x40, "  slot 2 back to all zero");
      img[0x2A11530] = 5; memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A114A0), "slot 2 has data -> slot 0 used");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      img[0x2A114B0] = 5; memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == -1 && r64(REC + 16) == 0 && img[0x2A114B0] == 5 && img[0x2A11530] == 5,
            "no free slot -> normal Start, nothing written");
      memset(img + 0x2A114A0, 0, 0x40); memset(img + 0x2A11520, 0, 0x40); memset(img + 0x2A11560, 0, 0x40); }
    img[SMM_WORLD] = 0x0F;
    check(call_at(S + SMM_START_OFF) == -1, "World Map -> normal Start");
    img[SMM_WORLD] = 0x04;
    img[SMM_SHOULDERS] = 0x03;
    check(call_at(S + SMM_START_OFF) == -1, "L2+R2 + Start -> normal Start (auto-load removed)");

    /* R2 in an ordinary battle (no mission pointers): the field layout */
    img[SMM_SHOULDERS] = 0x02; img[SMM_BTLTYP] = 1; memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560), "battle + R2 + Start -> custom pause");
    check(r16(0x2A11564) == 0x0001 && r16(0x2A11568) == 0x0011 && r16(0x2A1156C) == 0x000A && r16(0x2A11570) == 6 && img[0x2A11576] == 15,
          "  battle: Save / Load / Journal / Continue (retry buttons off), +0x16 intact");
    w16(0x744C10, 9); call_at(S + SMM_LOOP_OFF); w16(0x744C10, 0xFFFF); call_at(S + SMM_LOOP_OFF);
    check(g_pause_phase == 0 && img[0x2A11560] == 0, "  closed -> restored");

    /* R2 in a mission (mission pointers set): same buttons */
    memset(img + 0x2A0FF68, 0x11, 16); memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF); img[SMM_BTLTYP] = 2;
    check(call_at(S + SMM_START_OFF) == 0x1234, "mission + R2 + Start -> custom pause");
    check(r16(0x2A11564) == 0x0001 && r16(0x2A1156C) == 0x000A, "  mission: field layout (retry buttons off)");
    w16(0x744C10, 9); call_at(S + SMM_LOOP_OFF); w16(0x744C10, 0xFFFF); call_at(S + SMM_LOOP_OFF);
    check(g_pause_phase == 0 && img[0x2A11560] == 0 && r16(0x2A11564) == 0, "  closed -> restored");
    memset(img + 0x2A0FF68, 0, 16); img[SMM_BTLTYP] = 0;

    /* in battle with a special pause (mission) */
    { UINT64 X = 0x00007FF600C0FFEEULL;
      img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0x02; w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0);
      check(press_with_obj(X) == 1, "mission + R2 + Start -> normal path (custom pause)");
      img[SMM_SHOULDERS] = 0;
      check(press_with_obj(X) == 1, "mission + Start -> normal path (party menu)");
      w32(SMM_CUT_FLAGS, 5);
      check(press_with_obj(X) == X, "mission + cutscene -> game's pause kept");
      w32(SMM_CUT_FLAGS, 1); img[SMM_BTLTYP] = 0;
      check(press_with_obj(X) == X, "special pause out of battle -> kept"); }


    /* ================================================================ settings (v1.6) */
    printf("-- settings file\n");
    check(g_ini_writes > 0 && strcmp(g_ini_written, SMM_DEFAULT_INI) == 0, "no ini -> the default file is written next to the exe");
    g_test_ini = g_ini_written; smm_read_ini();
    check(smm_load_settings(1) == 0 && memcmp(&g_cfg, &SMM_DEFAULTS, sizeof(g_cfg)) == 0, "the default file parses back to exactly the defaults");
    { SmmBind b; char e[96];
      check(smm_parse_bind("R1+Start", &b, e, sizeof e) && b.mods == 0x08 && b.trig == 0x08, "bind R1+Start");
      check(smm_parse_bind(" l2 + r2 + up ", &b, e, sizeof e) && b.mods == 0x03 && b.trig == 0x10, "bind ' l2 + r2 + up ' (spaces, case)");
      check(smm_parse_bind("L1+R1+Left", &b, e, sizeof e) && b.mods == 0x0C && b.trig == 0x80, "bind L1+R1+Left");
      check(smm_parse_bind("None", &b, e, sizeof e) && b.trig == 0, "bind None");
      check(!smm_parse_bind("Start", &b, e, sizeof e), "bind Start alone -> rejected");
      printf("    (%s)\n", e);
      check(!smm_parse_bind("R1", &b, e, sizeof e), "bind R1 alone -> rejected");
      check(!smm_parse_bind("R1+Up+Start", &b, e, sizeof e), "bind with two triggers -> rejected");
      check(!smm_parse_bind("R1+Triangle", &b, e, sizeof e), "unknown button -> rejected");
      printf("    (%s)\n", e);
      check(!smm_parse_bind("R1+R1+Start", &b, e, sizeof e), "button twice -> rejected"); }
    g_test_ini = "[SaveMenu]\nKeybind = R2+Start\n[CustomPause]\nKeybind = r2 + start\nShowJournal = maybe\n[Bogus]\nX=1\n";
    smm_read_ini();
    { int pr = smm_load_settings(0);
      check(pr == 3 && g_cfg.save_on && !g_cfg.pause_on && g_cfg.show_journal == 1, "same keybind twice -> custom pause off; bad value and unknown key reported, defaults kept"); }

    /* --- button toggles */
    g_test_ini = "[CustomPause]\nShowJournal = 0\n"; smm_read_ini(); smm_load_settings(0);
    img[SMM_BTLTYP] = 0; img[SMM_SHOULDERS] = 0x02; w16(0x744C10, 0xFFFF); memset(img + 0x2A11560, 0, 0x40);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r16(0x2A11562) == 3 && r16(0x2A11564) == 1 && r16(0x2A11568) == 0x11 &&
          r16(0x2A1156C) == 6 && r16(0x2A1156E) == 0x8AB4 && r16(0x2A11570) == 0, "ShowJournal = 0 -> Save, Load Game, Continue (count 3)");
    g_test_ini = "[CustomPause]\nShowSave = 0\nShowLoadGame=0\nShowJournal=1\n"; smm_read_ini(); smm_load_settings(0);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r16(0x2A11562) == 2 && r16(0x2A11564) == 0x0A && r16(0x2A11568) == 6, "  Journal + Continue only (count 2)");
    for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
    check(g_pause_phase == 0 && r16(0x2A11562) == 0 && r16(0x2A11564) == 0, "  closed -> restored");

    /* --- custom keybinds on Start */
    g_test_ini = "[SaveMenu]\nKeybind = L1+R1+Start\n[CustomPause]\nKeybind = L2+Start\n"; smm_read_ini(); smm_load_settings(0);
    img[0x9006B0] = 0; w32(0x900724, 0xFFFFFFFF); w32(REC, 0); img[SMM_SHOULDERS] = 0x08;
    check(call_at(S + SMM_START_OFF) == -1 && r32(REC) == 0, "save bind L1+R1+Start: R1 alone -> normal Start");
    img[SMM_SHOULDERS] = 0x0C | 0x40;   /* plus a face button: ignored */
    check(call_at(S + SMM_START_OFF) == 0x1234 && r32(REC) == 3, "  L1+R1 (+ a face button) + Start -> save menu");
    img[SMM_SHOULDERS] = 0x01; memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560), "pause bind L2+Start -> custom pause");
    for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
    img[SMM_SHOULDERS] = 0x02; memset(img + REC + 16, 0, 24);
    check(call_at(S + SMM_START_OFF) == -1 && r64(REC + 16) == 0, "  R2 + Start now normal");

    /* --- D-pad keybinds behind the game's own Start gate */
    printf("-- D-pad keybinds\n");
    { BYTE f0[3] = { 0x31, 0xC0, 0xC3 }, ptr[11] = { 0x48, 0xB8 }; UINT64 q = (UINT64)(uintptr_t)(img + 0xE20000);
      memcpy(ptr + 2, &q, 8); ptr[10] = 0xC3;
      put(0x1515C0, f0, 3); put(0x19BA60, f0, 3); put(0x3A0A30, f0, 3); put(0x156DC0, ptr, 11);
      memset(img + 0xE20000, 0, 0x200); w32(0xABB878, 0); img[0x9BA8D0] = 1; }
    g_test_ini = "[SaveMenu]\nKeybind = L2+R2+Up\n[CustomPause]\nKeybind = L2+R2+Down\n"; smm_read_ini(); smm_load_settings(0);
    { BYTE keep[7], orig[7] = { 0xE8, 0x22, 0x97, 0x24, 0x00, 0x84, 0xC0 };   /* the harness stubs S+0x10; resolve against the real bytes */
      memcpy(keep, img + S + 0x10, 7); put(S + 0x10, orig, 7);
      g_gate_ok = smm_resolve_gate();
      put(S + 0x10, keep, 7); }
    g_lookup = test_lookup;
    check(g_gate_ok && g_gate_dw == 0xABB878 && g_gate_b == 0x9BA8D0 && (BYTE *)g_gate_f2 == img + 0x156DC0, "gate resolved from the code in front of the Start check");
    img[SMM_SHOULDERS] = 0x03; img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
    w32(0x900724, 0xFFFFFFFF); w32(REC, 0); img[SMM_DPAD] = 0x10;
    call_at(S + SMM_LOOP_OFF);
    check(r32(REC) == 3 && r32(0x900724) == 3, "L2+R2 + Up -> save menu (from the main loop)");
    w32(REC, 0); call_at(S + SMM_LOOP_OFF);
    check(r32(REC) == 0, "  Up still held -> nothing more");
    img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
    memset(img + REC + 16, 0, 24); w16(0x744C10, 0xFFFF); img[SMM_DPAD] = 0x40; call_at(S + SMM_LOOP_OFF);
    check(r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560) && g_pause_phase == 1, "L2+R2 + Down -> custom pause");
    img[SMM_DPAD] = 0; for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
    w32(REC, 0); img[SMM_DPAD] = 0x40; call_at(S + SMM_LOOP_OFF); img[SMM_DPAD] = 0x50; call_at(S + SMM_LOOP_OFF);
    check(r32(REC) == 0, "  Down held, then Up -> two directions, nothing");
    { BYTE t1[3] = { 0xB0, 0x01, 0xC3 }, f0[3] = { 0x31, 0xC0, 0xC3 };
      img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      put(0x1515C0, t1, 3); w32(REC, 0); img[SMM_DPAD] = 0x10; call_at(S + SMM_LOOP_OFF);
      check(r32(REC) == 0, "  game's check 1 says no (e.g. a menu is up) -> nothing");
      put(0x1515C0, f0, 3); img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      w32(0xE20108, 1); img[SMM_DPAD] = 0x10; call_at(S + SMM_LOOP_OFF);
      check(r32(REC) == 0, "  game's check 2 says no -> nothing");
      w32(0xE20108, 0); img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      put(0x3A0A30, t1, 3); img[SMM_DPAD] = 0x10; call_at(S + SMM_LOOP_OFF);
      check(r32(REC) == 0, "  game's last check says no -> nothing");
      put(0x3A0A30, f0, 3); img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      img[0x2A11560] = 1; img[SMM_BTLTYP] = 0; img[SMM_DPAD] = 0x10; call_at(S + SMM_LOOP_OFF);
      check(r32(REC) == 0, "  special pause slot active outside battle (minigame) -> nothing");
      img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      img[SMM_BTLTYP] = 2; img[SMM_DPAD] = 0x10; call_at(S + SMM_LOOP_OFF);
      check(r32(REC) == 3, "  same slot active in a battle (mission) -> save menu");
      img[0x2A11560] = 0; img[SMM_BTLTYP] = 0; img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF); }
    img[SMM_SHOULDERS] = 0x03; w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == -1 && r32(REC) == 0, "  L2+R2 + Start -> normal Start (only D-pad bound)");

    /* --- GameRetryPause: a fight the game can restart (its slot offers Retry) */
    printf("-- GameRetryPause\n");
    g_test_ini = NULL; smm_read_ini(); smm_load_settings(0);
    { static const WORD van[9] = { 4, 6, 0x8AB4, 3, 0x8AB1, 0x0C, 0xAF3A, 4, 0x8AB2 };
      BYTE before[0x40];
      memset(img + 0x2A114A0, 0, 0x100); img[0x2A11560] = 1; memcpy(img + 0x2A11562, van, sizeof van);
      memcpy(before, img + 0x2A11560, 0x40);
      img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0x02; w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0);
      check(smm_on_special(img + 0x2A11560) == 0, "retry fight + R2 + Start -> normal path (to our keybind)");
      memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560),
            "  -> the game's own pause opened with its slot (Retry offered)");
      check(memcmp(before, img + 0x2A11560, 0x40) == 0 && g_pause_phase == 0 && img[0x2A114A0] == 0 && img[0x2A11520] == 0,
            "  the game's slot untouched, nothing filled, nothing to restore");
      img[SMM_SHOULDERS] = 0;
      check(smm_on_special(img + 0x2A11560) == 0, "  plain Start there -> party menu as before");
      g_test_ini = "[CustomPause]\nGameRetryPause = 0\n"; smm_read_ini(); smm_load_settings(0);
      img[SMM_SHOULDERS] = 0x02; smm_on_special(img + 0x2A11560); memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11520),
            "  GameRetryPause = 0 -> custom pause in a free slot");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      g_test_ini = NULL; smm_read_ini(); smm_load_settings(0);
      w16(0x2A11568, 0x0A); w16(0x2A1156C, 0x01);   /* no Retry / Prepare & Retry in it */
      smm_on_special(img + 0x2A11560); memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11520),
            "  slot without Retry (plain mission pause) -> custom pause as before");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      memcpy(img + 0x2A11560, before, 0x40); img[SMM_BTLTYP] = 0;
      smm_on_special(img + 0x2A11560); memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11520),
            "  outside battle -> custom pause (Retry only offered in fights)");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      /* the same through a D-pad keybind */
      g_test_ini = "[CustomPause]\nKeybind = L2+R2+Down\n"; smm_read_ini(); smm_load_settings(0);
      img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0x03; img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      memset(img + REC + 16, 0, 24); img[SMM_DPAD] = 0x40; call_at(S + SMM_LOOP_OFF);
      check(r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560) && g_pause_phase == 0, "  D-pad keybind in a retry fight -> the game's pause");
      img[SMM_DPAD] = 0; call_at(S + SMM_LOOP_OFF);
      memset(img + 0x2A114A0, 0, 0x100); img[SMM_BTLTYP] = 0; img[SMM_SHOULDERS] = 0;
      g_test_ini = NULL; smm_read_ini(); smm_load_settings(0); }

    /* --- leftovers from a mission pause that used slot 3 (seen after the Sephiroth fight) */
    { static const BYTE left[0x40] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0x0F,0, 0x02,0x8D,0x4F,0x80, 0,0,0,0,
                                       0,0,0,0, 0x03,0,0x02,0 };
      img[SMM_BTLTYP] = 0; img[SMM_SHOULDERS] = 0x02; w16(0x744C10, 0xFFFF); memset(img + 0x2A114A0, 0, 0x100);
      memcpy(img + 0x2A11560, left, 0x40); memset(img + REC + 16, 0, 24);
      check(call_at(S + SMM_START_OFF) == 0x1234 && r64(REC + 16) == (uint64_t)(uintptr_t)(img + 0x2A11560), "slot 3 unarmed but holding a mission's leftovers -> used");
      check(r32(0x2A11578) == 0 && r32(0x2A11584) == 0 && img[0x2A11576] == 15 && r16(0x2A11562) == 4,
            "  leftovers zeroed (+0x18, +0x24), our fields written: the pause sees a clean slot");
      for (i = 0; i < 130; i++) call_at(S + SMM_LOOP_OFF);
      check(memcmp(img + 0x2A11560, left, 0x40) == 0, "  closed -> all 64 bytes back as the game left them");
      memset(img + 0x2A11560, 0, 0x40); }

    /* --- ForceWorldTravel */
    printf("-- ForceWorldTravel\n");
    g_test_ini = "[CustomPause]\nForceWorldTravel = 1\n"; smm_read_ini(); smm_load_settings(0);
    check(img[0x31D504] == 2, "before: exe+31D504 = 2");
    check(smm_resolve_force() && g_force_imm == 0x31D504 && g_save_mode == 0xBEDE90, "resolved: imm exe+31D504, mode variable exe+BEDE90");
    smm_apply_force();
    check(img[0x31D504] == 3 && g_cfg.force_world, "applied: 2 -> 3");
    w32(0xBEDE90, 2); img[SMM_SHOULDERS] = 0x08; w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r32(REC) == 3 && r32(0xBEDE90) == 3, "save keybind: mode 2 cleared (3) before opening");
    g_cfg.force_world = 0; w32(0xBEDE90, 2); w32(REC, 0);
    check(call_at(S + SMM_START_OFF) == 0x1234 && r32(0xBEDE90) == 2, "  with it off: mode left alone");
    img[0x31D504] = 2;

    /* --- party menu toggles */
    printf("-- party menu toggles\n");
    g_test_ini = "[PartyMenu]\nInBattle = 0\nInMissions = 0\n"; smm_read_ini(); smm_load_settings(0);
    img[SMM_BTLTYP] = 1; img[0x2A114E0] = 6; call_at(S + SMM_LOOP_OFF);
    check(img[0x2A114E0] == 6, "InBattle = 0 -> battle pause slot left alone");
    { UINT64 X = 0x00007FF600C0FFEEULL;
      img[SMM_BTLTYP] = 2; img[SMM_SHOULDERS] = 0; w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0);
      check(press_with_obj(X) == X, "InMissions = 0 -> mission's own pause kept on Start");
      img[SMM_SHOULDERS] = 0x02;
      check(press_with_obj(X) == 1, "  but R2 + Start (our keybind) still takes the normal path");
      img[SMM_SHOULDERS] = 0; img[SMM_BTLTYP] = 0; img[0x2A114E0] = 0; }
    g_test_ini = NULL; smm_read_ini(); smm_load_settings(0);
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails;
}
