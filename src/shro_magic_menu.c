/*
 * Shro's Magic Menu - native module.
 * KH2 Final Mix (PC, x64, Steam Global). Loaded by OpenKH Panacea from <openkh>\mod\kh2\dll\.
 * Idea and original scripts: KSX ("Open Menu in Battle", "Save, Load, World Map Anywhere").
 *
 * Features (all set in ShroMagicMenu.ini, next to the game exe; written with defaults on first run)
 *   1. Save menu keybind (default R1 + Start): opens the save point menu directly.
 *   2. Custom pause keybind (default R2 + Start): Save / Load Game / Jiminy's Journal / Continue,
 *      anywhere but the World Map. Every pause is fed by a "pause slot" (five 0x40-byte slots;
 *      slot 1 = battle pause, slot 3 = KSX's, slot 4 = cutscenes). The module fills slot 3 (or a
 *      completely unused slot when a mission has slot 3), calls the game's special-pause routine
 *      with it, disarms the slot at once and puts every value back once the pause has closed.
 *      In fights where the game's own armed pause slot offers Retry / Prepare & Retry (minigames,
 *      missions), the keybind opens that pause untouched instead (GameRetryPause, on by default).
 *      ForceWorldTravel (off by default) patches the pause's save-menu mode (exe+31D504, KSX's
 *      "savemenu" byte) from 2 to 3 so World Map is offered, and clears mode 2 for the save menu
 *      keybind. Travelling out mid-story can softlock the game.
 *   3. Party menu in battle: every frame, clears the battle pause slot's active byte (0x2A114E0),
 *      except on the World Map.
 *   4. Party menu in missions: when Start is pressed in battle and the game wants its special
 *      pause, take the normal path instead (party menu), except in cutscenes, on the World Map
 *      or while KSX's Lua anywhere pause is armed. Our own Start keybinds always take the normal
 *      path in battle, so they work in missions even with this off.
 *   5. Load menu keybind (default L1 + Start): the custom pause, opened straight on its Load Game
 *      screen (a hook at the end of the pause's setup makes Load the first screen).
 *   6. Remote shop keybind (default L2 + Start, out of battle): calls the game's own OpenShop(id),
 *      the function its event code calls when a shopkeeper is talked to, with the shop id from
 *      the settings. By default only shops whose unlock flag is set in the save (the player has
 *      met that shopkeeper). Not the reaction-command route: running a reaction command needs
 *      the target object it belongs to (e.g. the moogle), and without one the game crashes.
 *   The Journal button is only offered once the story has unlocked Jiminy's Journal (flag 0x73).
 *   No saving during a battle (the Save keybind and the custom pause's Save button) unless
 *   SaveInBattle = 1: a save made mid-fight can load into a broken state.
 *   Keybinds are shoulder buttons plus Start, or shoulder buttons plus a D-pad direction. A D-pad
 *   bind only fires when the game's own "may Start pause now?" checks (the five tests in front of
 *   the Start code) pass, so it behaves like Start would.
 *
 * How
 *   Functions are found by byte signature at load (logged). Three 5-byte hooks go into the
 *   game's Start-button / main loop code, at sites with no rip-relative operands, each jumping to
 *   a small cave that calls a C function:
 *     exe+157304  test rax,rax / jne     -> special-pause decision
 *     exe+157312  mov edx,-1             -> Start keybinds
 *     exe+15732A  xor edx,edx / mov rcx  -> once per loop: party menu flag, D-pad keybinds,
 *                                           custom pause tidy-up, state logging
 *   All hooks are written once in DllMain, before the game loop runs, and only if every site
 *   matches. Otherwise nothing is patched and the log says why. Data the code cannot find on its
 *   own (controller, world, battle state, cutscene flags, KSX's tables) is checked by the data
 *   guard: every data address the code does name must match our notes or have moved by one
 *   common amount (then the rest moves with it); otherwise nothing is patched. Optional parts (custom pause,
 *   D-pad binds, ForceWorldTravel) are switched off on their own if their code looks different.
 *
 * Log: ShroMagicMenu.log next to the game exe.
 * Authors: Shro and Claude, from KSX's scripts. Signatures from our own disassembly; battle type
 * (BtlTyp) and world (Now) addresses from the GoA ROM randomizer script; menu flag, pause
 * tables and World Map patch from KSX's scripts; cutscene flags from our snapshot finder.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SMM_VERSION "1.9.1"

/* ---------------------------------------------------------------- data addresses (Steam Global) */
/* Data the code cannot find on its own is listed at its v1.0.0.7 address and read through DA(),
   which adds the shift the data guard measured (smm_data_guard: every data address the code CAN
   find must agree on one shift, else nothing is patched). 0 on the known build. */
static INT64 g_delta = 0;
static DWORD g_slot3 = 0;                    /* pause slot 3 (exe+2A11560), derived from the Start code */
#define DA(x) ((DWORD)((INT64)(x) + g_delta))
#define SMM_SHOULDERS     DA(0x7133EB)   /* controller: L2=01 R2=02 L1=04 R1=08 (+face buttons above) */
#define SMM_DPAD          DA(0x7133EA)   /* controller: Start=08 Up=10 Right=20 Down=40 Left=80 */
#define SMM_MENU_BLOCK    0x2A114E0      /* notes only: battle pause slot 1 (derived: g_slot1) */
#define SMM_WORLD         DA(0x717008)   /* Now+0x00, world id; 0x0F = World Map */
#define SMM_WORLD_MAP     0x0F
#define SMM_BTLTYP        DA(0x2A11404)  /* battle status: 0 none, 1 regular, 2 forced (GoA) */
#define SMM_CUT_FLAGS     DA(0xABB854)   /* bit 0x04 set in cutscenes */
#define SMM_CUT_PAUSE     (g_slot3 + 0x40) /* pause slot 4 (cutscenes): non-zero only in cutscenes */
#define SMM_PAUSE_BLOCK   (g_slot3 + 2)  /* slot 3 options: count, then (function, text) */
#define SMM_SAVE_MENU     3
/* The exe this module was written against (v1.0.0.7 Steam Global): PE header values. */
#define SMM_KNOWN_STAMP   0x669E384Au
#define SMM_KNOWN_SIZE    0x2C2B000u

/* Hook sites, relative to the StartCheck signature match. */
#define SMM_SPECIAL_OFF   0x0B       /* 48 85 C0 75 17 : test rax,rax / jne mission */
#define SMM_NORMAL_OFF    0x10       /* normal pause path */
#define SMM_MISSION_OFF   0x27       /* special pause path (33 D2 48 8B C8 ...) */
#define SMM_START_OFF     0x19       /* BA FF FF FF FF : mov edx,-1 */
#define SMM_START_BACK    0x1E
#define SMM_LOOP_OFF      0x31       /* 33 D2 48 8B CB : xor edx,edx / mov rcx,rbx */
#define SMM_LOOP_BACK     0x36
#define SMM_PAUSEOPEN_CALL 0x20      /* call to the game's pause opener (exe+2E4950), right after the start site */

/* Game functions use the Windows x64 calling convention. MSVC uses it by default; the
   Linux test build defines SMM_GAME_ABI as ms_abi. */
#ifndef SMM_GAME_ABI
#define SMM_GAME_ABI
#endif

/* ---------------------------------------------------------------- settings */
#define SMM_TRIG_START 0x08          /* bits of the D-pad byte */
#define SMM_TRIG_UP    0x10
#define SMM_TRIG_RIGHT 0x20
#define SMM_TRIG_DOWN  0x40
#define SMM_TRIG_LEFT  0x80
#define SMM_TRIG_DPAD  0xF0
#define SMM_MODS_MASK  0x0F          /* shoulder bits; face buttons are ignored */

typedef struct { BYTE mods; BYTE trig; } SmmBind;   /* trig 0 = unbound */
typedef struct {
    int save_on;  SmmBind save_bind;
    int pause_on; SmmBind pause_bind;
    int show_save, show_load, show_journal;
    int force_world;
    int game_retry;
    int party_battle, party_missions;
    int log_on, log_verbose;
    int load_on;  SmmBind load_bind;
    int shop_on;  SmmBind shop_bind;
    int shop_id;
    int shop_visited_only;
    int save_in_battle;
} SmmConfig;
static const SmmConfig SMM_DEFAULTS = {
    1, { 0x08, SMM_TRIG_START },       /* save menu: R1 + Start */
    1, { 0x02, SMM_TRIG_START },       /* custom pause: R2 + Start */
    1, 1, 1,                           /* Save, Load Game, Journal shown */
    0,                                 /* ForceWorldTravel off */
    1,                                 /* GameRetryPause on */
    1, 1,                              /* party menu in battle, in missions */
    1, 0,                              /* log on, verbose off */
    1, { 0x04, SMM_TRIG_START },       /* load menu: L1 + Start */
    1, { 0x01, SMM_TRIG_START },       /* remote shop: L2 + Start */
    -1,                                /* shop: the last moogle used (Last), else 0x13 */
    1,                                 /* only shops the story has already unlocked */
    0                                  /* no saving during a battle */
};
static SmmConfig g_cfg;

static const char SMM_DEFAULT_INI[] =
"; Shro's Magic Menu settings (KH2 Final Mix, Steam).\r\n"
"; Edit, save, then restart the game. Delete this file to get the defaults back.\r\n"
"; 1 = on, 0 = off.\r\n"
";\r\n"
"; Keybinds: buttons joined with +. Hold the shoulder buttons (L1 R1 L2 R2), then press the\r\n"
"; last one: Start, or a D-pad direction (Up Down Left Right).\r\n"
";   Examples: R1+Start   L1+R1+Start   L2+R2+Up\r\n"
"; Every keybind needs at least one shoulder button: plain Start stays the game's own, and the\r\n"
"; D-pad on its own moves the command menu. Hold exactly the shoulder buttons in the keybind,\r\n"
"; no others. D-pad keybinds only work where the game would let Start pause.\r\n"
"\r\n"
"[SaveMenu]\r\n"
"; Opens the save point menu straight away.\r\n"
"Enabled = 1\r\n"
"Keybind = R1+Start\r\n"
"; Allow saving during a battle: this keybind and the custom pause's Save button.\r\n"
"; WARNING: a save made in the middle of a battle (above all a boss fight or a mission) may load\r\n"
"; into a broken state or softlock your game. If you turn this on, keep a separate save from\r\n"
"; before the fight.\r\n"
"SaveInBattle = 0\r\n"
"\r\n"
"[CustomPause]\r\n"
"; Pause with Save / Load Game / Jiminy's Journal / Continue. Not available on the World Map.\r\n"
"Enabled = 1\r\n"
"Keybind = R2+Start\r\n"
"; Buttons to show (Continue is always shown).\r\n"
"ShowSave = 1\r\n"
"ShowLoadGame = 1\r\n"
"ShowJournal = 1\r\n"
"; Offer World Map in the save menu even where the story has travel locked.\r\n"
"; WARNING: leaving a world in the middle of a story sequence or mission can softlock your game.\r\n"
"; If you turn this on, keep a separate save from before you travel.\r\n"
"ForceWorldTravel = 0\r\n"
"; In fights where the game's own pause offers Retry / Prepare & Retry (minigames, missions),\r\n"
"; this keybind opens that pause instead, so you can retry. Plain Start still opens the party menu.\r\n"
"GameRetryPause = 1\r\n"
"\r\n"
"[LoadMenu]\r\n"
"; Opens the pause straight on the Load Game screen. Backing out of it shows the custom pause\r\n"
"; buttons (Continue to resume). Not available on the World Map.\r\n"
"Enabled = 1\r\n"
"Keybind = L1+Start\r\n"
"\r\n"
"[RemoteShop]\r\n"
"; Opens a shop from anywhere outside battle, the same way talking to its shopkeeper does.\r\n"
"Enabled = 1\r\n"
"Keybind = L2+Start\r\n"
"; Which shop: Last = the last moogle you talked to (remembered between sessions; the\r\n"
"; Hollow Bastion moogle until you have talked to one), or a shop id (hex like 0x13).\r\n"
"; The log lists every shop the game knows the first time you use the keybind.\r\n"
"; Away from the shopkeeper, Sora stands in for them in the shop (the game can only show a\r\n"
"; shopkeeper whose model is loaded where you are).\r\n"
"ShopId = Last\r\n"
"; 1 = only shops the story has already unlocked (you have met that shopkeeper).\r\n"
"VisitedOnly = 1\r\n"
"\r\n"
"[PartyMenu]\r\n"
"; Start in battle opens the party menu instead of the battle pause.\r\n"
"InBattle = 1\r\n"
"; The same in mission battles that have their own pause (outside cutscenes).\r\n"
"InMissions = 1\r\n"
"\r\n"
"[Log]\r\n"
"; ShroMagicMenu.log next to the game exe. Include it when reporting a problem.\r\n"
"Enabled = 1\r\n"
"; Also log every Start press in battle and battle / cutscene changes.\r\n"
"Verbose = 0\r\n";

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
    /* No FlushFileBuffers: what WriteFile has taken survives a game crash, and a disk flush per
       line runs on the game thread inside the hooks, where it can hitch a frame. */
    WriteFile(g_log, buf, (DWORD)strlen(buf), &written, NULL);
}
#define smm_logv(...) do { if (g_cfg.log_verbose) smm_log(__VA_ARGS__); } while (0)

/* path = the game exe's folder + name. 0 if it does not fit. */
static int smm_game_path(wchar_t *path, const wchar_t *name)
{
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *slash;
    if (len == 0 || len >= MAX_PATH) return 0;
    slash = wcsrchr(path, L'\\');
    if (!slash) return 0;
    slash[1] = 0;
    if (wcslen(path) + wcslen(name) + 1 >= MAX_PATH) return 0;
    wcscat_s(path, MAX_PATH, name);
    return 1;
}

