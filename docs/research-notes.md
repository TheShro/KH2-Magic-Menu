# KH2 Final Mix (Steam, current build) — memory/menu findings for LuaBackend scripts

Handoff notes from porting KSX's "Save, Load, World Map Anywhere.lua". Everything below was observed
on the user's machine unless marked UNVERIFIED. Addresses are relative to the game module
(`KINGDOM HEARTS II FINAL MIX.exe+XXXXXX`), which is what LuaBackend 1.8.0+ expects (no `offset`).

## Environment / conventions
- LuaBackend 1.8.0+. Guard: `GAME_ID == 0x431219CC and ENGINE_TYPE == "BACKEND"`.
- API used: ReadByte/ReadShort/ReadInt, WriteByte/WriteShort/WriteInt/WriteFloat, ConsolePrint, `_OnInit`, `_OnFrame`.
- Cheat Engine notation `exe+716884` == Lua address `0x716884`. Module base seen once: 0x7FF7A5200000 (changes per run).
- Old-build addresses in older scripts (e.g. `offset = 0x56454E` style) do NOT apply. Pattern scans and offset
  shifting failed; what worked was scanning for live values (buttons) and dumping memory around hits.
- The user prefers text output (pasted console logs/hex) over screenshots, and wants version numbers shown
  in the install message so they can confirm the script reloaded.

## Input
| Address | Meaning |
|---|---|
| 0x7133EA | D-pad byte: up 0x10, right 0x20, down 0x40, left 0x80 |
| 0x7133E8 | previous-frame copy of the same |
| 0x7133EB | shoulder/face byte: L2=0x01, R2=0x02 (L2+R2 = 0x03), L1+R1 = 0x0C, triangle 0x10, circle 0x20, cross 0x40, square 0x80 |

Compare with `==` (e.g. `ReadByte(0x7133EB) == 0x03`) so extra buttons don't trigger combos.

## Menu system
| Address | Meaning |
|---|---|
| 0x716884 | menustate: 1 = no menu, 2 = a menu is open. **NEVER WRITE** (writing 2 blue-screened the game) |
| 0x900724 | currentmenu: -1 none, 3 save point menu, 8 party/status menu, 50 true in-combat pause menu. Writing it does nothing |
| 0x716B48 | menu request queue (-1 = none) |

Menu IDs: 3 = save point menu (Save / World Map / Title Screen, more unlock later), 8 = party/status menu
(the out-of-combat Start menu), 50 = the true in-combat pause menu (no buttons, just a pause).

### The queue trick (works, user-confirmed)
- The game reads 0x716B48 when a menu **closes** and opens the queued ID next, then resets it to -1.
- Writing it while no menu is open is ignored/cleared. It only works if written while a menu is open
  (and the slot is -1).
- So a combo cannot open a menu directly; the player opens any menu (party menu out of combat, pause in
  battle), the script writes the ID, and closing the menu opens the queued one.
- Queuing menu 50 from the field crashed the game. Avoid.
- Working implementation: arm on the combo; each frame, if `ReadInt(0x716884) == 2` and
  `ReadInt(0x716B48) == 0xFFFFFFFF` then `WriteInt(0x716B48, id)`; disarm after writing or after ~1800 frames.
  (Check this before any "shoulder ~= 0x03" early return, otherwise it never runs after the buttons are released.)
- The final script (v21) uses: combo + D-pad up -> queue 3, D-pad right -> queue 8.

## Pause menu option tables (found, but could NOT make extra buttons appear)
Each table is a count followed by 4-byte entries (2-byte function, 2-byte text ID).
- Table A: count at 0x2AB319A, entries at 0x2AB319C/9E, 31A0/A2, 31A4/A6, 31A8/AA
  (flag byte 0x2AB3198, menupause byte 0x2AB31AE). The game resets this as the pause menu opens.
- Table B: count at 0x2AB34DA, entries at 0x2AB34DC/DE, 34E0/E2, 34E4/E6, 34E8/EA. Persists what we write.
- Table C ("cutscene"): count at 0x2AB31DA, entries at 0x2AB31DC/DE, 31E0/E2. Persists what we write.
- Vanilla at menu 50: A = count 2: (2,8AB0),(1,8AAF); B = count 3: (6,8AB4),(3,8AB1),(4,8AB2);
  C = count 4: (6,8AB4),(3,8AB1),(0x0C,AF3A),(4,8AB2).
