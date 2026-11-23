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
    smm_start();

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
        check(r16(0x744C10) == 1 && r16(0x744C14) == 0xFFFF && !g_load_pending, "setup hook, Load pending: pause starts on Load Game (1), request untouched");
        w16(0x744C10, 3); g_load_pending = 1;
        ((void (*)(void))t)();
        check(r16(0x744C10) == 3 && !g_load_pending, "setup hook, unexpected screen: left alone");
        w16(0x744C10, 0xFFFF);
    }

    /* --- remote shop with a stand-in OpenShop and a stand-in shop table */
    {
        static const BYTE stub[7] = { 0x89, 0x0D, 0, 0, 0, 0, 0xC3 };   /* mov [exe+716B50],ecx; ret */
        BYTE *tab = g_img + 0x2DA0000;
        UINT64 p = (UINT64)(UINT_PTR)tab;
        INT32 d = (INT32)(0x716B50 - (0x1518E0 + 6));
        memcpy(g_img + 0x1518E0, stub, 7); memcpy(g_img + 0x1518E2, &d, 4);
        memset(tab, 0, 0x100);
        tab[6] = 3;                                                     /* 3 shops */
        w16(0x2DA0010, 0x13); w16(0x2DA0012, 0x01A0); w16(0x2DA0014, 0x1234); w16(0x2DA0016, 0x05);
        w16(0x2DA0028, 0x85); w16(0x2DA002A, 0x01A1);
        w16(0x2DA0040, 0x66); w16(0x2DA0042, 0x01A2);
        w32(0x716B50, 0xFFFFFFFF);
        memcpy(g_img + 0xBEE968, &p, 8);
        g_img[SMM_WORLD] = 0x04; g_img[SMM_BTLTYP] = 0;
        w32(SMM_CUT_FLAGS, 1); w16(SMM_CUT_PAUSE, 0);

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
        check(smm_load_settings(0) == 2 && g_cfg.shop_id == 0x13 && !g_cfg.load_on, "bad ShopId keeps 0x13; face button refused");
        memcpy(g_ini_text, SMM_DEFAULT_INI, sizeof(SMM_DEFAULT_INI));
        check(smm_load_settings(0) == 0 && g_cfg.load_on && g_cfg.shop_on && g_cfg.shop_id == 0x13, "default settings file parses cleanly");
    }

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails != 0;
}