static void smm_open_log(void)
{
    wchar_t path[MAX_PATH];
    if (!smm_game_path(path, L"ShroMagicMenu.log")) return;
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ---------------------------------------------------------------- ini */
#define SMM_INI_MAX 16384
static char g_ini_text[SMM_INI_MAX + 1];
static int g_ini_state = 0;          /* 0 read, 1 missing -> default written, 2 missing, could not write, 3 too big/unreadable, 4 UTF-16 */

static void smm_read_ini(void)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD got = 0;

    g_ini_text[0] = 0;
    g_ini_state = 0;
    if (!smm_game_path(path, L"ShroMagicMenu.ini")) { g_ini_state = 3; return; }
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (!ReadFile(h, g_ini_text, SMM_INI_MAX, &got, NULL)) got = 0;
        g_ini_text[got] = 0;
        if (got == SMM_INI_MAX) { g_ini_state = 3; g_ini_text[0] = 0; }   /* far too big for ours */
        /* Notepad's "Unicode" (UTF-16): every other byte is 0, so no line would parse. */
        else if (got >= 2 && (((BYTE)g_ini_text[0] == 0xFF && (BYTE)g_ini_text[1] == 0xFE) ||
                              ((BYTE)g_ini_text[0] == 0xFE && (BYTE)g_ini_text[1] == 0xFF) || g_ini_text[1] == 0)) {
            g_ini_state = 4; g_ini_text[0] = 0;
        }
        /* UTF-8 with a byte order mark (Notepad's "UTF-8 with BOM"): drop the mark. */
        else if (got >= 3 && (BYTE)g_ini_text[0] == 0xEF && (BYTE)g_ini_text[1] == 0xBB && (BYTE)g_ini_text[2] == 0xBF)
            memmove(g_ini_text, g_ini_text + 3, got - 3 + 1);
        CloseHandle(h);
        return;
    }
    /* No file: write the defaults so the player has something to edit. */
    memcpy(g_ini_text, SMM_DEFAULT_INI, sizeof(SMM_DEFAULT_INI));
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { g_ini_state = 2; return; }
    WriteFile(h, SMM_DEFAULT_INI, (DWORD)(sizeof(SMM_DEFAULT_INI) - 1), &got, NULL);
    CloseHandle(h);
    g_ini_state = 1;
}

static char *smm_trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int smm_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

static char g_save_name[40], g_pause_name[40];   /* "R1+Start" etc., for the log */
static char g_load_name[40], g_shop_name[40];

/* "R1+Start", "L2+R2+Up", "None". 1 = ok (trig 0 = none); 0 = error, reason in err. */
static int smm_parse_bind(const char *text, SmmBind *b, char *err, size_t errn)
{
    static const struct { const char *name; BYTE mod, trig; } KEYS[] = {
        { "L1", 0x04, 0 }, { "R1", 0x08, 0 }, { "L2", 0x01, 0 }, { "R2", 0x02, 0 },
        { "Start", 0, SMM_TRIG_START }, { "Up", 0, SMM_TRIG_UP }, { "Down", 0, SMM_TRIG_DOWN },
        { "Left", 0, SMM_TRIG_LEFT }, { "Right", 0, SMM_TRIG_RIGHT },
    };
    char buf[64], *p;
    b->mods = 0; b->trig = 0;
    if (strlen(text) >= sizeof(buf)) { _snprintf_s(err, errn, _TRUNCATE, "too long"); return 0; }
    strcpy_s(buf, sizeof(buf), text);
    p = smm_trim(buf);
    if (*p == 0 || smm_ieq(p, "None")) return 1;
    while (p) {
        char *plus = strchr(p, '+'), *tok;
        unsigned k;
        if (plus) *plus = 0;
        tok = smm_trim(p);
        for (k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++)
            if (smm_ieq(tok, KEYS[k].name)) break;
        if (k == sizeof(KEYS) / sizeof(KEYS[0])) {
            _snprintf_s(err, errn, _TRUNCATE, "unknown button \"%s\" (use L1 R1 L2 R2 Start Up Down Left Right)", tok);
            return 0;
        }
        if (KEYS[k].mod) {
            if (b->mods & KEYS[k].mod) { _snprintf_s(err, errn, _TRUNCATE, "%s listed twice", KEYS[k].name); return 0; }
            b->mods |= KEYS[k].mod;
        } else {
            if (b->trig) { _snprintf_s(err, errn, _TRUNCATE, "use only one of Start / Up / Down / Left / Right"); return 0; }
            b->trig = KEYS[k].trig;
        }
        p = plus ? plus + 1 : NULL;
    }
    if (!b->trig) { _snprintf_s(err, errn, _TRUNCATE, "needs Start or a D-pad direction (Up Down Left Right)"); return 0; }
    if (!b->mods) { _snprintf_s(err, errn, _TRUNCATE, "needs at least one shoulder button (L1 R1 L2 R2)"); return 0; }
    return 1;
}

static const char *smm_bind_text(const SmmBind *b, char *out, size_t n)
{
    static const struct { BYTE bit; const char *name; } MODS[] = { { 0x04, "L1" }, { 0x08, "R1" }, { 0x01, "L2" }, { 0x02, "R2" } };
    unsigned k;
    out[0] = 0;
    if (!b->trig) { strcpy_s(out, n, "None"); return out; }
    for (k = 0; k < 4; k++)
        if (b->mods & MODS[k].bit) { strcat_s(out, n, MODS[k].name); strcat_s(out, n, "+"); }
    strcat_s(out, n, b->trig == SMM_TRIG_START ? "Start" : b->trig == SMM_TRIG_UP ? "Up" :
                     b->trig == SMM_TRIG_DOWN ? "Down" : b->trig == SMM_TRIG_LEFT ? "Left" : "Right");
    return out;
}

/* "0x13", "13h" or "19". 0 = not a number in 0..0xFFFF. */
static int smm_parse_num(const char *v, int *out)
{
    unsigned long n = 0;
    int base = 10, digits = 0;
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) { base = 16; v += 2; }
    else { const char *e = v + strlen(v); if (e > v && (e[-1] == 'h' || e[-1] == 'H')) base = 16; }
    for (; *v; v++) {
        int d;
        if (*v >= '0' && *v <= '9') d = *v - '0';
        else if (base == 16 && *v >= 'a' && *v <= 'f') d = *v - 'a' + 10;
        else if (base == 16 && *v >= 'A' && *v <= 'F') d = *v - 'A' + 10;
        else if (base == 16 && (*v == 'h' || *v == 'H') && v[1] == 0) break;
        else return 0;
        n = n * base + d;
        if (n > 0xFFFF) return 0;
        digits++;
    }
    if (!digits) return 0;
    *out = (int)n;
    return 1;
}

static int smm_parse_flag(const char *v, int *out)
{
    if (smm_ieq(v, "1") || smm_ieq(v, "true") || smm_ieq(v, "yes") || smm_ieq(v, "on")) { *out = 1; return 1; }
    if (smm_ieq(v, "0") || smm_ieq(v, "false") || smm_ieq(v, "no") || smm_ieq(v, "off")) { *out = 0; return 1; }
    return 0;
}

/* Parse the ini text into g_cfg (starting from the defaults). quiet = no logging (first pass,
   before the log exists). Returns the number of problems. */
static int smm_load_settings(int quiet)
{
    static char work[SMM_INI_MAX + 1];
    char section[32] = "";
    char *line, *next;
    int problems = 0;

    g_cfg = SMM_DEFAULTS;
    memcpy(work, g_ini_text, sizeof(work));
    for (line = work; line && *line; line = next) {
        char *eq, *key, *val, *c;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        for (c = line; *c; c++) if (*c == ';' || *c == '#') { *c = 0; break; }
        line = smm_trim(line);
        if (*line == 0) continue;
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (end) *end = 0;
            strncpy_s(section, sizeof(section), smm_trim(line + 1), _TRUNCATE);
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) { if (!quiet) smm_log("Settings: ignored line \"%s\" (no '=')", line); problems++; continue; }
        *eq = 0;
        key = smm_trim(line);
        val = smm_trim(eq + 1);
        {
            int *flag = NULL, *num = NULL;
            SmmBind *bind = NULL;
            if (smm_ieq(section, "LoadMenu")) {
                if (smm_ieq(key, "Enabled")) flag = &g_cfg.load_on;
                else if (smm_ieq(key, "Keybind")) bind = &g_cfg.load_bind;
            } else if (smm_ieq(section, "RemoteShop")) {
                if (smm_ieq(key, "Enabled")) flag = &g_cfg.shop_on;
                else if (smm_ieq(key, "Keybind")) bind = &g_cfg.shop_bind;
                else if (smm_ieq(key, "ShopId")) { if (smm_ieq(val, "Last")) { g_cfg.shop_id = -1; continue; } num = &g_cfg.shop_id; }
                else if (smm_ieq(key, "VisitedOnly")) flag = &g_cfg.shop_visited_only;
            } else if (smm_ieq(section, "SaveMenu")) {
                if (smm_ieq(key, "Enabled")) flag = &g_cfg.save_on;
                else if (smm_ieq(key, "Keybind")) bind = &g_cfg.save_bind;
                else if (smm_ieq(key, "SaveInBattle")) flag = &g_cfg.save_in_battle;
            } else if (smm_ieq(section, "CustomPause")) {
                if (smm_ieq(key, "Enabled")) flag = &g_cfg.pause_on;
                else if (smm_ieq(key, "Keybind")) bind = &g_cfg.pause_bind;
                else if (smm_ieq(key, "ShowSave")) flag = &g_cfg.show_save;
                else if (smm_ieq(key, "ShowLoadGame")) flag = &g_cfg.show_load;
                else if (smm_ieq(key, "ShowJournal")) flag = &g_cfg.show_journal;
                else if (smm_ieq(key, "ForceWorldTravel")) flag = &g_cfg.force_world;
                else if (smm_ieq(key, "GameRetryPause")) flag = &g_cfg.game_retry;
            } else if (smm_ieq(section, "PartyMenu")) {
                if (smm_ieq(key, "InBattle")) flag = &g_cfg.party_battle;
                else if (smm_ieq(key, "InMissions")) flag = &g_cfg.party_missions;
            } else if (smm_ieq(section, "Log")) {
                if (smm_ieq(key, "Enabled")) flag = &g_cfg.log_on;
                else if (smm_ieq(key, "Verbose")) flag = &g_cfg.log_verbose;
            }
            if (num) {
                if (!smm_parse_num(val, num)) {
                    if (!quiet) smm_log("Settings: [%s] %s = \"%s\" is not a number (like 0x13), default kept", section, key, val);
                    problems++;
                }
            } else if (flag) {
                if (!smm_parse_flag(val, flag)) {
                    if (!quiet) smm_log("Settings: [%s] %s = \"%s\" is not 1 or 0, default kept", section, key, val);
                    problems++;
                }
            } else if (bind) {
                char err[96];
                SmmBind nb;
                if (smm_parse_bind(val, &nb, err, sizeof(err))) *bind = nb;
                else {
                    if (!quiet) smm_log("Settings: [%s] %s = \"%s\": %s. That keybind is OFF.", section, key, val, err);
                    bind->trig = 0;
                    problems++;
                }
            } else {
                if (!quiet) smm_log("Settings: unknown setting [%s] %s, ignored", section, key);
                problems++;
            }
        }
    }
    /* A keybind of None switches its feature off. */
    if (!g_cfg.save_bind.trig) g_cfg.save_on = 0;
    if (!g_cfg.pause_bind.trig) g_cfg.pause_on = 0;
    if (!g_cfg.load_bind.trig) g_cfg.load_on = 0;
    if (!g_cfg.shop_bind.trig) g_cfg.shop_on = 0;
    /* Two features on one keybind: the later one in this list is switched off. */
    {
        struct { const char *name; int *on; const SmmBind *b; } F[4] = {
            { "SaveMenu", &g_cfg.save_on, &g_cfg.save_bind }, { "CustomPause", &g_cfg.pause_on, &g_cfg.pause_bind },
            { "LoadMenu", &g_cfg.load_on, &g_cfg.load_bind }, { "RemoteShop", &g_cfg.shop_on, &g_cfg.shop_bind },
        };
        int i, j;
        for (j = 1; j < 4; j++)
            for (i = 0; i < j; i++)
                if (*F[i].on && *F[j].on && F[i].b->mods == F[j].b->mods && F[i].b->trig == F[j].b->trig) {
                    if (!quiet) smm_log("Settings: %s and %s have the same keybind; %s is OFF.", F[i].name, F[j].name, F[j].name);
                    *F[j].on = 0;
                    problems++;
                }
    }
    smm_bind_text(&g_cfg.save_bind, g_save_name, sizeof(g_save_name));
    smm_bind_text(&g_cfg.pause_bind, g_pause_name, sizeof(g_pause_name));
    smm_bind_text(&g_cfg.load_bind, g_load_name, sizeof(g_load_name));
    smm_bind_text(&g_cfg.shop_bind, g_shop_name, sizeof(g_shop_name));
    return problems;
}

/* ---------------------------------------------------------------- signatures */
typedef struct {
    const char *name;
    const char *sig;
    DWORD expected;
    DWORD found;
    int count;
} SmmTarget;