- Text IDs seen: 8AAF, 8AB0..8AB4, AF3A, C52C (C52C was the original script's "Load" entry, function 0x11).
- Result: writing the original script's values into all three tables while menu 50 was open (v20) showed
  NO extra buttons. The tables alone do not draw the buttons. Abandoned at the user's request.

## Unresolved from the original script (old-build names, never located in this build)
- `loadingheart` (float, original wrote 1200)
- `savepointsmenuslots` (byte, forced to 3 every frame)
- `menuflag` / `menuflag1` / `menupause` (flags the original wrote)
- `savepointcheck`, `soracurrentmodel` were never used by the original logic.
- The original D-pad Down (cutscene skip, minigame retry, others) was never ported. The user has
  another mod by the same author that opens that menu in combat and does not need it ported.
- Original features not ported: D-pad Down cutscene skip/retry table writes.

## Battle menu block flag (found, user-confirmed)
| Address | Meaning |
|---|---|
| 0x2A114E0 | byte: 0 out of battle, non-zero in battle (0x06 seen). While non-zero, Start gives the true pause (50) and the party menu (8) is refused/closed instantly. Writing 0 every frame makes Start open the party menu in battle |
- This replaces KSX's "Open Menu in Battle" menuflag1/menuflag2 (old 0x2A0EBE0 / 0x2A0EC60 - 0x56454E). Only one flag is needed in this build.
- Ported script: "Open Menu in Battle.lua" v2.0-steam.
- Found with a snapshot finder (bytes 0 out of battle / non-zero in battle across all writable sections), then shortlisted by hand.
  Other survivors were pointers (e.g. 0x2A11220-35), floats (0x2A0F832/33) and enemy data at 0x2B04xxx-0x2B05xxx (0x30 stride). Never zero those.
- Other flag-like bytes that also flip in battle (untested individually, role unknown): 0xABAD19, 0xABAD1C, 0xAD6A10, 0x2A11208, 0x2A11402, 0x2A11404.
- Queue trick note: queuing 8 from the in-combat pause does open the party menu, but the game closes it the same frame unless 0x2A114E0 is 0.

## Other leads / facts
- Other values seen: 0x9006B4 (stays 1 in field, 4 at title), 0x716C34 (0).
- Code: `axaAppMain` module at about exe+0x14F1E0; the menu-state code was decoded rip-relative.
- KSX's Nexus page "Save Menu in Pause" (nexusmods.com/kingdomheartsfinalmix/mods/149) is hidden. KSX's video
  (youtube.com/watch?v=UCL6CZ1L-Lk, Sept 2022, Epic build) lists the controls and mentions a "1st Pause Layout"
  that can black-screen on the World Map. No updated script for the current build was found.
- A ReShade "saved screenshot" banner at the top of the screen is not a game message.

## Testing notes
- Console prints appear in the LuaBackend console; a hard crash loses the log (v17).
- Two kinds of "pause": party menu (Start, out of combat, ID 8) and true pause (in combat, ID 50).

## Synthesis shop via the queue trick (attempted in Shro's Magic Menu v1.2-v1.7, abandoned)
- Menu ID 6 = moogle synthesis shop. A real moogle does NOT use the queue: it opens a talk menu
  (currentmenu -1, menustate 2) and switches straight to 6.
- Queuing 6 behind the party menu (8) is accepted (the game consumes the queue and currentmenu becomes 6), then the game freezes.
- Values a real moogle sets that the queue path does not:
  | Address | Real moogle | Queued | Notes |
  |---|---|---|---|
  | 0x716B50 | shop ID | -1 | Per moogle: Hollow Bastion = 0x13, another moogle = 0x85. Reset to -1 after the shop closes |
  | 0x716B68 | 64-bit pointer | 0 | Set the frame the real shop opens. 0x7FF7A5CFD410 (~exe+0xAFD410) at both moogles seen |
  | 0x716B74 | 0x81916860 | 0 | Same at both moogles |
  | 0x716B84 | 1 (3 on close) | 0 | Same at both moogles |
  | 0x9006B1 | 1 | 0 | Only while the real shop is open |
- Writing all of the above before queuing (v1.7, level 3) still froze. Note 0x716B68 and 0x9006C8/D0 read
  back as 0 on the frame the queued menu 6 opened, so the game seems to clear/rebuild them on the 8 -> 6
  switch; the missing setup is likely done by the moogle's event script, outside the dumped regions.
- Next step if resumed: snapshot-diff all writable memory mid-moogle-conversation vs. party menu open,
  or try queuing 6 behind a talk menu (-1) instead of behind 8.

## Battle menu flag: mission battles override it (found later)
- 0x2A114E0 = 0 works in ordinary battles everywhere tested (Timeless River, Land of Dragons, etc.).
- It does NOT work in mission battles: scripted fights whose pause screen shows an objective
  ("hold out while X", "defeat the enemies"), e.g. the run-up to the Battle of 1000 Heartless.
  Start still gives the true pause (50) there. Menus out of battle are unaffected.
- Ruled out as the mission lock (zeroing each every frame changed nothing): 0xAD6A10, 0x2A11402 (both flip
  with the pause menu opening/closing), 0x2A11404.
- Left as is: forcing the party menu mid-mission may break timers/objectives.
- Finder results (mission vs. ordinary battle, v1.9-finder) tested in v1.9.1/v1.9.2 by forcing the ordinary value
  every frame, alone and all together: NONE unlocked the party menu.
  0x2A109C0 (int, 1 / -1, but also -1 at times during a mission), 0xABAD18/19/1C (0 in mission, 1 ordinary),
  0xAC152C, 0xABAD88, 0x2A11401 (0x84), 0xABC510, and the mission-only pointers at 0x2A0FF68, 0x2A0FF70, 0x2A0F8C0.
  The finder printed only the first 80 hits, so the 0x2AB0000 pause-table hits were never seen.
- WARNING: forcing those values and then restoring the old ones STALLED the mission (it stopped progressing).
  Fixed by Retry after defeat / reloading the save (memory only, save file unaffected).
  Do not write mission state; only read it during missions.
- Recommended next step: Cheat Engine "Find out what writes to" exe+900724 (currentmenu) while pressing Start in a
  mission battle (writes 50) vs. an ordinary battle with the script on (writes 8); read the cmp/test/jumps before
  the write and decode the rip-relative addresses to find the actual check.
- If resumed: snapshot-diff a mission battle vs. an ordinary battle in the same world (both in battle),
  then "unchanged" across several mission fights. The mission pause draws objective text, so look near the
  pause option tables (0x2AB31xx-0x2AB34xx) first (unverified guess).

## Menu code decoded (CE disassembly, mission battle)
- axaAppMain = exe+0x14F1E0 exactly (proved by the queue slot 0x716B48 = axaAppMain+0x5C7968).
- axaAppMain+2590 = OpenMenu(ecx = menu ID):
    if byte[0x9006B0] != 0 (menu already open): if [0x716B48] != id then [0x716B48] = id   (the queue trick)
    else: [0x900724] = id (currentmenu); [0x716B48] = -1
- axaAppMain+2400 = menu close handler: reads 0x9006B4 (game mode), clears callback ptr 0x9006C0, checks menustate
  0x716884 == 2, 0x7169B4 == 2, compares currentmenu with 0x0A, checks 0x716C34 (if non-zero sets it to 2 and calls
  another routine), then resets 0x9006B8 / 0x900718 / 0x900720.
- New: byte 0x9006B0 = "a menu is open" (1 in party menu, 0 in field), decides open-now vs queue.
- axaAppMain+25C0 passes the shop pointer 0x716B68 to another function.
- The 8-vs-50 decision is in OpenMenu's CALLER. Next: conditional breakpoint at axaAppMain+2590 with RCX==0x32 in a
  mission battle, take the return address from the stack trace and disassemble before it.

## Pause / mission lock decoded (CE, mission battle) — THE ANSWER
- Start-button check = axaAppMain+80D0 (exe+0x1572B0), runs every frame:
    skip if dword [exe+ABB878] != 0 (pause-disable bits; set by axaAppMain+8070, cleared by +8080)
    skip if byte [exe+9BA8D0] == 0
    skip if a menu is open (axaAppMain+23E0), if [axaAppMain+7BE0()+0x108] != 0, if exe+19BA60() is true
    skip unless (exe+39B780(0))->byte[+8] & 0x02 (Start pressed)
    obj = exe+3AD540()
    if obj != null: exe+2E4C20(obj, 0)      <- MISSION pause (always 50, objective text)   [call at axaAppMain+8145]
    else: if exe+3A0A30() false: exe+2E4950(0, -1)  <- normal pause (0x2A114E0 trick works here)
- Branch to patch: axaAppMain+8127 (exe+0x157307): 75 17 = jne +8140. NOP (90 90) -> always the normal path.
- Pause selection (exe+2E4950..2E4B1C): key -> row in table exe+5BA170 (32-byte rows: word key, word menu ID):
    0->0, 1->3, 2->4, 3->5, 4->2, 5->6, 6->1, 7->7, 8->50, 9->-1, 10->8, 14->9, 11->51, 12->52, 13->53, 15->10, 16->900.
    Keys in list exe+5BA3B0 (4, 6, 15, 2) call exe+31AD30(row menu) then open the key-10 row (menu 8).
    If bit 0x04 of [exe+749830] is set, keys 0/8 become 15. 0x749830 = 0 in field and ordinary battle with the script.
- OpenMenu call sites in that function: exe+2E4A59 (ecx=0x32 direct) and exe+2E4AFA (row menu ID).

## Cutscene detection + final mission fix (Shro's Magic Menu v2.0)
- LuaBackend CAN write code: WriteShort(0x157307, 0x9090) / (0x157307, 0x1775) toggles the mission-pause
  branch cleanly (user-confirmed, pausing in between). One 2-byte write so no half-patched instruction.
- With the patch, mission battles give the party menu (8). Cutscenes also use that branch (skip menu),
  so the patch must be off in cutscenes.
- Cutscene flags (snapshot finder, 16 cutscene vs 15 field/battle snapshots, several worlds):
    0xABB854 dword: 0x04 in cutscenes, 0x01 otherwise (game-state bits; axaAppMain+7F50 ORs bits into it,
             +7FD0 returns early if non-zero)
    0x2A115A0..A8: 04 00 02 00 06 00 B4 8A 05 in cutscenes, all 0 otherwise (cutscene pause setup; 0x8AB4 is a
             pause text ID)
    Others (less clear): 0x2A1115C (0 cut / 1 other), 0x717418 (0/1), 0xABCBD1 (1/0), 0xAC1528 (1/0),
             0xABB5E8 & 0xABB708 (3/1).
- v2.0: patch on unless (ABB854 & 4) or word[2A115A0] != 0. L2+R2 + D-pad Down toggles the mission part.
  Refuses to touch the bytes if they are not 75 17 / 90 90.
- Removing the script (delete + F1 reload) does NOT restore the bytes (no unload hook).
  Before removing: L2+R2 + D-pad Down (writes 75 17 back), then delete + F1. F1 with v2.0 still present is fine.
- v2.1 tried "patch only in battle" using 0x2A11208 as the battle signal: FAILED, 0x2A11208 stayed 0 for a whole
  mission battle. 0x2A11208 is NOT a battle flag. Reverted to v2.0 (patch everywhere except cutscenes).
  A reliable battle flag the script doesn't overwrite is still unknown (0x2A114E0 is zeroed by the script).

## KSX's updated scripts (Steam GL addresses, from the author)
- Build detection: ReadLong(addr) == 0x7265737563697065 ("epicuser") at 0x585E59 = Steam GL, 0x585E09 = Steam JP,
  0x585B61 = Epic. Flag/table addresses are the same on GL and JP; code addresses differ (savemenu GL 0x31D504 vs JP 0x31D284),
  so our code patch at 0x157307 is GL-only (the 75 17 byte check protects other builds).
- World ID byte: 0x717008 (Epic 0x716DF8). KSX does NOT zero the menu flags when world == 0x0F (World Map).
- Open Menu in Battle zeroes TWO flags: 0x2A114E0 (ours) and 0x2A11560 (new to us).
- 0x2A11560 = flag byte of a pause-option block: count at 0x2A11562, then 4 x (2-byte function, 2-byte text ID),
  "menupause" byte at 0x2A11576 (KSX writes 15 = pause key 15 in the table at exe+5BA170).
  It was inside the v1.9 finder's scan range but after the 80-line print cutoff, so it was never seen. Possible
  mission-pause trigger: untested.
- Other pause tables: menuslots 0x2AB275A (count, then 4 entries); cutscene table 0x2A115A2 (count, 2 entries), flag 0x2A115A0
  (matches our cutscene finder hit 0x2A115A0-A8 = 04 | 0002 | 0006 8AB4 ...).
- Function IDs / text IDs used: 0x01/8AAF, 0x11/C52C (Load), 0x0A/8AB7, 0x06/8AB4, 0x02/8AB4, 0x03/8AB1, 0x0C/AF3A,
  0x04/8AB2, 0x05/8AB3. KSX's D-pad Left writes the vanilla tables back:
  slots (6,8AB4)(3,8AB1)(0C,AF3A)(4,8AB2); menuslots (2,8AB4)(3,8AB1)(0C,AF3A)(4,8AB2); cutscene (6,8AB4)(5,8AB3).
  KSX's D-pad Right = custom pause with Load: flag 0x2A11560=1, menupause=15, slots (1,8AAF)(11,C52C)(0A,8AB7)(6,8AB4),
  menuslots (2,8AB4)(1,8AAF)(11,C52C)(4,8AB2), cutscene (1,8AAF)(11,C52C).
- loadingheart (unresolved before) = float at 0x8EC5B0; KSX writes 1200 on every combo.
- savepointsmenuslots (unresolved before) = byte 0x31D504 (in the code section), KSX writes 3 every frame.
- KSX reads input from 0xBF3270 (D-pad) / 0xBF3271 (shoulders), same bit layout as our 0x7133EA / 0x7133EB.
- Our v20 "tables don't draw buttons" result used 0x2AB3198/0x2AB34DA/0x2AB31DA, not KSX's 0x2A11560 block / 0x2AB275A,
  which is probably why it failed.

## Custom pause works (v2.2-test, user-confirmed) -> v2.3
- Writing KSX's block (0x2A11560 flag=1, 0x2A11576=15, count/entries at 0x2A11562, plus 0x2AB275A and cutscene tables,
  loadingheart 0x8EC5B0 = 1200.0f) and letting the special-pause branch through (mission patch off) makes Start show
  a custom pause. Menu ID is still 50.
