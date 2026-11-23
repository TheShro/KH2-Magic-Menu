/*
 * Shro's Magic Menu - native module, STEP 1: read-only probe.
 * KH2 Final Mix (PC, x64). Loaded by OpenKH Panacea from <openkh>\mod\kh2\dll\.
 *
 * What it does: finds three game functions by byte signature (AOB), decodes the
 * rip-relative operands inside them, and logs the module offsets it derived.
 * It writes NOTHING to game memory.
 *
 * Log: ShroMagicMenu.log next to the game exe.
 * Authors: Shro and Claude. Signatures come from our own Cheat Engine disassembly.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SMM_VERSION "step1-probe 0.1"

/* ---------------------------------------------------------------- logging */
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void smm_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    DWORD written;
    SYSTEMTIME t;

    if (g_log == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&t);
    n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[%02u:%02u:%02u.%03u] ",
                    t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (n < 0) n = 0;
    va_start(ap, fmt);
    _vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    strcat_s(buf, sizeof(buf), "\r\n");
    WriteFile(g_log, buf, (DWORD)strlen(buf), &written, NULL);
    FlushFileBuffers(g_log);
}

static void smm_open_log(void)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *slash;

    if (len == 0 || len >= MAX_PATH) return;
    slash = wcsrchr(path, L'\\');
    if (!slash) return;
    slash[1] = 0;
    if (wcslen(path) + 20 >= MAX_PATH) return;
    wcscat_s(path, MAX_PATH, L"ShroMagicMenu.log");
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ---------------------------------------------------------------- signatures */
/* Text signatures, "??" = wildcard. Identical to tests/check_signatures.py. */
typedef struct {
    const char *name;
    const char *sig;
    DWORD expected;          /* module offset where it matched on Steam Global (Nov 2026) */
    DWORD found;             /* module offset found now, 0 if unresolved */
    int count;               /* number of matches (must be exactly 1) */
} SmmTarget;

static SmmTarget g_targets[] = {
    { "OpenMenu",
      "80 3D ?? ?? ?? ?? 00 74 0F 39 0D ?? ?? ?? ?? 74 17 89 0D ?? ?? ?? ?? C3 "
      "89 0D ?? ?? ?? ?? C7 05 ?? ?? ?? ?? FF FF FF FF C3",
      0x151770, 0, 0 },
    /* The two bytes at +0x0E are the mission-pause branch (75 17). Wildcarded so the
       Lua script's patch (90 90) cannot break this scan. */
    { "StartCheck",
      "F6 40 08 02 74 2B E8 ?? ?? ?? ?? 48 85 C0 ?? ?? E8 ?? ?? ?? ?? 84 C0 75 18 "
      "BA FF FF FF FF 33 C9 E8",
      0x1572F9, 0, 0 },
    { "PauseSelect",
      "83 F9 05 75 05 E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? C1 E8 02 A8 01 74 0E "
      "F7 C6 F7 FF FF FF B8 0F 00 00 00 0F 44 F0 33 DB 4C 8D 25 ?? ?? ?? ??",
      0x2E4983, 0, 0 },
};
#define SMM_TARGET_COUNT (sizeof(g_targets) / sizeof(g_targets[0]))

#define SMM_MAX_SIG 128

/* Parse "AB ?? CD" into bytes + mask. Returns length, 0 on error. */
static int smm_parse_sig(const char *text, BYTE *bytes, BYTE *mask)
{
    int len = 0;
    while (*text) {
        while (*text == ' ') text++;
        if (!*text) break;
        if (len >= SMM_MAX_SIG) return 0;
        if (text[0] == '?' && text[1] == '?') {
            bytes[len] = 0; mask[len] = 0;
        } else {
            unsigned v;
            if (sscanf_s(text, "%2x", &v) != 1) return 0;
            bytes[len] = (BYTE)v; mask[len] = 1;
        }
        len++;
        text += 2;
    }
    return len;
}

static BYTE *g_base = NULL;

/* Scan every executable section of the game module. */
static void smm_scan_target(SmmTarget *t)
{
    BYTE bytes[SMM_MAX_SIG], mask[SMM_MAX_SIG];
    int len = smm_parse_sig(t->sig, bytes, mask);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    t->count = 0;
    t->found = 0;
    if (len == 0 || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    sec = IMAGE_FIRST_SECTION(nt);

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        BYTE *start, *end, *p;
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        start = g_base + sec->VirtualAddress;
        end = start + sec->Misc.VirtualSize;
        if (end - start < len) continue;
        for (p = start; p <= end - len; p++) {
            int k;
            if (p[0] != bytes[0]) continue;      /* first byte is never a wildcard */
            for (k = 1; k < len; k++)
                if (mask[k] && p[k] != bytes[k]) break;
            if (k == len) {
                if (t->count == 0) t->found = (DWORD)(p - g_base);
                t->count++;
                if (t->count > 1) return;        /* ambiguous: stop early */
            }
        }
    }
}