static SmmTarget g_targets[] = {
    { "OpenMenu",
      "80 3D ?? ?? ?? ?? 00 74 0F 39 0D ?? ?? ?? ?? 74 17 89 0D ?? ?? ?? ?? C3 "
      "89 0D ?? ?? ?? ?? C7 05 ?? ?? ?? ?? FF FF FF FF C3",
      0x151770, 0, 0 },
    /* +0x0E/+0x0F (the mission branch, 75 17) are wildcarded so the Lua patch (90 90)
       cannot break the scan. */
    { "StartCheck",
      "F6 40 08 02 74 2B E8 ?? ?? ?? ?? 48 85 C0 ?? ?? E8 ?? ?? ?? ?? 84 C0 75 18 "
      "BA FF FF FF FF 33 C9 E8",
      0x1572F9, 0, 0 },
    { "PauseSelect",
      "83 F9 05 75 05 E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? C1 E8 02 A8 01 74 0E "
      "F7 C6 F7 FF FF FF B8 0F 00 00 00 0F 44 F0 33 DB 4C 8D 25 ?? ?? ?? ??",
      0x2E4983, 0, 0 },
    /* End of the game's pause code: movsx ecx,[rbx+2]; call OpenMenu; call follow-up. */
    { "PauseTail",
      "0F BF 4B 02 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 7C 24 50",
      0x2E4AF6, 0, 0 },
    /* The pause's button handler: function number -> jump table (one case per function). */
    { "PauseButtons",
      "48 8B 0D ?? ?? ?? ?? 48 89 74 24 50 48 8D 34 1B 44 0F BF 04 0E 41 8D 48 FF 83 F9 12 "
      "0F 87 ?? ?? ?? ?? 48 63 C1 48 8D 15 ?? ?? ?? ?? 8B 8C 82 ?? ?? ?? ?? 48 03 CA FF E1",
      0x31DE82, 0, 0 },
    /* ForceWorldTravel only: cmp word [pause screen],1 / jne / mov ecx,<2> / call SetSaveMode.
       The 2 is wildcarded (KSX's script writes 3 there). */
    { "SaveMode",
      "66 83 3D ?? ?? ?? ?? 01 75 0A B9 ?? 00 00 00 E8",
      0x31D4F9, 0, 0 },
    /* RemoteShop only: the game's OpenShop(shop id), reached from its event code (exe+3AABE0,
       one case of its switch) when a shopkeeper is talked to:
       if no menu is open and menustate != 2: busy byte = 1, shop id = ecx, start the shop task. */
    { "OpenShop",
      "40 53 48 83 EC 20 8B D9 E8 ?? ?? ?? ?? 84 C0 75 27 83 3D ?? ?? ?? ?? 02 74 1E "
      "48 8D 0D ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01 89 1D ?? ?? ?? ?? 48 83 C4 20 5B E9",
      0x1518E0, 0, 0 },
    /* RemoteShop only: the shop UI's lookup of a shop entry (table pointer, then a 0x18-byte
       entry per shop: id, unlock flag, name, shopkeeper, ...) and its unlock-flag test. */
    { "ShopTable",
      "4C 8B 3D ?? ?? ?? ?? 44 8B C6 45 0F BF 4F 06 49 8D 5F 10 45 85 C9 0F 8E",
      0x2FBA5E, 0, 0 },
    /* LoadMenu: the pause's screen task (exe+31D3C0), once it has built the button page (its first
       block runs only when the current screen is 9) and before it runs the first screen:
       mov edx,1 / mov [flag],sil / mov rcx,rbx / call / cmp word [screen],4 / jne.
       (1.9 switched the screen at the end of the setup, before the task ran, so the button page
       was never built: backing out of Load then used stale objects and crashed.) */
    { "PauseScreenTask",
      "BA 01 00 00 00 40 88 35 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 66 83 3D ?? ?? ?? ?? 04 75",
      0x31D499, 0, 0 },
    /* RemoteShop only: the shop's 3D shopkeeper setup (exe+3E6120): model path from the object
       entry, then the model cache. The shop can only show a model that is already loaded. */
    { "ShopKeeperModel",
      "40 53 48 83 EC 20 48 8B D9 33 D2 48 8B 49 08 E8 ?? ?? ?? ?? 8B 13 48 8B C8 E8 ?? ?? ?? ?? 8B 4B 04 48 89 43 10 E8",
      0x3E6120, 0, 0 },
    /* RemoteShop only: its caller, which looks the object entry up by id. */
    { "ShopKeeperEntry",
      "48 85 FF 75 0A 8B CE E8 ?? ?? ?? ?? 48 8B F8 48 8B D7 8B CE E8",
      0x3E6428, 0, 0 },
    /* Data guard only: the game's raw pad lookup GetPad(n) = exe+BF3270 + n*0x58, the controller
       state KSX's scripts read (same bits as our exe+7133EA / 7133EB). */
    { "GetPad",
      "48 63 C1 48 8D 0D ?? ?? ?? ?? 48 6B C0 58 48 03 C1 C3",
      0x39B760, 0, 0 },
};
#define SMM_TARGET_COUNT (sizeof(g_targets) / sizeof(g_targets[0]))
enum { T_OPENMENU = 0, T_STARTCHECK = 1, T_PAUSESELECT = 2, T_PAUSETAIL = 3, T_BUTTONS = 4, T_SAVEMODE = 5,
       T_OPENSHOP = 6, T_SHOPTABLE = 7, T_PAUSESETUP = 8, T_KEEPERMODEL = 9, T_KEEPERENTRY = 10, T_GETPAD = 11 };

#define SMM_MAX_SIG 128

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
static int g_known_build = 0;          /* 1 = the PE header matches v1.0.0.7 Steam Global */
static DWORD g_code_lo = 0, g_code_hi = 0;   /* executable sections, for the crash report */

/* The exe's PE header, or NULL. */
static IMAGE_NT_HEADERS64 *smm_nt(void)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base;
    IMAGE_NT_HEADERS64 *nt;
    if (!g_base || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : NULL;
}

/* Log which exe this is and note the executable range. Known build = the signatures are first
   checked at the offsets in our notes (all proven unique on this build by the tests). */
static void smm_identify_build(void)
{
    IMAGE_NT_HEADERS64 *nt = smm_nt();
    IMAGE_SECTION_HEADER *sec;
    WORD i;
    if (!nt) return;
    g_known_build = nt->FileHeader.TimeDateStamp == SMM_KNOWN_STAMP && nt->OptionalHeader.SizeOfImage == SMM_KNOWN_SIZE;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (!g_code_lo || sec->VirtualAddress < g_code_lo) g_code_lo = sec->VirtualAddress;
        if (sec->VirtualAddress + sec->Misc.VirtualSize > g_code_hi) g_code_hi = sec->VirtualAddress + sec->Misc.VirtualSize;
    }
    smm_log("Game exe: build stamp %08X, image size %X%s", nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage,
            g_known_build ? " (v1.0.0.7 Steam Global, the build our notes are from)" : " (NOT the build our notes are from: full scan)");
}

static int smm_match_at(const BYTE *p, const BYTE *bytes, const BYTE *mask, int len)
{
    int k;
    for (k = 0; k < len; k++)
        if (mask[k] && p[k] != bytes[k]) return 0;
    return 1;
}

static void smm_scan_target(SmmTarget *t)
{
    BYTE bytes[SMM_MAX_SIG], mask[SMM_MAX_SIG];
    int len = smm_parse_sig(t->sig, bytes, mask);
    IMAGE_NT_HEADERS64 *nt = smm_nt();
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    t->count = 0;
    t->found = 0;
    if (len == 0 || !nt) return;
    /* Known build: the signature where our notes put it is enough (a full scan costs ~4 ms per
       signature, inside DllMain). Anything else falls through to the full scan. */
    if (g_known_build && t->expected && t->expected + len <= nt->OptionalHeader.SizeOfImage &&
        smm_match_at(g_base + t->expected, bytes, mask, len)) {
        t->found = t->expected;
        t->count = 1;
        return;
    }
    sec = IMAGE_FIRST_SECTION(nt);

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        BYTE *start, *end, *p;
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        start = g_base + sec->VirtualAddress;
        end = start + sec->Misc.VirtualSize;
        if (end - start < len) continue;
        for (p = start; p <= end - len; p++) {
            int k;
            if (p[0] != bytes[0]) continue;
            for (k = 1; k < len; k++)
                if (mask[k] && p[k] != bytes[k]) break;
            if (k == len) {
                if (t->count == 0) t->found = (DWORD)(p - g_base);
                t->count++;
                if (t->count > 1) return;
            }
        }
    }
}

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

/* ---------------------------------------------------------------- game state helpers */
static BYTE rd8(DWORD off) { return g_base[off]; }
static WORD rd16(DWORD off) { WORD v; memcpy(&v, g_base + off, 2); return v; }
static DWORD rd32(DWORD off) { DWORD v; memcpy(&v, g_base + off, 4); return v; }

static int smm_on_world_map(void) { return rd8(SMM_WORLD) == SMM_WORLD_MAP; }
static int smm_in_cutscene(void) { return (rd32(SMM_CUT_FLAGS) & 0x04) != 0 || rd16(SMM_CUT_PAUSE) != 0; }

/* KSX's Lua anywhere pause writes Save (function 1, text 8AAF) as the first option of slot 3. */
static int g_pause_phase;            /* 0 idle, 1 waiting for the pause to start, 2 waiting for it to close */
static int smm_lua_pause_armed(void)
{
    return g_pause_phase == 0 && rd8(SMM_PAUSE_BLOCK - 2) != 0 && rd16(SMM_PAUSE_BLOCK) == 4 &&
           rd16(SMM_PAUSE_BLOCK + 2) == 0x0001 && rd16(SMM_PAUSE_BLOCK + 4) == 0x8AAF;
}

/* ---------------------------------------------------------------- custom pause: addresses */
/* Every pause is fed by a "pause slot": five 0x40-byte slots (slot 1 = battle pause, slot 3 = the
   one KSX's script fills, slot 4 = cutscenes). The custom pause fills slot 3 and calls the game's
   special-pause routine with it, which is what Start does when a slot is active. All of it is
   found in the game's own code at install (smm_resolve_pause). */
typedef void (SMM_GAME_ABI *SmmSpecialFn)(void *slot, void *arg);
typedef void *(SMM_GAME_ABI *SmmPtrFn)(void);
static SmmSpecialFn g_special_open = NULL;   /* the game's special-pause routine (exe+2E4C20) */
static SmmPtrFn g_lookup = NULL;             /* the special-pause lookup (exe+3AD540): active slot or 0 */
static DWORD g_pause_slot = 0;               /* the slot our pause uses this time: slot 3, or a free one */
static DWORD g_slot1 = SMM_MENU_BLOCK;       /* pause slot 1, the battle pause (exe+2A114E0) */
static DWORD g_screen_cur = 0;               /* word: the pause's current screen (-1 = no pause) */
static int g_pause_ok = 0;

/* What the custom pause writes, mirroring KSX's "Save, Load, World Map Anywhere": the slot
   (KSX's layout), his second option table, the cutscene table and "loadingheart".
   rel_slot: 1 = relative to the slot in use, 2 = relative to slot 4, 0 = Steam Global data address. */
typedef struct { int rel_slot; DWORD addr; BYTE size; DWORD value; } SmmWrite;
static const SmmWrite PAUSE_WRITES[] = {
    { 1, 0x00, 1, 1 },                                       /* slot: active */
    { 1, 0x02, 2, 4 },                                       /*   option count (set from the settings) */
    { 1, 0x04, 2, 0 }, { 1, 0x06, 2, 0 },                    /*   4 (function, text) pairs, from the settings */
    { 1, 0x08, 2, 0 }, { 1, 0x0A, 2, 0 },
    { 1, 0x0C, 2, 0 }, { 1, 0x0E, 2, 0 },
    { 1, 0x10, 2, 0 }, { 1, 0x12, 2, 0 },
    { 1, 0x16, 1, 15 },                                      /*   +0x16 word, read by exe+31F21B; KSX writes 15 */
    /* The rest of the slot is zeroed: a mission pause that used this slot leaves its own fields
       behind when it ends (seen after the Sephiroth fight: +0x18 = 804F8D02, +0x24 = 3), and the
       pause then hangs on them. A clean slot is the known-good state. All put back afterwards. */
    { 1, 0x14, 2, 0 }, { 1, 0x17, 1, 0 },
    { 1, 0x18, 4, 0 }, { 1, 0x1C, 4, 0 }, { 1, 0x20, 4, 0 }, { 1, 0x24, 4, 0 },
    { 1, 0x28, 4, 0 }, { 1, 0x2C, 4, 0 }, { 1, 0x30, 4, 0 }, { 1, 0x34, 4, 0 },
    { 1, 0x38, 4, 0 }, { 1, 0x3C, 4, 0 },
    { 0, 0x2AB275A, 2, 4 },                                  /* second option table */
    { 0, 0x2AB275C, 2, 0x0002 }, { 0, 0x2AB275E, 2, 0x8AB4 },
    { 0, 0x2AB2760, 2, 0x0001 }, { 0, 0x2AB2762, 2, 0x8AAF },
    { 0, 0x2AB2764, 2, 0x0011 }, { 0, 0x2AB2766, 2, 0xC52C },
    { 0, 0x2AB2768, 2, 0x0004 }, { 0, 0x2AB276A, 2, 0x8AB2 },
    { 2, 0x02, 2, 2 },                                       /* cutscene option table (slot 4, exe+2A115A2) */
    { 2, 0x04, 2, 0x0001 }, { 2, 0x06, 2, 0x8AAF },
    { 2, 0x08, 2, 0x0011 }, { 2, 0x0A, 2, 0xC52C },
    { 0, 0x8EC5B0, 4, 0x44960000 },                          /* "loadingheart" = 1200.0f (KSX writes it) */
};
#define PAUSE_WRITE_COUNT (sizeof(PAUSE_WRITES) / sizeof(PAUSE_WRITES[0]))
static DWORD g_pause_orig[PAUSE_WRITE_COUNT];
static DWORD g_pause_val[PAUSE_WRITE_COUNT];     /* the values written this time */
static int g_pause_frames = 0;

/* Pause buttons: [slot+4+i*4] = function, [slot+6+i*4] = text, read by the builder exe+31F3A0.
   The slot holds 4 (a 5th pair would land on the +0x16 word). Retry (3), game-over Continue (4)
   and Prepare & Retry (0x0C) only work from the Game Over screen: 4 crashed from a pause. */
/* Function 1 (text 8AAF, shown as "Load Game") asks the pause for screen 3, the load screen;
   function 0x11 (text C52C, shown as "Save") asks for screen 1, the save point menu. Before 1.9
   these two names were swapped here, so ShowSave hid the Load Game button and vice versa. */
#define SMM_FN_LOAD      0x0001
#define SMM_FN_SAVE      0x0011
#define SMM_SCREEN_LOAD  3
#define SMM_FN_JOURNAL   0x000A
#define SMM_FN_CONTINUE  0x0006

/* rel_slot 1 = the slot in use, 2 = slot 4 (cutscenes), 0 = a fixed data address (shifted by the
   data guard's delta). */
static DWORD pw_addr(const SmmWrite *w)
{
    return w->rel_slot == 1 ? g_pause_slot + w->addr : w->rel_slot == 2 ? g_slot3 + 0x40 + w->addr : DA(w->addr);
}
static DWORD pw_rd(const SmmWrite *w)
{
    DWORD v = 0;
    memcpy(&v, g_base + pw_addr(w), w->size);
    return v;
}
static void pw_wr(const SmmWrite *w, DWORD v) { memcpy(g_base + pw_addr(w), &v, w->size); }

static int smm_flag(unsigned n);
#define SMM_FLAG_JOURNAL 0x73

static void smm_pause_fill(void)
{
    WORD pairs[8] = { 0 };
    unsigned i, n = 0;
    /* Shown in this order: Load Game, Save (as before 1.9). */
    if (g_cfg.show_load)    { pairs[n * 2] = SMM_FN_LOAD;    pairs[n * 2 + 1] = 0x8AAF; n++; }
    /* No Save during a battle unless SaveInBattle = 1. */
    if (g_cfg.show_save && (g_cfg.save_in_battle || rd8(SMM_BTLTYP) == 0)) { pairs[n * 2] = SMM_FN_SAVE; pairs[n * 2 + 1] = 0xC52C; n++; }
    /* Jiminy's Journal only once the story has unlocked it (flag 0x73, Save+239E bit 8). */
    if (g_cfg.show_journal && smm_flag(SMM_FLAG_JOURNAL)) { pairs[n * 2] = SMM_FN_JOURNAL; pairs[n * 2 + 1] = 0x8AB7; n++; }
    pairs[n * 2] = SMM_FN_CONTINUE; pairs[n * 2 + 1] = 0x8AB4; n++;
    for (i = 0; i < PAUSE_WRITE_COUNT; i++) g_pause_val[i] = PAUSE_WRITES[i].value;
    g_pause_val[1] = n;
    for (i = 0; i < 8; i++) g_pause_val[2 + i] = pairs[i];
    for (i = 0; i < PAUSE_WRITE_COUNT; i++) { g_pause_orig[i] = pw_rd(&PAUSE_WRITES[i]); pw_wr(&PAUSE_WRITES[i], g_pause_val[i]); }
}
/* Put back only what still holds our value; anything the game changed is left alone. */
static void smm_pause_restore(const char *why)
{
    unsigned i, kept = 0;
    for (i = 0; i < PAUSE_WRITE_COUNT; i++) {
        if (pw_rd(&PAUSE_WRITES[i]) == g_pause_val[i]) pw_wr(&PAUSE_WRITES[i], g_pause_orig[i]);
        else if (PAUSE_WRITES[i].addr == 0x8EC5B0 && !PAUSE_WRITES[i].rel_slot) {}   /* the game always resets "loadingheart" */
        else { kept++; smm_log("  left alone: exe+%06X = %X (we wrote %X)", pw_addr(&PAUSE_WRITES[i]), pw_rd(&PAUSE_WRITES[i]), g_pause_val[i]); }
    }
    smm_log("Custom pause closed, values restored (%s)%s", why, kept ? "; some changed by the game were left alone" : "");
}