- Shown in the field: Load Game / Save / Jiminy's Journal / Continue (display order differs from table order).
- Function / text IDs: 0x01 / 8AAF = Save (opens the save point menu, same as queueing 3),
  0x11 / C52C = Load Game (goes to the load screen as if booting), 0x0A / 8AB7 = Jiminy's Journal, 0x06 / 8AB4 = Continue.
- The game clears one written value itself (likely the 0x2A11560 flag); compare-and-restore leaves it alone.
- v2.3: L2+R2 + D-pad Up arms this pause (replaces the queue trick); restores on close or after 1800 frames.
  Also: Steam-global-only check via "epicuser" at 0x585E59; World Map (0x717008 == 0x0F) skips the flag zeroing,
  the mission patch and the anywhere pause.
- Not yet tested: the anywhere pause in an ordinary battle and in a mission battle.

## Pause function IDs (game over screen: Retry / Prepare & Retry / Continue / Load Game)
| Function / text | Button | Status |
|---|---|---|
| 0x01 / 8AAF | Save (opens save point menu) | tested |
| 0x11 / C52C | Load Game | tested (KSX took it from the game over screen) |
| 0x0A / 8AB7 | Jiminy's Journal | tested |
| 0x06 / 8AB4 | Continue (resume) | tested |
| 0x03 / 8AB1 | Retry | inferred from tables + game over screen |
| 0x0C / AF3A | Prepare & Retry | inferred (Final Mix addition) |
| 0x04 / 8AB2 | game over "Continue" | inferred |
| 0x05 / 8AB3 | Skip (cutscene table) | inferred |
| 0x02 / 8AB0 | unknown | |
- No pause function opens a shop; a moogle shop needs its event script (see the synthesis section). Shop button: not possible this way.