/* Decode a rip-relative operand: instruction at module offset 'at', the 4-byte
   displacement at 'at + disp_at', instruction length 'ilen'. Returns a module offset. */
static DWORD smm_rip(DWORD at, int disp_at, int ilen)
{
    INT32 d;
    memcpy(&d, g_base + at + disp_at, 4);
    return (DWORD)((INT64)at + ilen + d);
}

static void smm_check(const char *what, DWORD got, DWORD expected)
{
    smm_log("  %-34s exe+%06X  %s", what, got,
            got == expected ? "(matches our notes)" : "(DIFFERENT from our notes)");
}

/* ---------------------------------------------------------------- probe */
static int smm_probe(int final_pass)
{
    unsigned i;
    int resolved = 0;

    for (i = 0; i < SMM_TARGET_COUNT; i++) {
        SmmTarget *t = &g_targets[i];
        smm_scan_target(t);
        if (t->count == 1) resolved++;
    }
    if (resolved < (int)SMM_TARGET_COUNT && !final_pass) return 0;

    for (i = 0; i < SMM_TARGET_COUNT; i++) {
        SmmTarget *t = &g_targets[i];
        if (t->count == 1)
            smm_log("%-12s FOUND at exe+%06X (expected exe+%06X)%s", t->name, t->found,
                    t->expected, t->found == t->expected ? "" : "  <- moved");
        else
            smm_log("%-12s NOT RESOLVED (%d matches; need exactly 1)", t->name, t->count);
    }

    if (g_targets[0].count == 1) {
        DWORD f = g_targets[0].found;
        smm_log("OpenMenu derived addresses:");
        smm_check("menu-open byte", smm_rip(f + 0x00, 2, 7), 0x9006B0);
        smm_check("menu queue", smm_rip(f + 0x09, 2, 6), 0x716B48);
        smm_check("currentmenu", smm_rip(f + 0x18, 2, 6), 0x900724);
    }
    if (g_targets[1].count == 1) {
        DWORD f = g_targets[1].found;
        DWORD site = f + 0x0E;
        smm_log("StartCheck derived addresses:");
        smm_check("mission-pause branch site", site, 0x157307);
        smm_log("  %-34s %02X %02X  %s", "site bytes now", g_base[site], g_base[site + 1],
                (g_base[site] == 0x75 && g_base[site + 1] == 0x17) ? "(original)" :
                (g_base[site] == 0x90 && g_base[site + 1] == 0x90) ? "(patched by the Lua script)" :
                "(UNEXPECTED)");
        smm_check("special-pause lookup function", smm_rip(f + 0x06, 1, 5), 0x3AD540);
    }
    if (g_targets[2].count == 1) {
        DWORD f = g_targets[2].found;
        smm_log("PauseSelect derived addresses:");
        smm_check("pause mode bits", smm_rip(f + 0x0A, 2, 6), 0x749830);
        smm_check("pause key table", smm_rip(f + 0x27, 3, 7), 0x5BA170);
    }
    smm_log("Probe finished: %d of %u targets resolved. Nothing was written to game memory.",
            resolved, (unsigned)SMM_TARGET_COUNT);
    return 1;
}

/* Retry for up to 90 s in case the code section is not ready at load time. */
static DWORD WINAPI smm_retry_thread(LPVOID unused)
{
    int tries;
    (void)unused;
    for (tries = 0; tries < 180; tries++) {
        Sleep(500);
        if (smm_probe(0)) {
            smm_log("(resolved on retry %d)", tries + 1);
            return 0;
        }
    }
    smm_probe(1);
    return 0;
}

static void smm_start(void)
{
    char marker[9] = { 0 };

    smm_open_log();
    g_base = (BYTE *)GetModuleHandleW(NULL);
    smm_log("Shro's Magic Menu native module %s", SMM_VERSION);
    smm_log("Game module base %p", g_base);
    if (!g_base) return;

    /* KSX's build check: the text "epicuser" sits at exe+585E59 only on Steam Global. */
    memcpy(marker, g_base + 0x585E59, 8);
    smm_log("Build marker at exe+585E59: \"%s\" %s", marker,
            strcmp(marker, "epicuser") == 0 ? "(Steam Global)" : "(not Steam Global - offsets may differ)");

    if (!smm_probe(0)) {
        HANDLE th;
        smm_log("Not all targets resolved at load; retrying in the background for up to 90 s.");
        th = CreateThread(NULL, 0, smm_retry_thread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        smm_start();
    }
    return TRUE;
}