static int smm_resolve_pause(void)
{
    DWORD S, at, opener, special, lookup, h, table, load_case, req_fn;
    INT32 v;
    static const BYTE special_path[6] = { 0x33, 0xD2, 0x48, 0x8B, 0xC8, 0xE8 };  /* xor edx,edx; mov rcx,rax; call */
    static const BYTE lookup_head[14] = { 0x48, 0x83, 0xEC, 0x28, 0xB9, 0x05, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x20 };
    static const BYTE case_load[6] = { 0xB9, 0x01, 0x00, 0x00, 0x00, 0xE8 };   /* mov ecx,1 ; call */
    static const BYTE req_head[3] = { 0x66, 0x89, 0x0D };                      /* mov [rip+d],cx */

    if (g_targets[T_BUTTONS].count != 1 || g_targets[T_PAUSESELECT].count != 1) {
        smm_log("Custom pause unavailable: the pause code was not found.");
        return 0;
    }
    S = g_targets[T_STARTCHECK].found;

    /* The game's pause opener: the call right after the Start hook site; the PauseSelect
       signature sits 0x33 bytes into it. */
    at = S + SMM_PAUSEOPEN_CALL;
    if (g_base[at] != 0xE8) { smm_log("Custom pause unavailable: no call after the Start site."); return 0; }
    opener = smm_rip(at, 1, 5);
    if (opener + 0x33 != g_targets[T_PAUSESELECT].found) {
        smm_log("Custom pause unavailable: the call after the Start site is not the pause opener.");
        return 0;
    }
    /* The special-pause routine: the call on the Start code's special path; it ends by jumping
       to the pause opener (+0x7D). */
    if (memcmp(g_base + S + SMM_MISSION_OFF, special_path, 6) != 0) {
        smm_log("Custom pause unavailable: unexpected special-pause path.");
        return 0;
    }
    special = smm_rip(S + SMM_MISSION_OFF + 5, 1, 5);
    if (g_base[special + 0x7D] != 0xE9 || smm_rip(special + 0x7D, 1, 5) != opener) {
        smm_log("Custom pause unavailable: exe+%06X does not end in the pause opener.", special);
        return 0;
    }

    /* The pause slots: the special-pause lookup (called at StartCheck+6) scans five 0x40-byte
       slots from 'lea rdx,[slots]'. */
    lookup = smm_rip(S + 0x06, 1, 5);
    if (memcmp(g_base + lookup, lookup_head, 14) != 0 || g_base[lookup + 0x15] != 0x48 ||
        g_base[lookup + 0x16] != 0x8D || g_base[lookup + 0x17] != 0x15) {
        smm_log("Custom pause unavailable: unexpected special-pause lookup at exe+%06X.", lookup);
        return 0;
    }

    /* The pause's current-screen word, to know when the pause has closed: the Load Game button's
       request function writes the request word; the current screen is the word before it. */
    h = g_targets[T_BUTTONS].found;
    memcpy(&v, g_base + h + 0x2F, 4); table = (DWORD)v;
    memcpy(&v, g_base + table + 0x10 * 4, 4); load_case = (DWORD)v;
    if (memcmp(g_base + load_case, case_load, 6) != 0) {
        smm_log("Custom pause unavailable: the Save button case is not 'mov ecx,1 / call'.");
        return 0;
    }
    req_fn = smm_rip(load_case + 5, 1, 5);
    if (memcmp(g_base + req_fn, req_head, 3) != 0 || g_base[req_fn + 7] != 0xC3) {
        smm_log("Custom pause unavailable: unexpected request function at exe+%06X.", req_fn);
        return 0;
    }

    g_special_open = (SmmSpecialFn)(g_base + special);
    g_lookup = (SmmPtrFn)(g_base + lookup);
    g_slot3 = smm_rip(lookup + 0x15, 3, 7) + 3 * 0x40;
    g_slot1 = g_slot3 - 2 * 0x40;
    g_screen_cur = smm_rip(req_fn, 3, 7) - 4;
    if (g_slot1 != SMM_MENU_BLOCK)
        smm_log("Note: battle pause slot is exe+%06X, not exe+%06X as in our notes; using the derived one.", g_slot1, SMM_MENU_BLOCK);
    smm_log("Custom pause: special-pause routine exe+%06X (expected exe+2E4C20), slot 3 exe+%06X (expected exe+2A11560),",
            special, g_slot3);
    smm_log("              battle pause slot 1 exe+%06X (expected exe+2A114E0)", g_slot1);
    smm_log("              pause screen word exe+%06X (expected exe+744C10)", g_screen_cur);
    return 1;
}

/* ---------------------------------------------------------------- ForceWorldTravel */
/* exe+31D4F9: cmp word [screen],1 / jne / mov ecx,2 / call SetSaveMode (writes exe+BEDE90).
   The save menu greys out World Map when that mode is 2. KSX's "savemenu" byte is the 2. */
static DWORD g_force_imm = 0;        /* exe+31D504 */
static DWORD g_save_mode = 0;        /* exe+BEDE90 */

static int smm_resolve_force(void)
{
    DWORD f, setter;
    if (g_targets[T_SAVEMODE].count != 1) { smm_log("ForceWorldTravel unavailable: the save-menu mode code was not found."); return 0; }
    f = g_targets[T_SAVEMODE].found;
    if (smm_rip(f, 3, 8) != g_screen_cur) { smm_log("ForceWorldTravel unavailable: exe+%06X does not test the pause screen.", f); return 0; }
    setter = smm_rip(f + 0x0F, 1, 5);
    if (g_base[setter] != 0x89 || g_base[setter + 1] != 0x0D || g_base[setter + 6] != 0xC3) {
        smm_log("ForceWorldTravel unavailable: unexpected save-mode setter at exe+%06X.", setter);
        return 0;
    }
    g_force_imm = f + 0x0B;
    g_save_mode = smm_rip(setter, 2, 6);
    return 1;
}

static void smm_apply_force(void)
{
    BYTE *p = g_base + g_force_imm;
    DWORD old;
    if (*p == 3) { smm_log("ForceWorldTravel: already applied (KSX's script?)"); return; }
    if (*p != 2) { smm_log("ForceWorldTravel unavailable: exe+%06X holds %u, expected 2.", g_force_imm, *p); g_cfg.force_world = 0; return; }
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) { smm_log("ForceWorldTravel: could not write exe+%06X.", g_force_imm); g_cfg.force_world = 0; return; }
    *p = 3;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    smm_log("ForceWorldTravel ON: save-menu mode exe+%06X 2 -> 3, mode variable exe+%06X. (Can softlock mid-story.)",
            g_force_imm, g_save_mode);
}

/* ---------------------------------------------------------------- story flags, remote shop */
/* The game's flag bit array (Save+0x2390, exe+9ABC40), read by its flag test exe+3E38F0: bit n =
   dword [array + (n>>5)*4] bit (n&31). The "menu flags" of the plando wiki are these bits, e.g.
   0x70 Items, 0x71 Party, 0x72 Customize, 0x73 Jiminy's Journal (Save+239E), and each shop's
   unlock flag. Found through the shop code; 0 = unknown (then nothing is hidden or refused). */
static DWORD g_flags = 0;

static int smm_flag(unsigned n) { return g_flags ? (rd32(g_flags + (n >> 5) * 4) >> (n & 31)) & 1 : 1; }

typedef void (SMM_GAME_ABI *SmmShopFn)(int);
static SmmShopFn g_open_shop = NULL;   /* OpenShop(id), exe+1518E0 */
static DWORD g_shop_var = 0;           /* the open shop's id, -1 = none (exe+716B50) */
static DWORD g_shop_table = 0;         /* qword: pointer to the loaded shop table (exe+BEE968) */
static int g_shop_ok = 0, g_shop_listed = 0;

/* The shop shows its shopkeeper as a 3D model (exe+3E63D0 -> 3E6120): object entry by id
   (exe+3DFEB0), model path "obj/<name>.mdlx" (exe+3E0B50), then the model cache (exe+39E1E0,
   via exe+39CC40). It cannot load a model: a shopkeeper that is not loaded where the player
   stands crashes the game (stack overflow, seen in 1.9-test2 from the room next to the
   moogle). Object id 0x236 is the game's "current player" (Sora's model for this world, via
   exe+3DFEB0's first special case), which is always loaded. */
typedef void *(SMM_GAME_ABI *SmmEntryFn)(int);
typedef char *(SMM_GAME_ABI *SmmPathFn)(void *entry, char *buf);
typedef void *(SMM_GAME_ABI *SmmFindFn)(const char *name, int tag);
static SmmEntryFn g_obj_entry = NULL;
static SmmPathFn g_obj_path = NULL;
static SmmFindFn g_res_find = NULL;
#define SMM_KEEPER_FLAG   0x20000000   /* the shop passes shopkeeper | this to the model code */
#define SMM_OBJ_PLAYER    0x236
static BYTE *g_keeper_swapped = NULL;  /* shop entry whose shopkeeper we replaced, until the shop closes */
static WORD g_keeper_orig = 0;

/* 1 = the shop's model code would find this object's model already loaded. */
static int smm_model_loaded(int obj)
{
    void *e;
    char *path;
    if (!g_obj_entry) return 0;
    e = g_obj_entry(obj | SMM_KEEPER_FLAG);
    if (!e) return 0;
    path = g_obj_path(e, NULL);
    return path && g_res_find(path, -1) != NULL;
}

static void smm_keeper_restore(const char *why)
{
    if (!g_keeper_swapped) return;
    if ((g_keeper_swapped[6] | g_keeper_swapped[7] << 8) == SMM_OBJ_PLAYER) {
        g_keeper_swapped[6] = (BYTE)g_keeper_orig; g_keeper_swapped[7] = (BYTE)(g_keeper_orig >> 8);
    }
    g_keeper_swapped = NULL;
    smm_logv("Shopkeeper put back (%s)", why);
}

/* ---- last moogle used: recorded when the game opens a moogle's shop, kept in a small file */
#define SMM_DEFAULT_SHOP  0x13         /* the Hollow Bastion moogle, until one has been talked to */
static int g_last_shop = -1;
static int g_shop_ours = 0;            /* 1 while our keybind is calling OpenShop */
static DWORD g_openshop_hook = 0;      /* exe+1518E0, 0 = not hooked (no tracking) */
static void smm_read_last_shop(void)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    char buf[64];
    DWORD got = 0;
    int v;
    if (!smm_game_path(path, L"ShroMagicMenu-lastshop.txt")) return;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL)) {
        buf[got] = 0;
        { char *nl = strpbrk(buf, "\r\n"); if (nl) *nl = 0; }
        if (smm_parse_num(smm_trim(buf), &v)) g_last_shop = v;
    }
    CloseHandle(h);
}
static void smm_write_last_shop(int id)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    char buf[32];
    DWORD n;
    if (!smm_game_path(path, L"ShroMagicMenu-lastshop.txt")) return;
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    n = (DWORD)_snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%X\r\n", id);
    WriteFile(h, buf, n, &n, NULL);
    CloseHandle(h);
}

/* Moogles are the shops whose shopkeeper is a moogle object: 0x487 (every world's moogle) or
   0x50F (Hollow Bastion's). Seen in the shop table: these are the shops that offer synthesis. */
static int smm_is_moogle(WORD keeper) { return keeper == 0x487 || keeper == 0x50F; }

static const BYTE *smm_shop_entry(int id, int *count);

/* exe+1518E0, OpenShop(id): every shop the game opens, a shopkeeper's or ours. */
static void SMM_GAME_ABI smm_on_open_shop(int id)
{
    int count;
    const BYTE *e;
    if (g_shop_ours || !g_shop_table) return;
    e = smm_shop_entry(id, &count);
    if (!e) return;
    if (!smm_is_moogle((WORD)(e[6] | e[7] << 8))) { smm_logv("Shop 0x%X opened (not a moogle)", id); return; }
    if (id != g_last_shop) {
        g_last_shop = id;
        smm_write_last_shop(id);
        smm_log("Moogle shop 0x%X opened (world %02X room %02X): remembered for the shop keybind", id, rd8(SMM_WORLD), rd8(SMM_WORLD + 1));
    }
}

static int smm_resolve_shop(void)
{
    DWORD f, t, test;
    static const BYTE test_head[11] = { 0x8B, 0xC1, 0x8B, 0xD1, 0x48, 0xC1, 0xE8, 0x05, 0x48, 0x8D, 0x0D };
    if (g_targets[T_SHOPTABLE].count != 1) { smm_log("Story flags / remote shop: the shop table code was not found."); return 0; }
    t = g_targets[T_SHOPTABLE].found;
    if (g_base[t + 0x49] != 0xE8) { smm_log("Story flags / remote shop: no flag test call in the shop code."); return 0; }
    test = smm_rip(t + 0x49, 1, 5);
    if (memcmp(g_base + test, test_head, 11) != 0) { smm_log("Story flags / remote shop: unexpected flag test at exe+%06X.", test); return 0; }
    g_flags = smm_rip(test + 8, 3, 7);
    g_shop_table = smm_rip(t, 3, 7);
    smm_log("Story flags: flag array exe+%06X (expected exe+9ABC40); shop table pointer exe+%06X (expected exe+BEE968)",
            g_flags, g_shop_table);
    if (g_targets[T_OPENSHOP].count != 1) { smm_log("Remote shop unavailable: OpenShop was not found."); return 0; }
    f = g_targets[T_OPENSHOP].found;
    if (smm_rip(f + 0x11, 2, 7) != DA(0x716884)) {
        smm_log("Remote shop unavailable: OpenShop does not test menustate at exe+716884.");
        return 0;
    }
    g_shop_var = smm_rip(f + 0x28, 2, 6);
    g_open_shop = (SmmShopFn)(g_base + f);
    smm_log("Remote shop: OpenShop exe+%06X, shop id variable exe+%06X (expected exe+716B50)", f, g_shop_var);
    /* The shopkeeper model check. Without it the remote shop crashes away from the shopkeeper. */
    if (g_targets[T_KEEPERMODEL].count != 1 || g_targets[T_KEEPERENTRY].count != 1) {
        smm_log("Remote shop unavailable: the shopkeeper model code was not found.");
        return 0;
    }
    {
        DWORD m = g_targets[T_KEEPERMODEL].found, en = g_targets[T_KEEPERENTRY].found;
        DWORD path = smm_rip(m + 0x0F, 1, 5), load = smm_rip(m + 0x19, 1, 5), entry = smm_rip(en + 0x07, 1, 5), find;
        static const BYTE load_head[6] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20 };
        /* exe+39CC40 starts with push rbx / sub rsp,20h / call <cache lookup>. */
        if (memcmp(g_base + load, load_head, 6) != 0 || g_base[load + 6] != 0xE8) {
            smm_log("Remote shop unavailable: unexpected shopkeeper model code at exe+%06X.", m);
            return 0;
        }
        find = smm_rip(load + 6, 1, 5);
        g_obj_entry = (SmmEntryFn)(g_base + entry);
        g_obj_path = (SmmPathFn)(g_base + path);
        g_res_find = (SmmFindFn)(g_base + find);
        smm_log("Remote shop: object entry exe+%06X (expected exe+3DFEB0), model path exe+%06X (expected exe+3E0B50),", entry, path);
        smm_log("             model cache lookup exe+%06X (expected exe+39E1E0)", find);
    }
    return 1;
}