## Save point menu: World Map missing (v2.3.1)
- The save point menu shows World Map only where the story allows leaving; in story-locked stretches it is
  Save / Title Screen only. Earlier tests (Timeless River etc.) had it; the Hollow Bastion run-up does not.
- KSX's "savemenu" byte 0x31D504 (code section, GL only; JP 0x31D284) = save point menu option count; he forces 3
  every frame (Save / World Map / Title Screen).
- v2.3.1 writes 3 there only while the anywhere pause is armed/open (compare-and-restore on close), only if the
  current value is <= 3, and prints the original value once. Setting FORCE_WORLD_MAP (default true).
- Risk: leaving a world mid-story via a forced World Map may break story flow.
- Not yet merged into v2.4-test (retry pause).

## External references (researched 2026-11-18)
### GoA ROM randomizer script (KH2FM-Mods-Num/GoA-ROM-Edition, "F266B00B GoA ROM.lua"), Steam Global block
- Detection: ReadString(0x9A98B0, 4) == 'KH2J' (save data header); used for Steam Global and JP.
- Now = 0x0717008 (current location): World byte +0x00, Room byte +0x01, Door short +0x02, Map +0x04, Btl +0x06,
  Evt +0x08, PrevPlace short +0x30. (KSX's "SteamWorld" 0x717008 = Now+0x00.)
- Save = 0x09A98B0 (save data; location recorded at Save+0x0C). Sve = 0x2A0C540.
- BtlTyp = 0x2A11404: battle status, 0 = out of battle, 1 = regular, 2 = forced (comment on the PS2 line).
  Matches our logs (02 at the Hollow Bastion mission start). -> battle + mission detector.
- CutNow = 0x0B64F98 (cutscene timer), CutLen = 0x0B64FB4, CutSkp = 0x0B64F9C.
- Pause = 0x0ABB878 (= the pause-disable dword checked first by the Start-button code), Timer = 0x0ABB850.
- BtlEnd = 0x2A0FCE0, Menu1 = 0x2A11110 (+0x8 per command menu entry), React = 0x2A11162, Cntrl = 0x2A171E8,
  Music = 0x0ABAD44, GamSpd = 0x0717424, CamTyp = 0x0718CA8, TxtBox = 0x074DF20, DemCln = 0x2A0F8B4,
  Slot1 = 0x2A23598 (+0x278 per party slot), Sys3Pointer = 0x2AE5E50, Btl0Pointer = 0x2AE5E58,
  Obj0Pointer = 0x2A25030, ARDPointer = 0x2A0F828, MSN = BASE + 0x0BF33C0.
- Warp(W,R,D,M,B,E): writes Now+0x00..0x08 and Save+0x0C. GoA triggers it while on the World Map (place 0x000F),
  rewriting the destination as the game transitions; writing Now alone in the field does not load anything.
### KH2 Lua Library (KH2FM-Mods-equations19/KH2-Lua-Library)
- Shared address library (require "kh2lib") so scripts survive game patches: the library is updated, scripts aren't.
  Versions: Epic/Steam Global/Steam JP 1.0.0.9, Epic/Steam 1.0.0.10. Addresses live in its VersionTemplate.lua.
### OpenKH Mods Manager (openkh.dev, creatingMods)
- mod.yml asset methods: copy, binarc, imgd/imgz, kh2msg (replace text by ID), areadatascript / areadataspawn
  (ARD spawn scripts), listpatch (cmd, item, shop, arif, place, localset, prty, fmlv), synthpatch (Mixdata.bar).
- With LuaBackend Hook configured for Mods Manager, mods can bundle their own Lua scripts.

## Native module (started 2026-11-18)
- Loading route (from the user's install, not from Re:Fined's code): Panacea runs as DBGHELP.dll in the game folder,
  chainloads LuaBackend.dll; panacea_settings.txt mod_path = <game>\openkh\mod. Built output has mod\kh2\dll\,
  which is where mods' DLLs go. A mod.yml `copy` to `dll/<name>.dll` should get ours loaded. UNVERIFIED until the
  step 1 log appears. MinHook (BSD) licence is present in the game folder; usable for x64 hooks later.
- Build: MSVC x64 (vcvars64, cl /LD /O2 /MT), expect "8664 machine (x64)".
- Step 1 (ShroMagicMenu-native-step1.zip): read-only probe, logs to ShroMagicMenu.log next to the exe.
  Signatures (verified against the CE dumps and by running the real C on a fake image):
    OpenMenu    "80 3D ?? ?? ?? ?? 00 74 0F 39 0D ?? ?? ?? ?? 74 17 89 0D ?? ?? ?? ?? C3 89 0D ?? ?? ?? ?? C7 05 ?? ?? ?? ?? FF FF FF FF C3"  -> exe+151770
                rip: +0x00 (disp +2, len 7) menu-open byte; +0x09 (disp +2, len 6) queue; +0x18 (disp +2, len 6) currentmenu
    StartCheck  "F6 40 08 02 74 2B E8 ?? ?? ?? ?? 48 85 C0 ?? ?? E8 ?? ?? ?? ?? 84 C0 75 18 BA FF FF FF FF 33 C9 E8"  -> exe+1572F9
                site = +0x0E (75 17, wildcarded so the Lua patch doesn't break the scan); call at +0x06 -> exe+3AD540
    PauseSelect "83 F9 05 75 05 E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? C1 E8 02 A8 01 74 0E F7 C6 F7 FF FF FF B8 0F 00 00 00 0F 44 F0 33 DB 4C 8D 25 ?? ?? ?? ??"  -> exe+2E4983
                rip: +0x0A (disp +2, len 6) mode bits 749830; +0x27 (disp +3, len 7) pause table 5BA170
- Step 2 plan: frame hook + call OpenMenu(3) directly on L2+R2 + D-pad Up.
- Re:Fined: its repo carries no-agents AGENTS.md/CLAUDE.md; its source and DLLs were deliberately not read.
- Step 1 VERIFIED in game (2026-11-18 19:41): Panacea loaded the DLL from mod\kh2\dll; all 3 signatures matched exactly
  once in the real exe at the expected offsets; all 8 derived addresses matched. Module base that run 0x7FF692230000.
- Step 2 (ShroMagicMenu-native-step2.zip): hook at the Start-button code's "mov edx,-1" (StartCheck+0x19 = exe+157312,
  bytes BA FF FF FF FF, no rip-relative operand). Cave: align stack, call smm_on_start; if L1+R1 (shoulder byte 0x0C)
  -> OpenMenu(3) + follow-up exe+151480(3), jump to the loop rejoin StartCheck+0x31 (exe+15732A, after the pause call);
  else run the copied 5 bytes and jump back to exe+157317. New signature PauseTail
  "0F BF 4B 02 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 7C 24 50" -> exe+2E4AF6 (calls OpenMenu, then exe+151480).
  Tested natively with the shipped cave + the game's real OpenMenu bytes (ms_abi modelled). Awaiting in-game test.
- Step 2 VERIFIED in game (2026-11-18 19:49): PATCHED line, cave allocated at 0x7FF692220000 (just below the module),
  L1+R1 + Start opened the save point menu directly, 5 times, user-confirmed working nicely.
- Step 3 (ShroMagicMenu-native-step3.zip): three hooks, all relative to StartCheck match S:
    S+0x0B exe+157304 "48 85 C0 75 17" (also accepts the Lua's 48 85 C0 90 90): special-pause decision.
      Cave keeps rax (obj), calls smm_on_special(obj): 0 -> S+0x10 normal path, 1 -> S+0x27 special path.
      Rule: obj==0 normal; BtlTyp==0 keep; in battle keep only if cutscene, World Map, Lua anywhere pause armed
      (pause block first entry (1,8AAF)) or R2 alone held; else normal (party menu).
    S+0x19 exe+157312: L1+R1 + Start (step 2), handled -> S+0x31.
    S+0x31 exe+15732A "33 D2 48 8B CB": per loop pass: clear 0x2A114E0 unless world==0x0F; log BtlTyp / cutscene.
  All-or-nothing install in DllMain; Steam Global only (epicuser marker). Tested natively incl. mutations.
  The Lua v2.3.1 mission patch disables itself ("unexpected bytes") once this module owns exe+157304.
- Step 3 VERIFIED in game (2026-11-18 20:18): three PATCHED lines; cutscene detected, then BtlTyp 0 -> 2 at the
  mission start (2 = forced, confirmed); Start in the mission -> party menu path (x2); R2 + Start -> game's pause kept.
  Still to confirm: BtlTyp 1 in an ordinary battle, cutscene skip prompt, Lua anywhere pause in battle.
- Step 3 fully confirmed by the user (2026-11-18 20:22): party menu in ordinary and mission battles, R2 + Start keeps
  the game's own pause, cutscene skip prompt intact, Lua anywhere pause unaffected. "Everything worked as expected."

## Load screen found (2026-11-18, static analysis of the user's own exe, which is unencrypted on disk)
- Pause button handler exe+31DE27..31DFF1: index = selected button (exe+2F9440 on menu obj [exe+BEF528]);
  fn = word ids[index] (ids list pointer [exe+BEF538], rebuilt every pause by the builder exe+31F3A0);
  jump table exe+31DFF4 [fn-1], fn 1..0x13.
    0x01 Save -> exe+31D850(3) | 0x11 Load -> exe+31D850(1) | 0x0A Journal -> exe+31D850(4)
    0x06 Continue -> exe+2EB790 | 0x03/0x04 -> exe+3A3DD0(fn) | 0x05/0x13 -> exe+31DEC9 (skip) | 0x0C -> exe+31DEFE
  then (unless fn 0x0C) exe+2F6500 = set byte exe+BEE658 = 1 (close the button page).
- exe+31D850(n): word exe+744C14 = n (requested screen). exe+744C10 = current screen. These belong to the pause
  (menu 50) screen system: setup exe+31D200 (current 9, request -1, starts task exe+31D690),
  teardown exe+31D0B0 (both -1). Screen 9 = main button page; 3 save, 1 load, 4 journal.
  Pointer row in the pause key table exe+5BA170: row 0x5BA270 = key 8 / menu 0x32 / setup 31D200 / teardown 31D0B0.
  Row 0x5BA290 = key 9 / menu -1 / setup 31EE60 / teardown 31ED60 (second id-list user, likely the game over screen).
- So Save from the pause never changes currentmenu (it is a sub-screen of menu 50), which is what the CE traces showed.
- Step 4 (ShroMagicMenu-native-step4.zip): L2+R2 + Start -> OpenMenu(50)+follow(50); per loop pass, once
  current==9 for 10 frames and request<0: write request=1, close byte=1; give up after 300 frames.
  Addresses derived at load: sig "PauseButtons" (exe+31DE82) -> jump table [0x10] -> 'B9 01 00 00 00 E8' -> request fn
  '66 89 0D d32 C3' -> request word; sig "ButtonsTail" (exe+31DFC1) -> call at +0x13 -> 'C6 05 d32 01 C3' -> close byte.
  Load combo optional: missing/odd code disables only Load. Tested against the real exe (harness4).
- Step 4 v0.4 CRASHED in game right after L2+R2 + Start (OpenMenu(50)+follow-up(50) called directly from the field).
  v0.5: open the pause like the special-pause path does: exe+2E4C20 ends with jmp exe+2E4950(key 8, -1) (the pause
  opener: key -> pause table row -> menu). Opener found as the call at StartCheck+0x20, cross-checked: opener+0x33 ==
  PauseSelect match. Load writes unchanged. Awaiting in-game test.
- Step 4 v0.5 also CRASHED (pause opener 2E4950(8,-1) with no slot).
- PAUSE SLOTS (exe+3AD540 = special-pause lookup): five 0x40-byte slots at exe+2A114A0, slot i at +i*0x40 (i=1..5);
  highest active (word at +0 > 0) wins. Slot 1 = 0x2A114E0 (battle pause; the "menu block flag" = this slot's active word),
  slot 3 = 0x2A11560 (KSX / Lua anywhere pause block), slot 4 = 0x2A115A0 (cutscene block). Layout (slot 3): +0 active byte,
  +2 option count, +4.. (function, text) pairs, +0x16 pause key.
- Special-pause routine exe+2E4C20(slot, 0): 31D860(slot, 0) -> [exe+BEF4A0]=slot, [exe+BEF4A8]=0; ...; jmp 2E4950(8,-1).
  The pause's screen system reads [BEF4A0] when it starts -> null = crash (0.4/0.5).
- v0.6: L2+R2 + Start -> fill slot 3 (KSX layout) -> 2E4C20(slot3, 0) -> wait for screen 9 x10 -> request 1 + close
  -> restore slot 3 after the pause closes (screen -1). Steps aside if slot 3 already active. Awaiting in-game test.
- Step 5 v0.7 (user's design): auto-load dropped. R2 + Start -> fill slot 3 + KSX's other tables + loadingheart (as the Lua)
  -> 2E4C20(slot3, 0) -> restore once the pause screen word (744C10) returns to -1 (or 300 frames if it never starts).
  In battle the special decision now always takes the normal path except cutscene / World Map / Lua pause armed
  (R2 no longer keeps the game's own mission pause). L2+R2 + Start = normal Start. Tested on the real exe (harness5).


## Native module step5 v0.8 / v0.9 (2026-11-18)
- v0.8: Re:Fined's retry takes precedence. R2 + Start in battle no longer opens the custom pause.
  - Mission (special slot active): smm_on_special returns 1 when R2 is held -> mission's own pause.
  - Other battles: slot 1 (exe+2A114E0, derived as slot3 - 0x80) is cleared every frame by the
    party-menu feature; the frame hook now remembers the game's value (06) first. R2 + Start writes
    it back, calls 2E4C20(slot1, 0), and suspends clearing until screen word 744C10 returns to -1.
- v0.9: R1 alone + Start (shoulder byte 0x08) = save point menu, same path as L1+R1 (kept).
  Load stays behind the custom pause as a confirmation step.
- Tests: harness5 (real exe) O2/O0 + mutation 1, harness3: all pass. Untested in game.

- Correction: Re:Fined's Retry / Prepare & Retry are on the GAME OVER screen, not the pause. v0.8's premise was wrong.
## Native module step5 v1.0
- Retry (func 3, text 8AB1) -> exe+3A3DD0(3): uses mission object [2A0FF70], posts event 0x8B. Func 4 (game-over Continue) -> event 0x8D.
- Prepare & Retry (func 0x0C, text AF3A) -> exe+3A3F40: returns 0 if [2A0FF68] is null, else opens a confirm box (text 0x754B).
- Slot 3 holds 4 buttons (+0x04..+0x13); +0x16 is the pause key, so a 5th pair would overwrite it.
- v1.0: R2 + Start = custom pause everywhere but the World Map. In battle with 2A0FF68 and 2A0FF70 set:
  Retry / Prepare & Retry / Load Game / Continue; otherwise Save / Load Game / Journal / Continue.
  Slot-1 re-enable logic removed. L1+R1 combo removed; R1 + Start = save point menu.
## Native module step5 v1.1
- Button builder 31F3A0: slot ptr [BEF540]; count byte +2; pairs at +4+i*4 (func) / +6+i*4 (text). Button list [BEF538] is malloc(0x12) = 9 words.
- slot +0x16 is a signed WORD read at 31F21B (negative = skipped) - probably the info/banner text, not a "pause key". 5th pair would overwrite it.
- Func 4 (game-over Continue) -> 3A3DD0(4): calls 3A5650(9), then event 0x8D. No mission pointer needed.
- v1.1: battles without a mission: Save / Load Game / "Retry" (func 4, text 8AB1) / Continue. Field and mission layouts unchanged.
- In game (v1.0): field pause, ordinary battle pause, R1 save confirmed working by the user.
## Native module step5 v1.2
- v1.1 log: R2 pause in field at :47, no restore line, battle at :51, Start at :52 -> "Lua anywhere pause armed, kept" -> game opened slot 3 (field layout).
  Cause: our main-loop hook (15732A) does NOT run while the pause is open, so it never saw screen word >= 0;
  restore only came from the 300-frame "never started" timeout. Teardown 31D0B0 does write 744C10 = -1 and clears BEF4A0/BEF4A8 (unless currentmenu == 0x32).
- Fix: disarm slot 3 (+0 active byte) right after 2E4C20 returns; buttons kept until close; timeout 120 frames; Lua-armed check ignored while our pause is pending.
## Native module step5 v1.3
- v1.2 in game: slot-disarm fix works (field pause restored 7 s later on resume; battle R2 got the battle layout).
- Function 4 (game-over Continue, labelled Retry) pressed from our battle pause CRASHED the game. Func 3 shares 3A3460 + event post, assumed unsafe too.
- v1.3: SMM_FEATURE_RETRY_BUTTONS 0 -> one layout everywhere (Save / Load Game / Journal / Continue). Restore now logs each value the game changed.
- Idea for later: a real retry would need the game-over flow itself (e.g. trigger Game Over so Re:Fined's screen appears) - not attempted.
## Native module step5 v1.4
- v1.3 in game: field and regular battle R2 OK, R1 save OK in a forced battle. In a forced (mission) battle the game had slot 3 ACTIVE itself
  (lookup returned it -> our hook sent Start to the party menu; R2 saw slot 3 busy and skipped). So mission pauses can live in slot 3.
- loadingheart 8EC5B0: the game resets it to 0 after our pause (logged "left alone") - expected.
- v1.4: if slot 3 is active, use slot 2 then slot 0, only if all 0x40 bytes are zero; same fill/disarm/restore; the game's slot 3 untouched.
## Native module step5 v1.5
- v1.4 in game: mission R2 used free slot 2 and worked; field, regular battle, R1 all fine.
- User: R2 sometimes gave the party menu right after a pause -> our "pending restore" phase blocked R2 for up to 120 frames.
- v1.5: any Start press reaching exe+157312 ends the pending phase first (the game is unpaused, so our pause is closed). No lockout.
- User retiring the Lua script now.
## Native module v1.6 (settings)
- ShroMagicMenu.ini next to the game exe (written with commented defaults if missing; not overwritten by Mods Manager).
  Sections: [SaveMenu] Enabled/Keybind, [CustomPause] Enabled/Keybind/ShowSave/ShowLoadGame/ShowJournal/ForceWorldTravel,
  [PartyMenu] InBattle/InMissions, [Log] Enabled/Verbose. Two-pass parse (quiet, then logged once the log is open).
- Keybinds: shoulder mods (shoulder byte & 0x0F) + one trigger (Start, or D-pad bit in 0x7133EA: Up 10 Right 20 Down 40 Left 80). Face buttons ignored.
- D-pad binds: main-loop hook, rising edge, single direction; gated by the game's own Start checks in front of StartCheck:
  [ABB878]==0, [9BA8D0]!=0, !1515C0(), [156DC0()+0x108]==0, !19BA60(), special slot (lookup 3AD540) -> only in battle w/o cutscene/World Map, !3A0A30().
- Save-menu mode: SetSaveMode exe+2E4940 writes BEDE90; readers 2E9FE4/2EB4C7/2EB641 grey out item 1 (World Map) when mode == 2 (msg 8A88).
  Only writer: exe+31D508 (cmp [744C10],1 / mov ecx,2). ForceWorldTravel: imm exe+31D504 2 -> 3 at load; save keybind sets a leftover 2 to 3.
- Button toggles: count = shown buttons, unused pairs zeroed; Continue always.
- Retry layouts removed from code (crash). Our Start keybinds take the normal path in battle even with InMissions = 0.
## Native module v1.7 (GameRetryPause)
- v1.6 in game: settings file written, everything fine for hours of play. Olympus Coliseum (cup fights): no Retry -
  the game arms slot 3 with its vanilla retry layout (Continue / Retry / Prepare & Retry / ... per KSX's D-pad Left tables);
  InMissions sent Start to the party menu and R2 opened our pause in free slot 0, so Retry was unreachable.
- v1.7: g_last_obj = the slot the lookup picked (special hook / D-pad gate). Pause keybind in battle, not cutscene, slot offers
  fn 3 or 0x0C -> g_special_open(that slot) untouched (= vanilla special path). Setting [CustomPause] GameRetryPause (default 1).
  Otherwise, in battle, the log lists the game's slot functions/texts for diagnosis.
- v1.7 in game (Olympus Coliseum): "the game's pause offers Retry here, opening it (slot exe+2A11560)" - Retry pause shown. User: flawless, responsiveness fixed.
## Native module v1.8 (post-Continue freeze)
- Freeze: custom pause after Game Over -> Continue (Sephiroth fight, Hollow Bastion). Diag builds 1.7.1-1.7.3:
  pause bookkeeping identical good vs bad (screen, BEF508-B, 9A8738, 749830, BEF4A0, BEF488-4B8, BEF510-550);
  minimal pause (Continue only, no KSX tables, no +0x16) still froze; slot left armed still froze;
  vanilla battle pause (InBattle=0, InMissions=0) after Continue did NOT freeze.
- Cause: the fight's mission pause used slot 3 and left fields behind when it ended (active byte 0):
  +0x16 = 000F, +0x18 = 804F8D02, +0x24 = 0003, +0x26 = 0002 (slot 1 for comparison: +0x16 0F, +0x24 01, +0x26 02).
  We only wrote +0..+0x13 and +0x16, so the pause read the stale +0x18/+0x24 fields and hung.
- 2E4C20(slot, arg): if slot+0x25 bit0 -> [156DC0()+0x108] = 0; 31D860 sets BEF4A0/BEF4A8, ABB878 |= 1; 2EB810(2 or -1); jmp 2E4950(8,-1).
- v1.8: the custom pause writes the whole 64-byte slot (zeros except our fields), compare-and-restore per field. Debug switches removed;
  pause-state/slot dump only with Verbose.
- v1.8 in game: Sephiroth Game Over -> Continue -> R2 + Start: slot 3 had the leftovers, pause opened and closed normally. Fixed.
