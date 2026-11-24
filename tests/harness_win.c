/* Windows test for 1.9 (Load menu, remote shop, story flags): maps the REAL game exe (argv[1])
   into memory, runs the real module (signature scan, hook install with real caves), then checks
   the new parts with the game's own code where it can run without game state and stand-ins
   where it cannot. Build (x64 Native Tools prompt, from the repo folder):
     cl /nologo /O2 /W4 tests\harness_win.c /Fe<scratch>\harness_win.exe
   Run it from a scratch folder: it writes ShroMagicMenu.log/.ini next to itself. */
#include <windows.h>
#include <stdio.h>
static BYTE *g_img;
static HMODULE WINAPI test_GetModuleHandleW(LPCWSTR n) { (void)n; return (HMODULE)g_img; }
#define GetModuleHandleW test_GetModuleHandleW
#include "../src/shro_magic_menu.c"
#undef GetModuleHandleW

#define IMG_SIZE 0x2E00000
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }
static WORD r16(DWORD o) { WORD v; memcpy(&v, g_img + o, 2); return v; }
static DWORD r32(DWORD o) { DWORD v; memcpy(&v, g_img + o, 4); return v; }
static void w16(DWORD o, WORD v) { memcpy(g_img + o, &v, 2); }
static void w32(DWORD o, DWORD v) { memcpy(g_img + o, &v, 4); }