/* The loaded shop table: word count at +6, entries from +0x10, 0x18 bytes each
   (+0 shop id, +2 unlock flag, +4 name text, +6 shopkeeper). NULL if not loaded yet. */
static const BYTE *smm_shop_entry(int id, int *count)
{
    UINT64 p;
    const BYTE *tab;
    int n, i;
    memcpy(&p, g_base + g_shop_table, 8);
    *count = 0;
    if (!p) return NULL;
    tab = (const BYTE *)(UINT_PTR)p;
    n = (INT16)(tab[6] | tab[7] << 8);
    if (n <= 0 || n > 512) return NULL;
    *count = n;
    for (i = 0; i < n; i++) {
        const BYTE *e = tab + 0x10 + i * 0x18;
        if ((INT16)(e[0] | e[1] << 8) == id) return e;
    }
    return NULL;
}

static void smm_list_shops(void)
{
    UINT64 p;
    const BYTE *tab;
    int n, i;
    char line[400];
    memcpy(&p, g_base + g_shop_table, 8);
    if (!p) return;
    tab = (const BYTE *)(UINT_PTR)p;
    n = (INT16)(tab[6] | tab[7] << 8);
    if (n <= 0 || n > 512) return;
    g_shop_listed = 1;
    smm_log("Shops the game knows (id: unlock flag, unlocked?, name text, shopkeeper):");
    line[0] = 0;
    for (i = 0; i < n; i++) {
        const BYTE *e = tab + 0x10 + i * 0x18;
        WORD id = (WORD)(e[0] | e[1] << 8), fl = (WORD)(e[2] | e[3] << 8), nm = (WORD)(e[4] | e[5] << 8), kp = (WORD)(e[6] | e[7] << 8);
        char one[48];
        _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%X: %X %s %X %X", line[0] ? " | " : "  ", id, fl, smm_flag(fl) ? "yes" : "no", nm, kp);
        strcat_s(line, sizeof(line), one);
        if (strlen(line) > 300 || i == n - 1) { smm_log("%s", line); line[0] = 0; }
    }
}

/* ---------------------------------------------------------------- D-pad keybinds: the game's own gate */
/* In front of the Start code (StartCheck - 0x39 .. +0x15) the game runs five tests before it
   accepts Start. D-pad keybinds call the same functions and only act when all pass. */
typedef BYTE (SMM_GAME_ABI *SmmBoolFn)(void);
typedef BYTE *(SMM_GAME_ABI *SmmBytePtrFn)(void);
static DWORD g_gate_dw = 0, g_gate_b = 0;
static SmmBoolFn g_gate_f1 = NULL, g_gate_f3 = NULL, g_gate_f4 = NULL;
static SmmBytePtrFn g_gate_f2 = NULL;
static int g_gate_ok = 0;

static int smm_resolve_gate(void)
{
    DWORD S = g_targets[T_STARTCHECK].found;
    static const BYTE cmp108[7] = { 0x83, 0xB8, 0x08, 0x01, 0x00, 0x00, 0x00 };
    BYTE *s = g_base + S;
    if (!g_lookup) { smm_log("D-pad keybinds unavailable: needs the custom pause code."); return 0; }
    if (s[-0x39] != 0x83 || s[-0x38] != 0x3D || s[-0x33] != 0x00 || s[-0x32] != 0x75 ||
        s[-0x30] != 0x80 || s[-0x2F] != 0x3D || s[-0x2A] != 0x00 || s[-0x29] != 0x74 ||
        s[-0x27] != 0xE8 || s[-0x22] != 0x84 || s[-0x21] != 0xC0 ||
        s[-0x1E] != 0xE8 || memcmp(s - 0x19, cmp108, 7) != 0 ||
        s[-0x10] != 0xE8 || s[-0x0B] != 0x84 || s[-0x0A] != 0xC0 ||
        s[0x10] != 0xE8 || s[0x15] != 0x84 || s[0x16] != 0xC0) {
        smm_log("D-pad keybinds unavailable: the checks in front of the Start code look different.");
        return 0;
    }
    g_gate_dw = smm_rip(S - 0x39, 2, 7);
    g_gate_b = smm_rip(S - 0x30, 2, 7);
    g_gate_f1 = (SmmBoolFn)(g_base + smm_rip(S - 0x27, 1, 5));
    g_gate_f2 = (SmmBytePtrFn)(g_base + smm_rip(S - 0x1E, 1, 5));
    g_gate_f3 = (SmmBoolFn)(g_base + smm_rip(S - 0x10, 1, 5));
    g_gate_f4 = (SmmBoolFn)(g_base + smm_rip(S + 0x10, 1, 5));
    smm_log("D-pad keybinds: Start gate exe+%06X / exe+%06X / exe+%06X / exe+%06X / exe+%06X / exe+%06X",
            g_gate_dw, g_gate_b, (unsigned)((BYTE *)g_gate_f1 - g_base), (unsigned)((BYTE *)g_gate_f2 - g_base),
            (unsigned)((BYTE *)g_gate_f3 - g_base), (unsigned)((BYTE *)g_gate_f4 - g_base));
    return 1;
}

/* ---------------------------------------------------------------- actions */
typedef void (SMM_GAME_ABI *SmmMenuFn)(int);
static SmmMenuFn g_open_menu = NULL;     /* OpenMenu(id) */
static SmmMenuFn g_menu_follow = NULL;   /* the call the pause code makes right after OpenMenu */

/* 1 = opened. */
static int smm_do_save(const char *how)
{
    if (smm_on_world_map()) { smm_logv("%s: not on the World Map", how); return 0; }
    if (!g_cfg.save_in_battle && rd8(SMM_BTLTYP) != 0) {
        smm_log("%s: no saving during a battle (SaveInBattle = 0)", how);
        return 0;
    }
    if (g_cfg.force_world && g_save_mode && rd32(g_save_mode) == 2) {
        DWORD three = 3;
        memcpy(g_base + g_save_mode, &three, 4);     /* the setter's own value with ForceWorldTravel */
    }
    smm_log("%s: opening the save point menu", how);
    g_open_menu(SMM_SAVE_MENU);
    g_menu_follow(SMM_SAVE_MENU);
    return 1;
}

/* Verbose diagnostics: the game's pause state when the custom pause opens.
   Steam Global addresses from the pause code: screen 744C10/744C14, pause-active and flag bytes
   BEF508-BEF50B, slot pointer BEF4A0/BEF4A8, opener busy byte 9A8738, mode bits 749830, the
   pause's objects BEF488-BEF4B8 and the Game Over screen's objects BEF510-BEF550. */
static void smm_log_pause_state(const char *what)
{
    UINT64 q[7];
    unsigned i;
    static const DWORD objs[7] = { 0xBEF510, 0xBEF518, 0xBEF520, 0xBEF528, 0xBEF530, 0xBEF538, 0xBEF550 };
    UINT64 p88, p90, p98, pb0, pa0, pa8;
    for (i = 0; i < 7; i++) memcpy(&q[i], g_base + DA(objs[i]), 8);
    memcpy(&p88, g_base + DA(0xBEF488), 8); memcpy(&p90, g_base + DA(0xBEF490), 8); memcpy(&p98, g_base + DA(0xBEF498), 8);
    memcpy(&pb0, g_base + DA(0xBEF4B0), 8); memcpy(&pa0, g_base + DA(0xBEF4A0), 8); memcpy(&pa8, g_base + DA(0xBEF4A8), 8);
    smm_log("%s: screen %d req %d | BEF508-B %u %u %u %u | 9A8738 %u | 749830 %08X | menu-open %u cur %d | BtlTyp %u world %02X",
            what, (int)(INT16)rd16(g_screen_cur), (int)(INT16)rd16(g_screen_cur + 4), rd8(DA(0xBEF508)), rd8(DA(0xBEF509)),
            rd8(DA(0xBEF50A)), rd8(DA(0xBEF50B)), rd8(DA(0x9A8738)), rd32(DA(0x749830)), rd8(DA(0x9006B0)), (int)rd32(DA(0x900724)),
            rd8(SMM_BTLTYP), rd8(SMM_WORLD));
    smm_log("%s: slotptr %llX/%llX | pause objs 488 %llX 490 %llX 498 %llX 4B0 %llX", what,
            (unsigned long long)pa0, (unsigned long long)pa8, (unsigned long long)p88, (unsigned long long)p90,
            (unsigned long long)p98, (unsigned long long)pb0);
    {
        char hex[0x40 * 3 + 1];
        int k, which;
        for (which = 0; which < 2; which++) {
            DWORD at = which ? g_slot1 : (g_pause_slot ? g_pause_slot : g_slot3);
            for (k = 0; k < 0x40; k++) _snprintf_s(hex + k * 3, 4, _TRUNCATE, "%02X ", g_base[at + k]);
            smm_log("%s: %s exe+%06X: %s", what, which ? "battle slot 1" : "our slot   ", at, hex);
        }
    }
    smm_log("%s: game-over objs 510 %llX 518 %llX 520 %llX 528 %llX 530 %llX 538 %llX 550 %llX", what,
            (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2], (unsigned long long)q[3],
            (unsigned long long)q[4], (unsigned long long)q[5], (unsigned long long)q[6]);
}

/* The special pause slot the game picked on the last Start press / D-pad check (0 = none). */
static BYTE *g_last_obj = NULL;

/* Does this pause slot offer the game's Retry (function 3) or Prepare & Retry (0x0C)? The game
   arms such a slot (vanilla: Continue / Retry / Prepare & Retry / ...) in fights it can restart. */
static int smm_slot_offers_retry(const BYTE *slot)
{
    unsigned i, n = slot[2];
    if (n > 4) n = 4;
    for (i = 0; i < n; i++) {
        WORD fn;
        memcpy(&fn, slot + 4 + i * 4, 2);
        if (fn == 0x0003 || fn == 0x000C) return 1;
    }
    return 0;
}

/* LoadMenu: set just before our pause opens; the screen-task hook (smm_on_pause_task) then
   makes the Load Game screen the pause's first screen. */
static int g_load_pending = 0;
static DWORD g_setup_hook = 0;         /* exe+31D499 (pause screen task), 0 = LoadMenu unavailable */

/* 1 = opened. load = open it on the Load Game screen. */
static int smm_do_pause(const char *how, int load)
{
    BYTE *obj = g_last_obj;
    g_last_obj = NULL;
    if (smm_on_world_map()) { smm_logv("%s: not on the World Map", how); return 0; }
    /* A fight the game can restart: open the game's own pause, untouched (as vanilla Start). */
    if (!load && g_cfg.game_retry && obj && rd8(SMM_BTLTYP) != 0 && !smm_in_cutscene() && smm_slot_offers_retry(obj)) {
        smm_log("%s: the game's pause offers Retry here, opening it (slot exe+%06X)", how, (unsigned)(obj - g_base));
        g_special_open(obj, NULL);
        return 1;
    }
    if (obj && rd8(SMM_BTLTYP) != 0) {                 /* for reports: what the game's own pause would have offered */
        char list[64] = "";
        unsigned i, n = obj[2] > 4 ? 4 : obj[2];
        for (i = 0; i < n; i++) {
            WORD fn, tx;
            char one[16];
            memcpy(&fn, obj + 4 + i * 4, 2); memcpy(&tx, obj + 6 + i * 4, 2);
            _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%X/%04X", i ? " " : "", fn, tx);
            strcat_s(list, sizeof(list), one);
        }
        smm_log("%s: the game's own pause here (slot exe+%06X) offers [%s]; using the custom pause", how, (unsigned)(obj - g_base), list);
    }
    /* Slot 3 is KSX's, but some missions use it for their own pause. Then take a slot that is
       completely unused (all 0x40 bytes zero): slot 2, then slot 0. Never slot 1 (battle) or 4
       (cutscenes). The pause takes any slot it is handed; the Start lookup never sees ours. */
    g_pause_slot = g_slot3;
    if (g_base[g_slot3] != 0) {
        static const int alt[2] = { 2, 0 };
        int k, b;
        g_pause_slot = 0;
        for (k = 0; k < 2 && !g_pause_slot; k++) {
            DWORD cand = g_slot3 - (DWORD)(3 - alt[k]) * 0x40;
            for (b = 0; b < 0x40 && g_base[cand + b] == 0; b++) {}
            if (b == 0x40) g_pause_slot = cand;
        }
        if (!g_pause_slot) {
            smm_log("%s: slot 3 is in use by the game and no free slot was found, custom pause skipped", how);
            return 0;
        }
        smm_log("%s: slot 3 is in use by the game (mission pause), using free slot %d (exe+%06X)",
                how, 3 - (int)((g_slot3 - g_pause_slot) / 0x40), g_pause_slot);
    }
    smm_log(load ? "%s: opening the pause on the Load Game screen" : "%s: opening the custom pause", how);
    if (g_cfg.log_verbose) smm_log_pause_state("  pause state before");
    smm_pause_fill();
    g_load_pending = load;
    g_special_open(g_base + g_pause_slot, NULL);
    /* The pause has taken the slot (BEF4A0); only the Start lookup reads the active byte, so
       disarm it now. Our main-loop hook does not run while the pause is up. The buttons stay
       until the close is seen. */
    g_base[g_pause_slot] = (BYTE)g_pause_orig[0];
    g_pause_val[0] = g_pause_orig[0];
    g_pause_phase = 1;
    g_pause_frames = 0;
    return 1;
}

static void smm_pause_end(const char *why)
{
    if (g_load_pending) { smm_log("  Load Game: the pause never started, nothing changed"); g_load_pending = 0; }
    smm_pause_restore(why);
    g_pause_phase = 0;
}

/* exe+31D499, in the pause's screen task (exe+31D3C0): the setup (exe+31D200) wrote current
   screen = 9 (button page) and request = -1; the task has just built the button page (its first
   block, only when the screen is 9) and is about to run the first screen. Make that the load
   screen (3). It is the same switch the Load Game button makes (function 1: request 3 once the
   button page closes), minus showing the button page first: backing out of Load goes to screen 9
   as usual, with its objects built, so the pause buttons appear and Continue resumes.
   Every pause start runs this, so it also tells the custom pause that it has started. */
static void SMM_GAME_ABI smm_on_pause_task(void)
{
    INT16 cur;
    /* Our pause has started: the restore can run on the first main-loop pass after it closes,
       instead of waiting for the phase-1 timeout (the main loop does not run while it is up). */
    if (g_pause_phase == 1) { g_pause_phase = 2; g_pause_frames = 0; }
    if (!g_load_pending) return;
    g_load_pending = 0;
    cur = (INT16)rd16(g_screen_cur);
    if (cur != 9 || (INT16)rd16(g_screen_cur + 4) != -1) {
        smm_log("  Load Game: unexpected pause screen %d / request %d, left alone", cur, (int)(INT16)rd16(g_screen_cur + 4));
        return;
    }
    {
        WORD load = SMM_SCREEN_LOAD;
        memcpy(g_base + g_screen_cur, &load, 2);
    }
    smm_log("  Load Game: pause starts on the Load Game screen");
}

/* 1 = the shop opened. */
static int smm_do_shop(const char *how)
{
    BYTE *e;
    int count, id;
    WORD flag, keeper;
    if (smm_on_world_map()) { smm_log("%s: no shops on the World Map", how); return 0; }
    if (rd8(SMM_BTLTYP) != 0) { smm_log("%s: shops only open out of battle", how); return 0; }
    if (smm_in_cutscene()) { smm_log("%s: shops do not open during cutscenes", how); return 0; }
    if (!g_shop_listed) smm_list_shops();
    id = g_cfg.shop_id >= 0 ? g_cfg.shop_id : g_last_shop >= 0 ? g_last_shop : SMM_DEFAULT_SHOP;
    e = (BYTE *)smm_shop_entry(id, &count);
    if (!count) { smm_log("%s: the shop table is not loaded", how); return 0; }
    if (!e) { smm_log("%s: the game has no shop 0x%X (see the list above); check ShopId", how, id); return 0; }
    flag = (WORD)(e[2] | e[3] << 8);
    if (g_cfg.shop_visited_only && !smm_flag(flag)) {
        smm_log("%s: shop 0x%X is not unlocked in this save yet (flag %X), skipped (VisitedOnly = 1)", how, id, flag);
        return 0;
    }
    /* The shopkeeper's model must already be loaded here, or the shop crashes. Otherwise Sora
       (the player's own model, always loaded) stands in until the shop closes. */
    smm_keeper_restore("new shop");
    keeper = (WORD)(e[6] | e[7] << 8);
    if (!smm_model_loaded(keeper)) {
        if (!smm_model_loaded(SMM_OBJ_PLAYER)) {
            smm_log("%s: shopkeeper %X is not loaded here and neither is the player model; not opening (it would crash)", how, keeper);
            return 0;
        }
        g_keeper_orig = keeper;
        g_keeper_swapped = e;
        e[6] = (BYTE)(SMM_OBJ_PLAYER & 0xFF); e[7] = (BYTE)(SMM_OBJ_PLAYER >> 8);
        smm_log("%s: shopkeeper %X is not loaded here, Sora stands in", how, keeper);
    }
    smm_log("%s: opening shop 0x%X%s (world %02X room %02X)", how, id, g_cfg.shop_id < 0 ? " (last moogle)" : "",
            rd8(SMM_WORLD), rd8(SMM_WORLD + 1));
    g_shop_ours = 1;
    g_open_shop(id);
    g_shop_ours = 0;
    if ((int)rd32(g_shop_var) != id) {
        smm_log("%s: the game did not open it (a menu is busy)", how);
        smm_keeper_restore("shop not opened");
        return 0;
    }
    return 1;
}

/* Our main-loop hook does not run while a pause is open: seeing it run with no pause screen for
   120 passes, or a new Start press, means our pause has closed. */
static void smm_pause_step(void)
{
    INT16 cur = (INT16)rd16(g_screen_cur);
    g_pause_frames++;
    if (g_pause_phase == 1) {
        if (cur >= 0) { g_pause_phase = 2; g_pause_frames = 0; }
        else if (g_pause_frames > 120) smm_pause_end("main loop resumed");
    } else if (g_pause_phase == 2) {
        if (cur < 0) smm_pause_end("pause screen closed");
        else if (g_pause_frames > 36000) smm_pause_end("pause open ~10 minutes");
    }
}

static int smm_bind_held(const SmmBind *b, BYTE trig)
{
    return b->trig == trig && (rd8(SMM_SHOULDERS) & SMM_MODS_MASK) == b->mods;
}
static int smm_start_combo(void)
{
    return (g_cfg.save_on && smm_bind_held(&g_cfg.save_bind, SMM_TRIG_START)) ||
           (g_cfg.pause_on && g_pause_ok && smm_bind_held(&g_cfg.pause_bind, SMM_TRIG_START)) ||
           (g_cfg.load_on && smm_bind_held(&g_cfg.load_bind, SMM_TRIG_START)) ||
           (g_cfg.shop_on && smm_bind_held(&g_cfg.shop_bind, SMM_TRIG_START));
}

/* With a special pause slot active: 1 = keep the game's special pause, 0 = take the normal path. */
static int smm_keep_special(int ours, int quiet)
{
    BYTE btl = rd8(SMM_BTLTYP);
    if (btl == 0) return 1;                            /* outside battle (minigames, events): leave it */
    if (smm_in_cutscene()) { if (!quiet) smm_logv("Start in battle during a cutscene: game's pause kept"); return 1; }
    if (smm_on_world_map()) return 1;
    if (smm_lua_pause_armed()) { if (!quiet) smm_logv("Start in battle: KSX's Lua anywhere pause armed, kept"); return 1; }
    if (ours) return 0;                                /* our keybinds work in missions too */
    if (!g_cfg.party_missions) return 1;
    if (!quiet) smm_logv("Start in battle (type %u): special pause skipped, normal path (party menu)", btl);
    return 0;
}

/* The game's own "may Start pause now?" checks, for D-pad keybinds. */
static int smm_game_accepts_start(void)
{
    BYTE *p;
    DWORD v;
    void *obj;
    if (rd32(g_gate_dw) != 0 || rd8(g_gate_b) == 0) return 0;
    if (g_gate_f1()) return 0;
    p = g_gate_f2();
    if (!p) return 0;
    memcpy(&v, p + 0x108, 4);
    if (v) return 0;
    if (g_gate_f3()) return 0;
    obj = g_lookup();
    g_last_obj = (BYTE *)obj;
    if (obj && smm_keep_special(1, 1)) return 0;
    if (g_gate_f4()) return 0;
    return 1;
}

/* ---------------------------------------------------------------- hook callbacks (game thread) */

/* exe+157312: the game is about to open its normal pause. 1 = we opened something instead. */
static int g_start_pass = 0;           /* 1 = the Start site ran in this main-loop pass */
static int SMM_GAME_ABI smm_on_start(void)
{
    /* This pass ends at the main-loop site; no D-pad keybind may open a second menu there. */
    g_start_pass = 1;
    /* The game is handling a Start press, so our last pause has closed: tidy up now (no lockout). */
    if (g_pause_phase != 0) smm_pause_end("Start pressed again");
    if (g_cfg.save_on && smm_bind_held(&g_cfg.save_bind, SMM_TRIG_START))
        return smm_do_save(g_save_name);
    if (g_cfg.pause_on && g_pause_ok && smm_bind_held(&g_cfg.pause_bind, SMM_TRIG_START))
        return smm_do_pause(g_pause_name, 0);
    if (g_cfg.load_on && smm_bind_held(&g_cfg.load_bind, SMM_TRIG_START))
        return smm_do_pause(g_load_name, 1);
    if (g_cfg.shop_on && smm_bind_held(&g_cfg.shop_bind, SMM_TRIG_START))
        return smm_do_shop(g_shop_name);
    return 0;
}

/* exe+157304: Start was pressed; obj is the game's special pause slot (0 = none).
   1 = keep the special pause, 0 = take the normal pause path (party menu, or our keybinds). */
static int SMM_GAME_ABI smm_on_special(void *obj)
{
    g_last_obj = (BYTE *)obj;                          /* for the keybinds on the normal path */
    if (obj == NULL) return 0;                         /* no special pause: as the game does */
    return smm_keep_special(smm_start_combo(), 0);
}

static BYTE g_last_dpad = 0;
/* blocked = the Start code already handled a press this pass (only track the D-pad). */
static void smm_dpad_step(int blocked)
{
    BYTE pad = rd8(SMM_DPAD) & SMM_TRIG_DPAD, fresh = (BYTE)(pad & ~g_last_dpad);
    int save, pause, load, shop;
    const char *name;
    g_last_dpad = pad;
    if (!fresh || pad != fresh) return;                /* one direction, just pressed, nothing else held */
    if (blocked) { smm_logv("D-pad keybind ignored: Start was handled in the same frame"); return; }
    save = g_cfg.save_on && smm_bind_held(&g_cfg.save_bind, pad);
    pause = g_cfg.pause_on && g_pause_ok && smm_bind_held(&g_cfg.pause_bind, pad);
    load = g_cfg.load_on && smm_bind_held(&g_cfg.load_bind, pad);
    shop = g_cfg.shop_on && smm_bind_held(&g_cfg.shop_bind, pad);
    if (!save && !pause && !load && !shop) return;
    name = save ? g_save_name : pause ? g_pause_name : load ? g_load_name : g_shop_name;
    if (!smm_game_accepts_start()) { smm_logv("%s: ignored, the game would not pause right now", name); return; }
    if (g_pause_phase != 0) smm_pause_end("keybind pressed again");
    if (save) smm_do_save(g_save_name);
    else if (pause) smm_do_pause(g_pause_name, 0);
    else if (load) smm_do_pause(g_load_name, 1);
    else smm_do_shop(g_shop_name);
}

/* exe+15732A: once per main-loop pass. */
static BYTE g_last_btl = 0xFF;
static int g_last_cut = -1;
static void SMM_GAME_ABI smm_on_frame(void)
{
    BYTE btl;
    int cut;
    if (g_cfg.party_battle && !smm_on_world_map() && g_base[g_slot1] != 0)
        g_base[g_slot1] = 0;
    if (g_pause_phase) smm_pause_step();
    if (g_keeper_swapped && rd32(g_shop_var) == 0xFFFFFFFF) smm_keeper_restore("shop closed");
    if (g_gate_ok) smm_dpad_step(g_start_pass);
    g_start_pass = 0;

    if (!g_cfg.log_verbose) return;                    /* the rest only feeds the verbose log */
    btl = rd8(SMM_BTLTYP);
    if (btl != g_last_btl) {
        const char *what = btl == 0 ? " (out of battle)" : btl == 1 ? " (regular battle)" :
                           btl == 2 ? " (forced battle)" : "";
        if (g_last_btl == 0xFF) smm_logv("BtlTyp at first frame: %u%s", btl, what);
        else smm_logv("BtlTyp %u -> %u%s", g_last_btl, btl, what);
        g_last_btl = btl;
    }
    cut = smm_in_cutscene();
    if (cut != g_last_cut) {
        if (g_last_cut != -1 || cut) smm_logv(cut ? "Cutscene started" : "Cutscene ended");
        g_last_cut = cut;
    }
}

/* ---------------------------------------------------------------- caves */
static const BYTE CAVE_ALIGN_PRO[] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x83, 0xEC, 0x20 };
static const BYTE CAVE_JMP_ABS[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };

static int put_bytes(BYTE *c, int n, const BYTE *b, int len) { memcpy(c + n, b, len); return n + len; }
static int put_u64(BYTE *c, int n, UINT64 v) { memcpy(c + n, &v, 8); return n + 8; }
static int put_jmp(BYTE *c, int n, const void *to)
{
    n = put_bytes(c, n, CAVE_JMP_ABS, 6);
    return put_u64(c, n, (UINT64)(UINT_PTR)to);
}
static int put_call(BYTE *c, int n, const void *fn)
{
    static const BYTE call_rax[] = { 0xFF, 0xD0 };
    c[n++] = 0x48; c[n++] = 0xB8;                         /* mov rax, imm64 */
    n = put_u64(c, n, (UINT64)(UINT_PTR)fn);
    return put_bytes(c, n, call_rax, 2);
}

/* Start: call fn(); if it returns non-zero jump to 'handled', else run the original 5 bytes
   and jump back. */
static int cave_start(BYTE *c, const void *fn, const BYTE *orig5, const BYTE *back, const BYTE *handled)
{
    static const BYTE tail[] = { 0x48, 0x89, 0xEC, 0x5D, 0x85, 0xC0, 0x75, 0x13 }; /* mov rsp,rbp; pop rbp; test; jnz +19 */
    int n = put_bytes(c, 0, CAVE_ALIGN_PRO, sizeof(CAVE_ALIGN_PRO));
    n = put_call(c, n, fn);
    n = put_bytes(c, n, tail, sizeof(tail));
    n = put_bytes(c, n, orig5, 5);
    n = put_jmp(c, n, back);
    return put_jmp(c, n, handled);
}

/* Special: rax = obj. Call fn(obj); restore rax; non-zero -> special path, else normal path.
   Replaces "test rax,rax / jne special" entirely. */
static int cave_special(BYTE *c, const void *fn, const BYTE *normal, const BYTE *special)
{
    static const BYTE pre[] = { 0x50 };                                       /* push rax */
    static const BYTE arg[] = { 0x48, 0x8B, 0x4D, 0x08 };                     /* mov rcx,[rbp+8] */
    static const BYTE tail[] = { 0x48, 0x89, 0xEC, 0x5D, 0x85, 0xC0, 0x58, 0x75, 0x0E }; /* ...; test; pop rax; jnz +14 */
    int n = put_bytes(c, 0, pre, 1);
    n = put_bytes(c, n, CAVE_ALIGN_PRO, sizeof(CAVE_ALIGN_PRO));
    n = put_bytes(c, n, arg, sizeof(arg));
    n = put_call(c, n, fn);
    n = put_bytes(c, n, tail, sizeof(tail));
    n = put_jmp(c, n, normal);
    return put_jmp(c, n, special);
}