static int map_exe(const char *path)
{
    FILE *f;
    long n;
    BYTE *d;
    DWORD pe, i;
    WORD nsec, opt;
    if (fopen_s(&f, path, "rb") || !f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = (BYTE *)malloc(n);
    fread(d, 1, n, f); fclose(f);
    g_img = (BYTE *)VirtualAlloc(NULL, IMG_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    memcpy(&pe, d + 0x3C, 4); memcpy(&nsec, d + pe + 6, 2); memcpy(&opt, d + pe + 20, 2);
    memcpy(g_img, d, 0x1000);
    for (i = 0; i < nsec; i++) {
        BYTE *sh = d + pe + 24 + opt + i * 40;
        DWORD vs, va, rs, rp;
        memcpy(&vs, sh + 8, 4); memcpy(&va, sh + 12, 4); memcpy(&rs, sh + 16, 4); memcpy(&rp, sh + 20, 4);
        memcpy(g_img + va, d + rp, rs < vs || vs == 0 ? rs : vs);
    }
    free(d);
    return 1;
}

/* Stand-ins for the game's object entry / model path / model cache lookups: two loaded objects. */
static int g_loaded_a = 0, g_loaded_b = 0;
static char g_path_buf[32];
static void *SMM_GAME_ABI test_entry(int id) { static int e; e = id & 0x0FFFFFFF; return &e; }
static char *SMM_GAME_ABI test_path(void *entry, char *buf) { (void)buf; _snprintf_s(g_path_buf, sizeof(g_path_buf), _TRUNCATE, "obj/%X.mdlx", *(int *)entry); return g_path_buf; }
static void *SMM_GAME_ABI test_find(const char *name, int tag)
{
    char a[32], b[32];
    (void)tag;
    _snprintf_s(a, sizeof(a), _TRUNCATE, "obj/%X.mdlx", g_loaded_a);
    _snprintf_s(b, sizeof(b), _TRUNCATE, "obj/%X.mdlx", g_loaded_b);
    return (g_loaded_a && !strcmp(name, a)) || (g_loaded_b && !strcmp(name, b)) ? (void *)name : NULL;
}

static void set_flag(unsigned n, int on)
{
    DWORD a = 0x9ABC40 + (n >> 5) * 4, v = r32(a);
    v = on ? v | (1u << (n & 31)) : v & ~(1u << (n & 31));
    w32(a, v);
}

int main(int argc, char **argv)
{
    unsigned i;
    if (argc < 2 || !map_exe(argv[1])) { printf("usage: harness_win <KINGDOM HEARTS II FINAL MIX.exe>\n"); return 2; }

    /* --- 1.9.1: the full scan (any other build) finds every signature exactly once, where the
       known-build fast path looks. Run before the hooks change the code. */
    {
        int all = 1;
        g_base = g_img;
        g_known_build = 0;
        smm_probe(1);
        for (i = 0; i < SMM_TARGET_COUNT; i++)
            if (g_targets[i].count != 1 || g_targets[i].found != g_targets[i].expected) {
                printf("  full scan: %s count %d at exe+%06X\n", g_targets[i].name, g_targets[i].count, g_targets[i].found);
                all = 0;
            }
        check(all, "full scan: every signature unique, at the offset the fast path checks (GetPad included)");
        g_base = NULL;
    }
    smm_start();
    check(g_known_build && g_code_lo == 0x1000 && g_code_hi > 0x579000, "known build recognised from the PE header; code range from the sections");

    /* --- signatures and derived addresses */
    for (i = 0; i < SMM_TARGET_COUNT; i++) {
        char m[96];
        _snprintf_s(m, sizeof(m), _TRUNCATE, "%s found once at exe+%06X", g_targets[i].name, g_targets[i].expected);
        check(g_targets[i].count == 1 && g_targets[i].found == g_targets[i].expected, m);
    }
    check(g_flags == 0x9ABC40, "flag array = exe+9ABC40 (Save+2390)");
    check(g_shop_table == 0xBEE968, "shop table pointer = exe+BEE968");
    check(g_shop_var == 0x716B50, "shop id variable = exe+716B50");
    check(g_shop_ok && g_open_shop == (SmmShopFn)(g_img + 0x1518E0), "OpenShop = exe+1518E0");
    check(g_setup_hook == 0x31D34D && g_img[0x31D34D] == 0xE9, "pause setup hook written at exe+31D34D");
    check(g_img[0x157304] == 0xE9 && g_img[0x157312] == 0xE9 && g_img[0x15732A] == 0xE9, "three main hooks written");
    check(g_cfg.load_on && g_cfg.shop_on && g_cfg.save_on && g_cfg.pause_on, "all keybind features on with defaults");

    /* --- 1.9.1 data guard: move the data the code names (its rip displacements) and check that
       an even move shifts the listed addresses with it and an uneven one refuses. */
    {
        static const DWORD disp[] = { 0x151772, 0x15177B, 0x15178A,           /* OpenMenu: menu-open, queue, current menu */
                                      0x3AD558,                               /* slot lookup: lea rdx,[slots] */
                                      0x1572C2,                               /* pause state test in front of the Start code */
                                      0x2E498F, 0x31DE85, 0x2FBA61,           /* pause mode bits, button list, shop table */
                                      0x1518F3, 0x15190A, 0x39B766 };         /* menu state, shop id, raw pad */
        unsigned k;
        INT32 v;
        check(g_delta == 0 && SMM_WORLD == 0x717008 && SMM_CUT_PAUSE == 0x2A115A0 && SMM_PAUSE_BLOCK == 0x2A11562,
              "data guard, known build: no shift; slot 4 / slot 3 block derived from slot 3");
        for (k = 0; k < sizeof(disp) / sizeof(disp[0]); k++) { memcpy(&v, g_img + disp[k], 4); v += 0x40; memcpy(g_img + disp[k], &v, 4); }
        check(smm_data_guard() && g_delta == 0x40 && SMM_WORLD == 0x717048 && SMM_SHOULDERS == 0x71342B && g_slot3 == 0x2A115A0,
              "data guard, all data moved +0x40: listed addresses moved with it");
        memcpy(&v, g_img + disp[0], 4); v += 0x10; memcpy(g_img + disp[0], &v, 4);
        g_delta = 0;
        check(!smm_data_guard(), "data guard, data moved unevenly: refuses (nothing patched)");
        memcpy(&v, g_img + disp[0], 4); v -= 0x10; memcpy(g_img + disp[0], &v, 4);
        for (k = 0; k < sizeof(disp) / sizeof(disp[0]); k++) { memcpy(&v, g_img + disp[k], 4); v -= 0x40; memcpy(g_img + disp[k], &v, 4); }
        check(smm_data_guard() && g_delta == 0 && g_slot3 == 0x2A11560, "data guard back on the real layout");
    }

    /* --- Load menu: run the real hook through a copy of the setup's own prologue + the patched tail */
    {
        /* mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,40h; jmp exe+31D34D */
        BYTE *t = g_img + 0x2D90000;
        static const BYTE pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40 };
        INT32 rel;
        memcpy(t, pro, sizeof(pro));
        t[sizeof(pro)] = 0xE9;
        rel = (INT32)((g_img + 0x31D34D) - (t + sizeof(pro) + 5));
        memcpy(t + sizeof(pro) + 1, &rel, 4);
        w16(0x744C10, 9); w16(0x744C14, 0xFFFF);
        g_load_pending = 0;
        ((void (*)(void))t)();
        check(r16(0x744C10) == 9 && r16(0x744C14) == 0xFFFF, "setup hook, no Load pending: pause starts on the button page (9)");
        g_load_pending = 1;
        ((void (*)(void))t)();
        check(r16(0x744C10) == 3 && r16(0x744C14) == 0xFFFF && !g_load_pending, "setup hook, Load pending: pause starts on the load screen (3), request untouched");
        w16(0x744C10, 3); g_load_pending = 1;
        ((void (*)(void))t)();
        check(r16(0x744C10) == 3 && !g_load_pending, "setup hook, unexpected screen: left alone");
        w16(0x744C10, 0xFFFF);
    }

    /* --- remote shop with a stand-in OpenShop and a stand-in shop table */
    {
        /* OpenShop past our entry hook (the cave runs push rbx / sub rsp,20h, then jumps to +6):
           mov [exe+716B50],ecx; add rsp,20h; pop rbx; ret */
        static const BYTE stub[12] = { 0x89, 0x0D, 0, 0, 0, 0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 };
        BYTE *tab = g_img + 0x2DA0000;
        UINT64 p = (UINT64)(UINT_PTR)tab;
        INT32 d = (INT32)(0x716B50 - (0x1518E6 + 6));
        check(g_openshop_hook == 0x1518E0 && g_img[0x1518E0] == 0xE9, "OpenShop entry hook written (last moogle tracking)");
        memcpy(g_img + 0x1518E6, stub, 12); memcpy(g_img + 0x1518E8, &d, 4);
        g_obj_entry = test_entry; g_obj_path = test_path; g_res_find = test_find;
        memset(tab, 0, 0x100);
        tab[6] = 3;                                                     /* 3 shops */
        w16(0x2DA0010, 0x13); w16(0x2DA0012, 0x01A0); w16(0x2DA0014, 0x1234); w16(0x2DA0016, 0x50F);
        w16(0x2DA0028, 0x85); w16(0x2DA002A, 0x01A1); w16(0x2DA002E, 0x487);
        w16(0x2DA0040, 0x66); w16(0x2DA0042, 0x01A2); w16(0x2DA0046, 0x539);
        w32(0x716B50, 0xFFFFFFFF);
        memcpy(g_img + 0xBEE968, &p, 8);
        g_img[SMM_WORLD] = 0x04; g_img[SMM_BTLTYP] = 0;
        w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0);
        g_loaded_a = 0x50F; g_loaded_b = SMM_OBJ_PLAYER;               /* the moogle's room */

        set_flag(0x1A0, 0);
        g_cfg.shop_id = 0x13; g_cfg.shop_visited_only = 1;
        check(!smm_do_shop("test") && r32(0x716B50) == 0xFFFFFFFF, "shop not unlocked in the save: refused");
        check(g_shop_listed, "shop list logged on first use");
        set_flag(0x1A0, 1);
        check(smm_do_shop("test") && r32(0x716B50) == 0x13, "unlocked shop 0x13: OpenShop(0x13) called");
        w32(0x716B50, 0xFFFFFFFF);
        g_cfg.shop_visited_only = 0; g_cfg.shop_id = 0x85;
        check(smm_do_shop("test") && r32(0x716B50) == 0x85, "VisitedOnly = 0: locked shop 0x85 opens");
        w32(0x716B50, 0xFFFFFFFF);
        g_cfg.shop_id = 0x99;
        check(!smm_do_shop("test") && r32(0x716B50) == 0xFFFFFFFF, "unknown shop id: refused");
        g_cfg.shop_id = 0x13;
        g_img[SMM_BTLTYP] = 1;
        check(!smm_do_shop("test") && r32(0x716B50) == 0xFFFFFFFF, "in battle: refused");
        g_img[SMM_BTLTYP] = 0; g_img[SMM_WORLD] = SMM_WORLD_MAP;
        check(!smm_do_shop("test"), "World Map: refused");
        g_img[SMM_WORLD] = 0x04; w32(SMM_CUT_FLAGS, 5);
        check(!smm_do_shop("test"), "cutscene: refused");
        w32(SMM_CUT_FLAGS, 1);

        /* shopkeeper model: loaded -> as is; not loaded -> Sora (0x236) until the shop closes */
        g_cfg.shop_id = 0x13; w32(0x716B50, 0xFFFFFFFF);
        check(smm_do_shop("test") && r16(0x2DA0016) == 0x50F && !g_keeper_swapped, "moogle loaded here: shopkeeper kept");
        w32(0x716B50, 0xFFFFFFFF);
        g_loaded_a = 0;                                                 /* the next room */
        check(smm_do_shop("test") && r16(0x2DA0016) == SMM_OBJ_PLAYER && g_keeper_swapped, "moogle not loaded: Sora stands in");
        smm_on_frame();
        check(r16(0x2DA0016) == SMM_OBJ_PLAYER, "shop still open: stand-in kept");
        w32(0x716B50, 0xFFFFFFFF);
        smm_on_frame();
        check(r16(0x2DA0016) == 0x50F && !g_keeper_swapped, "shop closed: moogle put back");
        g_loaded_b = 0;
        check(!smm_do_shop("test") && r32(0x716B50) == 0xFFFFFFFF && r16(0x2DA0016) == 0x50F, "nothing loaded: refused, table untouched");
        g_loaded_a = 0x50F; g_loaded_b = SMM_OBJ_PLAYER;

        /* last moogle: the game opening a moogle's shop (through the real hook) is remembered */
        g_last_shop = -1;
        ((void (*)(int))(g_img + 0x1518E0))(0x66);
        check(g_last_shop == -1 && r32(0x716B50) == 0x66, "game opens a non-moogle shop: not remembered");
        w32(0x716B50, 0xFFFFFFFF);
        ((void (*)(int))(g_img + 0x1518E0))(0x85);
        check(g_last_shop == 0x85 && r32(0x716B50) == 0x85, "game opens moogle shop 0x85: remembered");
        g_last_shop = -1; smm_read_last_shop();
        check(g_last_shop == 0x85, "remembered moogle read back from ShroMagicMenu-lastshop.txt");
        w32(0x716B50, 0xFFFFFFFF);
        g_cfg.shop_id = -1;
        g_loaded_a = 0x487;
        check(smm_do_shop("test") && r32(0x716B50) == 0x85 && g_last_shop == 0x85, "ShopId = Last: opens the remembered moogle 0x85");
        w32(0x716B50, 0xFFFFFFFF);
        g_last_shop = -1;
        g_loaded_a = 0x50F;
        check(smm_do_shop("test") && r32(0x716B50) == 0x13, "ShopId = Last, none remembered: Hollow Bastion 0x13");
        w32(0x716B50, 0xFFFFFFFF);

        p = 0; memcpy(g_img + 0xBEE968, &p, 8);
        check(!smm_do_shop("test"), "shop table not loaded: refused");
    }

    /* --- Journal button follows the story flag 0x73 (Save+239E bit 8) */
    {
        g_pause_slot = g_slot3;
        set_flag(0x73, 0);
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 3 && g_pause_val[2 + 4] == SMM_FN_CONTINUE, "Journal locked: Save / Load Game / Continue");
        set_flag(0x73, 1);
        check(g_img[0x9ABC4E] & 0x08, "flag 0x73 is Save+239E bit 8");
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 4 && g_pause_val[2 + 4] == SMM_FN_JOURNAL, "Journal unlocked: Save / Load Game / Journal / Continue");
    }

    /* --- settings */
    {
        static const char ini[] =
            "[RemoteShop]\r\nShopId = 85h\r\nKeybind = L2+Down\r\nVisitedOnly = 0\r\n"
            "[LoadMenu]\r\nKeybind = R1+Start\r\n";
        static const char ini2[] = "[RemoteShop]\r\nShopId = 0x1G\r\n[LoadMenu]\r\nKeybind = L1+Triangle\r\n";
        memcpy(g_ini_text, ini, sizeof(ini));
        smm_load_settings(0);
        check(g_cfg.shop_id == 0x85 && g_cfg.shop_bind.mods == 0x01 && g_cfg.shop_bind.trig == SMM_TRIG_DOWN && !g_cfg.shop_visited_only,
              "ShopId 85h, L2+Down, VisitedOnly 0 parsed");
        check(g_cfg.save_on && !g_cfg.load_on, "LoadMenu on the SaveMenu keybind: LoadMenu OFF");
        memcpy(g_ini_text, ini2, sizeof(ini2));
        check(smm_load_settings(0) == 2 && g_cfg.shop_id == -1 && !g_cfg.load_on, "bad ShopId keeps Last; face button refused");
        memcpy(g_ini_text, SMM_DEFAULT_INI, sizeof(SMM_DEFAULT_INI));
        check(smm_load_settings(0) == 0 && g_cfg.load_on && g_cfg.shop_on && g_cfg.shop_id == -1, "default settings file parses cleanly (ShopId = Last)");
    }

    /* --- crash report: a fault is logged once and passed on to the next handler */
    {
        volatile int *bad = (volatile int *)(UINT_PTR)0x10;
        int caught = 0;
        __try { *bad = 1; } __except (EXCEPTION_EXECUTE_HANDLER) { caught = 1; }
        check(caught && g_crash_logged == 1, "crash report logged, fault passed on");
    }

    /* --- custom pause buttons: function 1 / text 8AAF is the Load Game button */
    {
        set_flag(0x73, 1);
        g_cfg.show_save = 0; g_cfg.show_load = 1; g_cfg.show_journal = 0;
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 2 && g_pause_val[2] == 0x0001 && g_pause_val[3] == 0x8AAF, "ShowSave 0: only Load Game (1 / 8AAF) + Continue");
        g_cfg.show_save = 1; g_cfg.show_load = 0;
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 2 && g_pause_val[2] == 0x0011 && g_pause_val[3] == 0xC52C, "ShowLoadGame 0: only Save (11 / C52C) + Continue");
    }

    /* --- 1.9.1: no saving in battle (default), World Map refused for the Save keybind */
    {
        g_cfg.show_save = 1; g_cfg.show_load = 1; g_cfg.show_journal = 0; g_cfg.save_in_battle = 0;
        g_img[SMM_WORLD] = 0x04; g_img[SMM_BTLTYP] = 1;
        check(!smm_do_save("test"), "Save keybind in battle, SaveInBattle 0: refused");
        g_pause_slot = g_slot3;
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 2 && g_pause_val[2] == SMM_FN_LOAD && g_pause_val[4] == SMM_FN_CONTINUE,
              "custom pause in battle, SaveInBattle 0: Load Game / Continue, no Save");
        g_cfg.save_in_battle = 1;
        smm_pause_fill(); smm_pause_restore("test");
        check(g_pause_val[1] == 3 && g_pause_val[4] == SMM_FN_SAVE, "custom pause in battle, SaveInBattle 1: Save offered");
        g_img[SMM_BTLTYP] = 0; g_img[SMM_WORLD] = SMM_WORLD_MAP;
        check(!smm_do_save("test"), "Save keybind on the World Map: refused");
        g_img[SMM_WORLD] = 0x04; g_cfg.save_in_battle = 0;
        memcpy(g_ini_text, SMM_DEFAULT_INI, sizeof(SMM_DEFAULT_INI));
        check(smm_load_settings(0) == 0 && !g_cfg.save_in_battle, "default settings: SaveInBattle = 0");
    }

    /* --- 1.9.1: a D-pad keybind does not fire in a pass where Start was handled */
    {
        g_cfg.pause_on = 1; g_cfg.pause_bind.mods = 0x02; g_cfg.pause_bind.trig = SMM_TRIG_DOWN;
        g_img[SMM_SHOULDERS] = 0x02; g_img[SMM_DPAD] = SMM_TRIG_DOWN; g_last_dpad = 0;
        g_pause_phase = 0;
        smm_dpad_step(1);                                  /* the gate would be called next: it is not */
        check(g_pause_phase == 0 && g_last_dpad == SMM_TRIG_DOWN, "D-pad keybind in the same pass as a Start keybind: ignored");
        g_img[SMM_SHOULDERS] = 0; g_img[SMM_DPAD] = 0;
        g_cfg.pause_bind.mods = 0x02; g_cfg.pause_bind.trig = SMM_TRIG_START;
    }

    /* --- 1.9.1: the pause-setup hook tells the custom pause that it started (prompt restore) */
    {
        g_load_pending = 0; g_pause_phase = 1; g_pause_frames = 50;
        smm_on_pause_setup();
        check(g_pause_phase == 2 && g_pause_frames == 0, "pause setup seen: custom pause waits for the close, not the timeout");
        g_pause_slot = g_slot3; smm_pause_fill();
        w16(g_screen_cur, 0xFFFF);
        smm_pause_step();
        check(g_pause_phase == 0, "first main-loop pass after the close: values restored");
    }

    /* --- 1.9.1: settings file saved by Notepad as UTF-8 with BOM or as UTF-16 */
    {
        wchar_t path[MAX_PATH];
        HANDLE h;
        DWORD n;
        static const char bom[] = "\xEF\xBB\xBF[SaveMenu]\r\nSaveInBattle = 1\r\n";
        static const char u16[] = "\xFF\xFE[\0S\0";
        smm_game_path(path, L"ShroMagicMenu.ini");
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        WriteFile(h, bom, sizeof(bom) - 1, &n, NULL); CloseHandle(h);
        smm_read_ini();
        check(g_ini_state == 0 && smm_load_settings(0) == 0 && g_cfg.save_in_battle, "UTF-8 with BOM: parsed, first section kept");
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        WriteFile(h, u16, sizeof(u16) - 1, &n, NULL); CloseHandle(h);
        smm_read_ini();
        check(g_ini_state == 4 && g_ini_text[0] == 0, "UTF-16: recognised, defaults used");
        DeleteFileW(path);                                 /* the next run writes the defaults again */
    }

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails != 0;
}