/* Loop: call fn(); run the original 5 bytes; jump back. */
static int cave_loop(BYTE *c, const void *fn, const BYTE *orig5, const BYTE *back)
{
    static const BYTE tail[] = { 0x48, 0x89, 0xEC, 0x5D };                    /* mov rsp,rbp; pop rbp */
    int n = put_bytes(c, 0, CAVE_ALIGN_PRO, sizeof(CAVE_ALIGN_PRO));
    n = put_call(c, n, fn);
    n = put_bytes(c, n, tail, sizeof(tail));
    n = put_bytes(c, n, orig5, 5);
    return put_jmp(c, n, back);
}

/* Pause setup: keep rax (the setup's last call result), call fn(), run the original 5 bytes,
   jump back. */
/* OpenShop entry: keep rcx (the shop id), call fn(id), run the original 6 bytes
   (push rbx / sub rsp,20h), jump back. */
static int cave_openshop(BYTE *c, const void *fn, const BYTE *orig6, const BYTE *back)
{
    static const BYTE tail[] = { 0x48, 0x89, 0xEC, 0x5D, 0x59 };              /* mov rsp,rbp; pop rbp; pop rcx */
    int n = put_bytes(c, 0, (const BYTE *)"\x51", 1);                         /* push rcx */
    n = put_bytes(c, n, CAVE_ALIGN_PRO, sizeof(CAVE_ALIGN_PRO));
    n = put_call(c, n, fn);                                                   /* rcx is still the id */
    n = put_bytes(c, n, tail, sizeof(tail));
    n = put_bytes(c, n, orig6, 6);
    return put_jmp(c, n, back);
}

static int cave_setup(BYTE *c, const void *fn, const BYTE *orig5, const BYTE *back)
{
    static const BYTE tail[] = { 0x48, 0x89, 0xEC, 0x5D, 0x58 };              /* mov rsp,rbp; pop rbp; pop rax */
    int n = put_bytes(c, 0, (const BYTE *)"\x50", 1);                         /* push rax */
    n = put_bytes(c, n, CAVE_ALIGN_PRO, sizeof(CAVE_ALIGN_PRO));
    n = put_call(c, n, fn);
    n = put_bytes(c, n, tail, sizeof(tail));
    n = put_bytes(c, n, orig5, 5);
    return put_jmp(c, n, back);
}

#ifndef SMM_TEST_ALLOC
/* Executable memory within +-1.75 GB of 'target', so a 5-byte E9 jump reaches it. */
static BYTE *smm_alloc_near(BYTE *target, SIZE_T size)
{
    SYSTEM_INFO si;
    UINT_PTR gran, t, lo, hi, p;
    GetSystemInfo(&si);
    gran = si.dwAllocationGranularity;
    t = (UINT_PTR)target & ~(gran - 1);
    lo = t > 0x70000000 ? t - 0x70000000 : (UINT_PTR)si.lpMinimumApplicationAddress;
    hi = t + 0x70000000;
    for (p = t - gran; p > lo; p -= gran) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_FREE) {
            void *r = VirtualAlloc((LPVOID)p, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (r) return (BYTE *)r;
        }
    }
    for (p = t + gran; p < hi; p += gran) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_FREE) {
            void *r = VirtualAlloc((LPVOID)p, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (r) return (BYTE *)r;
        }
    }
    return NULL;
}
#endif
/* ---------------------------------------------------------------- install */
typedef struct {
    const char *name;
    DWORD off;            /* site, relative to the StartCheck match */
    BYTE expect[5];
    BYTE alt[5];          /* also accepted (the Lua patch); all zero = none */
    BYTE *site;
    BYTE *cave;
} SmmHook;

static SmmHook g_hooks[] = {
    { "special-pause decision", SMM_SPECIAL_OFF, { 0x48, 0x85, 0xC0, 0x75, 0x17 }, { 0x48, 0x85, 0xC0, 0x90, 0x90 }, NULL, NULL },
    { "Start keybinds",         SMM_START_OFF,   { 0xBA, 0xFF, 0xFF, 0xFF, 0xFF }, { 0 }, NULL, NULL },
    { "main loop",              SMM_LOOP_OFF,    { 0x33, 0xD2, 0x48, 0x8B, 0xCB }, { 0 }, NULL, NULL },
};
#define SMM_HOOK_COUNT (sizeof(g_hooks) / sizeof(g_hooks[0]))
enum { H_SPECIAL = 0, H_START = 1, H_LOOP = 2 };

static int smm_site_ok(const SmmHook *h)
{
    static const BYTE zero[5] = { 0 };
    if (memcmp(h->site, h->expect, 5) == 0) return 1;
    return memcmp(h->alt, zero, 5) != 0 && memcmp(h->site, h->alt, 5) == 0;
}

static int smm_write_jump(BYTE *site, BYTE *cave)
{
    INT64 rel = (INT64)(cave - (site + 5));
    BYTE jmp[5];
    DWORD old;
    INT32 r32;
    if (rel > 0x7FFFFFFFLL || rel < -0x80000000LL) return 0;
    r32 = (INT32)rel;
    jmp[0] = 0xE9;
    memcpy(jmp + 1, &r32, 4);
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(site, jmp, 5);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 1;
}

/* Data guard. Code is found by signature, but some data addresses (controller, world, battle
   type, cutscene flags, KSX's tables) can only be listed (DA()). Every data address the code
   names is compared with our notes: all the same = known layout; all moved by one amount (a game
   update that shifted its data, as the wiki's first release -> v1.0.0.7 did by +0x2840) = the
   listed ones are moved by that amount too; moved by different amounts = nothing is patched.
   Also sets g_slot3 / g_slot1. 0 = do not patch. */
static int smm_data_guard(void)
{
    static const BYTE lookup_head[14] = { 0x48, 0x83, 0xEC, 0x28, 0xB9, 0x05, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x20 };
    struct { const char *name; DWORD got, expected; } A[12];
    int n = 0, i, even = 1;
    DWORD S = g_targets[T_STARTCHECK].found, f, lookup;
    INT64 d;
    BYTE *s = g_base + S;

    f = g_targets[T_OPENMENU].found;
    A[n].name = "menu-open byte";    A[n].got = smm_rip(f, 2, 7);        A[n++].expected = 0x9006B0;
    A[n].name = "menu queue";        A[n].got = smm_rip(f + 0x09, 2, 6); A[n++].expected = 0x716B48;
    A[n].name = "current menu";      A[n].got = smm_rip(f + 0x18, 2, 6); A[n++].expected = 0x900724;
    lookup = smm_rip(S + 0x06, 1, 5);
    if (memcmp(g_base + lookup, lookup_head, 14) != 0 || g_base[lookup + 0x15] != 0x48 ||
        g_base[lookup + 0x16] != 0x8D || g_base[lookup + 0x17] != 0x15) {
        smm_log("HOOKS NOT INSTALLED: the pause slots could not be found (exe+%06X), so the game's data cannot be checked.", lookup);
        return 0;
    }
    g_slot3 = smm_rip(lookup + 0x15, 3, 7) + 3 * 0x40;
    g_slot1 = g_slot3 - 2 * 0x40;
    A[n].name = "pause slot 3";      A[n].got = g_slot3;                 A[n++].expected = 0x2A11560;
    if (s[-0x39] != 0x83 || s[-0x38] != 0x3D || s[-0x33] != 0x00) {
        smm_log("HOOKS NOT INSTALLED: the pause state test in front of the Start code looks different.");
        return 0;
    }
    A[n].name = "pause state";       A[n].got = smm_rip(S - 0x39, 2, 7); A[n++].expected = 0xABB878;
    if (g_targets[T_PAUSESELECT].count == 1) {
        A[n].name = "pause mode bits"; A[n].got = smm_rip(g_targets[T_PAUSESELECT].found + 0x0A, 2, 6); A[n++].expected = 0x749830;
    }
    if (g_targets[T_BUTTONS].count == 1) {
        A[n].name = "pause button list"; A[n].got = smm_rip(g_targets[T_BUTTONS].found, 3, 7); A[n++].expected = 0xBEF538;
    }
    if (g_targets[T_SHOPTABLE].count == 1) {
        A[n].name = "shop table pointer"; A[n].got = smm_rip(g_targets[T_SHOPTABLE].found, 3, 7); A[n++].expected = 0xBEE968;
    }
    if (g_targets[T_OPENSHOP].count == 1) {
        f = g_targets[T_OPENSHOP].found;
        A[n].name = "menu state";    A[n].got = smm_rip(f + 0x11, 2, 7); A[n++].expected = 0x716884;
        A[n].name = "shop id";       A[n].got = smm_rip(f + 0x28, 2, 6); A[n++].expected = 0x716B50;
    }
    if (g_targets[T_GETPAD].count == 1) {
        A[n].name = "controller (raw)"; A[n].got = smm_rip(g_targets[T_GETPAD].found + 3, 3, 7); A[n++].expected = 0xBF3270;
    }
    d = (INT64)A[0].got - A[0].expected;
    for (i = 1; i < n; i++) if ((INT64)A[i].got - A[i].expected != d) even = 0;
    if (!even) {
        smm_log("HOOKS NOT INSTALLED: this game version moved its data by different amounts, so the addresses");
        smm_log("  the module cannot find on its own (controller, world, battle state) are unknown:");
        for (i = 0; i < n; i++)
            smm_log("    %-18s exe+%06X (notes exe+%06X, %+lld)", A[i].name, A[i].got, A[i].expected, (long long)((INT64)A[i].got - A[i].expected));
        return 0;
    }
    g_delta = d;
    if (d) smm_log("Data guard: all %d data addresses the code names moved by %+lld (0x%llX); the listed ones are moved to match.",
                   n, (long long)d, (unsigned long long)(d < 0 ? -d : d));
    else smm_log("Data guard: all %d data addresses the code names match our notes.", n);
    return 1;
}

static void smm_install_hooks(void)
{
    BYTE *S, *block, orig[SMM_HOOK_COUNT][5];
    unsigned i;
    int lens[SMM_HOOK_COUNT];
    BYTE setup_orig[5];
    int setup_len = 0, openshop_len = 0;
    int dpad = (g_cfg.save_on && g_cfg.save_bind.trig != SMM_TRIG_START) ||
               (g_cfg.pause_on && g_cfg.pause_bind.trig != SMM_TRIG_START) ||
               (g_cfg.load_on && g_cfg.load_bind.trig != SMM_TRIG_START) ||
               (g_cfg.shop_on && g_cfg.shop_bind.trig != SMM_TRIG_START);

    if (!g_cfg.save_on && !g_cfg.pause_on && !g_cfg.load_on && !g_cfg.shop_on &&
        !g_cfg.party_battle && !g_cfg.party_missions) {
        smm_log("Every feature is switched off in ShroMagicMenu.ini: nothing patched.");
        return;
    }
    if (g_targets[T_OPENMENU].count != 1 || g_targets[T_STARTCHECK].count != 1 ||
        g_targets[T_PAUSETAIL].count != 1) {
        smm_log("HOOKS NOT INSTALLED: a required function was not found.");
        return;
    }
    if (smm_rip(g_targets[T_PAUSETAIL].found + 4, 1, 5) != g_targets[T_OPENMENU].found) {
        smm_log("HOOKS NOT INSTALLED: the pause code does not call the OpenMenu we found.");
        return;
    }
    g_open_menu = (SmmMenuFn)(g_base + g_targets[T_OPENMENU].found);
    g_menu_follow = (SmmMenuFn)(g_base + smm_rip(g_targets[T_PAUSETAIL].found + 9, 1, 5));
    S = g_base + g_targets[T_STARTCHECK].found;

    /* Check every site before touching any. */
    for (i = 0; i < SMM_HOOK_COUNT; i++) {
        g_hooks[i].site = S + g_hooks[i].off;
        if (!smm_site_ok(&g_hooks[i])) {
            BYTE *b = g_hooks[i].site;
            smm_log("HOOKS NOT INSTALLED: %s site bytes are %02X %02X %02X %02X %02X.",
                    g_hooks[i].name, b[0], b[1], b[2], b[3], b[4]);
            return;
        }
        memcpy(orig[i], g_hooks[i].site, 5);
    }
    if (!smm_data_guard()) return;

    /* Optional parts: each one is switched off on its own if its code looks different. */
    g_pause_ok = (g_cfg.pause_on || g_cfg.load_on || dpad) ? smm_resolve_pause() : 0;
    if (g_cfg.pause_on && !g_pause_ok) g_cfg.pause_on = 0;
    if (dpad) {
        g_gate_ok = g_pause_ok ? smm_resolve_gate() : 0;
        if (!g_gate_ok) {
            if (g_cfg.save_on && g_cfg.save_bind.trig != SMM_TRIG_START) g_cfg.save_on = 0;
            if (g_cfg.pause_on && g_cfg.pause_bind.trig != SMM_TRIG_START) g_cfg.pause_on = 0;
            if (g_cfg.load_on && g_cfg.load_bind.trig != SMM_TRIG_START) g_cfg.load_on = 0;
            if (g_cfg.shop_on && g_cfg.shop_bind.trig != SMM_TRIG_START) g_cfg.shop_on = 0;
            smm_log("D-pad keybinds are OFF (see above).");
        }
    }
    /* Story flags (Journal button) and the remote shop. */
    g_shop_ok = smm_resolve_shop();
    if (g_cfg.shop_on && !g_shop_ok) g_cfg.shop_on = 0;
    /* A hook in the pause's screen task, after it built the button page: LoadMenu needs it (Load
       Game as the first screen); the custom pause uses it to see that its pause started. The
       task must test "screen == 9" before the site (the button page block) and "screen == 4"
       right after it, both on the pause screen word. */
    g_setup_hook = 0;
    if (g_cfg.load_on || g_cfg.pause_on) {
        DWORD f = g_targets[T_PAUSESETUP].found;
        if (!g_pause_ok) smm_log("Load menu unavailable: needs the custom pause code.");
        else if (g_targets[T_PAUSESETUP].count != 1) smm_log("Pause task hook unavailable (Load menu off): the pause screen task was not found.");
        else if (smm_rip(f + 0x14, 3, 8) != g_screen_cur ||
                 g_base[f - 0xAB] != 0x66 || g_base[f - 0xAA] != 0x83 || g_base[f - 0xA9] != 0x3D || g_base[f - 0xA4] != 0x09 ||
                 smm_rip(f - 0xAB, 3, 8) != g_screen_cur)
            smm_log("Pause task hook unavailable (Load menu off): exe+%06X is not the pause screen task we know.", f);
        else g_setup_hook = f;
        if (!g_setup_hook) g_cfg.load_on = 0;
        else memcpy(setup_orig, g_base + g_setup_hook, 5);
    }
    if (g_cfg.force_world) {
        if (g_pause_ok && smm_resolve_force()) smm_apply_force();
        else g_cfg.force_world = 0;
    }

    block = smm_alloc_near(S, 0x1000);
    if (!block) { smm_log("HOOKS NOT INSTALLED: no memory near the game module."); return; }
    g_hooks[H_SPECIAL].cave = block;
    g_hooks[H_START].cave = block + 0x100;
    g_hooks[H_LOOP].cave = block + 0x200;
    lens[H_SPECIAL] = cave_special(g_hooks[H_SPECIAL].cave, (const void *)(UINT_PTR)smm_on_special,
                                   S + SMM_NORMAL_OFF, S + SMM_MISSION_OFF);
    lens[H_START] = cave_start(g_hooks[H_START].cave, (const void *)(UINT_PTR)smm_on_start, orig[H_START],
                               S + SMM_START_BACK, S + SMM_LOOP_OFF);
    lens[H_LOOP] = cave_loop(g_hooks[H_LOOP].cave, (const void *)(UINT_PTR)smm_on_frame, orig[H_LOOP],
                             S + SMM_LOOP_BACK);
    if (g_setup_hook)
        setup_len = cave_setup(block + 0x300, (const void *)(UINT_PTR)smm_on_pause_task, setup_orig,
                               g_base + g_setup_hook + 5);
    /* Remote shop "Last": a hook at OpenShop's entry remembers the moogles the player talks to. */
    g_openshop_hook = 0;
    if (g_cfg.shop_on && g_shop_ok) {
        static const BYTE head[6] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20 };   /* push rbx; sub rsp,20h */
        DWORD f = g_targets[T_OPENSHOP].found;
        if (memcmp(g_base + f, head, 6) == 0) {
            openshop_len = cave_openshop(block + 0x380, (const void *)(UINT_PTR)smm_on_open_shop, head, g_base + f + 6);
            g_openshop_hook = f;
        } else smm_log("Last moogle tracking unavailable: unexpected bytes at OpenShop.");
    }
    FlushInstructionCache(GetCurrentProcess(), block, 0x400);

    for (i = 0; i < SMM_HOOK_COUNT; i++) {
        if (!smm_write_jump(g_hooks[i].site, g_hooks[i].cave)) {
            unsigned j;
            DWORD old;
            for (j = 0; j < i; j++) {                    /* undo what we already wrote */
                VirtualProtect(g_hooks[j].site, 5, PAGE_EXECUTE_READWRITE, &old);
                memcpy(g_hooks[j].site, orig[j], 5);
                VirtualProtect(g_hooks[j].site, 5, old, &old);
                FlushInstructionCache(GetCurrentProcess(), g_hooks[j].site, 5);
            }
            smm_log("HOOKS NOT INSTALLED: could not write the %s hook (all hooks undone).", g_hooks[i].name);
            g_pause_ok = 0; g_gate_ok = 0;
            return;
        }
    }
    for (i = 0; i < SMM_HOOK_COUNT; i++)
        smm_log("PATCHED: %-22s exe+%06X -> cave %p (%d bytes)", g_hooks[i].name,
                (unsigned)(g_hooks[i].site - g_base), g_hooks[i].cave, lens[i]);
    if (memcmp(orig[H_SPECIAL], g_hooks[H_SPECIAL].alt, 5) == 0)
        smm_log("(The Lua mission patch was already applied; this hook replaces it.)");
    /* Optional: the Load menu's hook. If it cannot be written only the Load menu is off. */
    if (g_setup_hook) {
        if (smm_write_jump(g_base + g_setup_hook, block + 0x300))
            smm_log("PATCHED: %-22s exe+%06X -> cave %p (%d bytes)", "pause screen task", g_setup_hook, block + 0x300, setup_len);
        else {
            smm_log("Pause task hook unavailable (Load menu off): could not write it.");
            g_setup_hook = 0;
            g_cfg.load_on = 0;
        }
    }
    /* Optional: last moogle tracking. 5-byte jump over the 6-byte prologue; the cave returns to +6. */
    if (g_openshop_hook) {
        if (smm_write_jump(g_base + g_openshop_hook, block + 0x380))
            smm_log("PATCHED: %-22s exe+%06X -> cave %p (%d bytes)", "OpenShop (last moogle)", g_openshop_hook, block + 0x380, openshop_len);
        else { smm_log("Last moogle tracking unavailable: could not write the OpenShop hook."); g_openshop_hook = 0; }
    }
    /* The caves are written: execute-read from now on (no writable code left behind). */
    {
        DWORD old;
        if (!VirtualProtect(block, 0x1000, PAGE_EXECUTE_READ, &old)) smm_log("(Could not make the hook memory read-only; harmless.)");
    }
    smm_read_last_shop();
    if (g_cfg.shop_on && g_cfg.shop_id < 0) {
        if (g_last_shop >= 0) smm_log("Remote shop: last moogle remembered, shop 0x%X", g_last_shop);
        else smm_log("Remote shop: no moogle remembered yet, using 0x%X (Hollow Bastion)", SMM_DEFAULT_SHOP);
    }

    smm_log("Ready:");
    smm_log("  Save menu:     %s%s", g_cfg.save_on ? g_save_name : "off",
            g_cfg.save_in_battle ? " (saving in battle ON)" : " (no saving in battle, also not from the custom pause)");
    {
        char buttons[64] = "";
        if (g_cfg.show_save) strcat_s(buttons, sizeof(buttons), "Save, ");
        if (g_cfg.show_load) strcat_s(buttons, sizeof(buttons), "Load Game, ");
        if (g_cfg.show_journal) strcat_s(buttons, sizeof(buttons), g_flags ? "Journal (once unlocked), " : "Journal, ");
        strcat_s(buttons, sizeof(buttons), "Continue");
        if (g_cfg.pause_on) smm_log("  Custom pause:  %s (%s)%s%s", g_pause_name, buttons, g_cfg.force_world ? ", ForceWorldTravel ON" : "",
                                    g_cfg.game_retry ? "; the game's Retry pause where offered" : "");
        else smm_log("  Custom pause:  off");
    }
    smm_log("  Load menu:     %s", g_cfg.load_on ? g_load_name : "off");
    if (g_cfg.shop_on && g_cfg.shop_id < 0) smm_log("  Remote shop:   %s (last moogle%s)", g_shop_name, g_cfg.shop_visited_only ? ", once unlocked" : "");
    else if (g_cfg.shop_on) smm_log("  Remote shop:   %s (shop 0x%X%s)", g_shop_name, g_cfg.shop_id, g_cfg.shop_visited_only ? ", once unlocked" : "");
    else smm_log("  Remote shop:   off");
    smm_log("  Party menu:    in battle %s, in missions %s", g_cfg.party_battle ? "on" : "off", g_cfg.party_missions ? "on" : "off");
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
    /* The first four are required; the others only enable optional parts. */
    if (!final_pass) {
        for (i = 0; i <= T_PAUSETAIL; i++)
            if (g_targets[i].count != 1) return 0;
    }

    for (i = 0; i < SMM_TARGET_COUNT; i++) {
        SmmTarget *t = &g_targets[i];
        if (t->count == 1)
            smm_log("%-12s FOUND at exe+%06X (expected exe+%06X)%s", t->name, t->found,
                    t->expected, t->found == t->expected ? "" : "  <- moved");
        else
            smm_log("%-12s NOT RESOLVED (%d matches; need exactly 1)", t->name, t->count);
    }
    if (g_targets[T_OPENMENU].count == 1) {
        DWORD f = g_targets[T_OPENMENU].found;
        smm_log("OpenMenu derived addresses:");
        smm_check("menu-open byte", smm_rip(f + 0x00, 2, 7), 0x9006B0);
        smm_check("menu queue", smm_rip(f + 0x09, 2, 6), 0x716B48);
        smm_check("currentmenu", smm_rip(f + 0x18, 2, 6), 0x900724);
    }
    if (g_targets[T_STARTCHECK].count == 1) {
        DWORD f = g_targets[T_STARTCHECK].found;
        smm_log("StartCheck derived addresses:");
        smm_check("mission-pause branch", f + 0x0E, 0x157307);
        smm_check("special-pause lookup function", smm_rip(f + 0x06, 1, 5), 0x3AD540);
    }
    if (g_targets[T_PAUSESELECT].count == 1) {
        DWORD f = g_targets[T_PAUSESELECT].found;
        smm_log("PauseSelect derived addresses:");
        smm_check("pause mode bits", smm_rip(f + 0x0A, 2, 6), 0x749830);
        smm_check("pause key table", smm_rip(f + 0x27, 3, 7), 0x5BA170);
    }
    if (g_targets[T_PAUSETAIL].count == 1) {
        DWORD f = g_targets[T_PAUSETAIL].found;
        smm_log("PauseTail derived addresses:");
        smm_check("first call (OpenMenu)", smm_rip(f + 0x04, 1, 5), 0x151770);
        smm_check("second call (follow-up)", smm_rip(f + 0x09, 1, 5), 0x151480);
    }
    if (g_targets[T_BUTTONS].count == 1) {
        DWORD f = g_targets[T_BUTTONS].found;
        smm_log("PauseButtons derived addresses:");
        smm_check("button function-number list ptr", smm_rip(f, 3, 7), 0xBEF538);
    }
    smm_log("Probe finished: %d of %u targets resolved.", resolved, (unsigned)SMM_TARGET_COUNT);
    return 1;
}

static DWORD WINAPI smm_retry_thread(LPVOID unused)
{
    int tries;
    (void)unused;
    /* A few tries only: after a game update the signatures never match, and every full scan
       costs ~50 ms of CPU while the game boots. */
    for (tries = 0; tries < 6; tries++) {
        Sleep(1000);
        if (smm_probe(0)) {
            smm_log("(resolved on retry %d, for the log only)", tries + 1);
            return 0;
        }
    }
    smm_probe(1);
    return 0;
}

/* ---------------------------------------------------------------- crash report */
/* If the game crashes, write where (exe+offset) and the code addresses on the stack to the log
   before Windows ends it. Only real faults are logged (not the C++ exceptions games throw and
   catch), at most a few, and the exception is always passed on unchanged. This handler runs
   before the game's own handlers, so a fault the game (or another mod) catches is logged too:
   hence "fault", and "CRASH" only means something if the game closed right after. */
#ifndef SMM_TEST_ALLOC
static volatile LONG g_crash_logged = 0;
static LONG CALLBACK smm_on_exception(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    UINT_PTR rip, base = (UINT_PTR)g_base, end;
    IMAGE_NT_HEADERS64 *nt;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_crash_logged) > 3 || !g_base) return EXCEPTION_CONTINUE_SEARCH;
    nt = (IMAGE_NT_HEADERS64 *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    end = base + nt->OptionalHeader.SizeOfImage;
    rip = (UINT_PTR)ep->ContextRecord->Rip;
    if (rip >= base && rip < end)
        smm_log("FAULT %08X at exe+%06X (if the game closed right after this, this is the CRASH)", code, (unsigned)(rip - base));
    else
        smm_log("FAULT %08X at %p, outside the game exe (if the game closed right after this, this is the CRASH)", code, (void *)rip);
    /* Stack overflow: this runs on the little stack that is left; one line is all we risk. */
    if (code == EXCEPTION_STACK_OVERFLOW) { if (g_log != INVALID_HANDLE_VALUE) FlushFileBuffers(g_log); return EXCEPTION_CONTINUE_SEARCH; }
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        smm_log("  %s address %p", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    smm_log("  rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX",
            ep->ContextRecord->Rax, ep->ContextRecord->Rbx, ep->ContextRecord->Rcx,
            ep->ContextRecord->Rdx, ep->ContextRecord->Rsi, ep->ContextRecord->Rdi);
    {
        /* Return addresses into the game's code on the stack: the call chain, newest first. */
        const UINT64 *sp = (const UINT64 *)(UINT_PTR)ep->ContextRecord->Rsp;
        char line[400] = "";
        int i, found = 0;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(sp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
            UINT_PTR top = (UINT_PTR)mbi.BaseAddress + mbi.RegionSize;
            for (i = 0; i < 512 && (UINT_PTR)(sp + i + 1) <= top && found < 16; i++) {
                if (sp[i] > base + g_code_lo && sp[i] < base + g_code_hi) {
                    char one[16];
                    _snprintf_s(one, sizeof(one), _TRUNCATE, " exe+%06X", (unsigned)(sp[i] - base));
                    strcat_s(line, sizeof(line), one);
                    found++;
                }
            }
        }
        smm_log("  stack:%s", found ? line : " (no game code addresses)");
    }
    if (g_log != INVALID_HANDLE_VALUE) FlushFileBuffers(g_log);   /* the process may end now */
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void smm_start(void)
{
    char marker[9] = { 0 };
    int problems;

    smm_read_ini();
    smm_load_settings(1);                       /* quiet pass: is the log wanted? */
    if (g_cfg.log_on) smm_open_log();
    smm_log("Shro's Magic Menu %s", SMM_VERSION);
    problems = smm_load_settings(0);            /* again, logging any problems */
    smm_log("Settings: %s%s", g_ini_state == 0 ? "ShroMagicMenu.ini" :
            g_ini_state == 1 ? "ShroMagicMenu.ini was missing, defaults written" :
            g_ini_state == 2 ? "ShroMagicMenu.ini missing and could not be written, defaults used" :
            g_ini_state == 4 ? "ShroMagicMenu.ini is saved as Unicode (UTF-16); save it as ANSI or UTF-8. Defaults used" :
                               "ShroMagicMenu.ini could not be read, defaults used",
            problems ? " (problems above)" : "");

    g_base = (BYTE *)GetModuleHandleW(NULL);
    smm_log("Game module base %p", g_base);
    if (!g_base) return;
    smm_identify_build();
#ifndef SMM_TEST_ALLOC
    if (g_cfg.log_on) AddVectoredExceptionHandler(0, smm_on_exception);
#endif

    /* KSX's build check. The data addresses above are Steam Global only. */
    memcpy(marker, g_base + 0x585E59, 8);
    smm_log("Build marker at exe+585E59: \"%s\" %s", marker,
            strcmp(marker, "epicuser") == 0 ? "(Steam Global)" : "(not Steam Global)");

    if (smm_probe(0)) {
        if (strcmp(marker, "epicuser") == 0)
            smm_install_hooks();
        else
            smm_log("HOOKS NOT INSTALLED: not the Steam Global build (data addresses would be wrong).");
    } else {
        HANDLE th;
        smm_log("Not all targets resolved at load, so no hooks are installed (hooks are only ever");
        smm_log("installed at load, before the game loop runs). Retrying the probe for the log only.");
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
