# KH2-Magic-Menu

Shro's Magic Menu (v1.9.1): KSX's menu Lua scripts reworked as a native .DLL, with additional features.

KH2 Final Mix, PC (Steam Global, Nov 2026 build). Loaded by OpenKH Panacea. Idea and original
scripts by KSX.

## Features (defaults)

| Input | Result |
|---|---|
| **R1 + Start** | Save point menu, instantly (not during battles or on the World Map; see `SaveInBattle`) |
| **R2 + Start** | **Custom pause**: Save / Load Game / Jiminy's Journal / Continue, anywhere but the World Map (field, battles, missions); no Save button during battles unless `SaveInBattle = 1` |
| **L1 + Start** | **Load Game** screen straight away (backing out shows the custom pause; Continue resumes) |
| **L2 + Start** | **Remote shop**: the last moogle you talked to, from anywhere outside battle (synthesis included) |
| **Start in any battle** | Party menu, missions included |
| Start outside battle | Unchanged: cutscene skip menus and minigames work as normal |

The party menu is never forced during cutscenes or on the World Map. World Map appears in the save
menu wherever the game allows travel. Jiminy's Journal is only offered once the story has unlocked
it, and the remote shop only opens shops your save has unlocked.

## Settings: ShroMagicMenu.ini

On first run the module writes `ShroMagicMenu.ini` next to the game exe (beside
`ShroMagicMenu.log`), where a Mods Manager rebuild never overwrites it. Edit it and restart the
game; delete it to get the defaults back. Problems in it are reported in the log, and the
affected setting keeps its default.

| Section / key | Default | Meaning |
|---|---|---|
| `[SaveMenu] Enabled` | 1 | Save menu keybind on/off |
| `[SaveMenu] Keybind` | `R1+Start` | see Keybinds |
| `[SaveMenu] SaveInBattle` | 0 | Allow saving during a battle (this keybind and the custom pause's Save button). **A save made mid-battle, above all in a boss fight or mission, may load into a broken state or softlock the game**: keep a separate save from before the fight. |
| `[CustomPause] Enabled` | 1 | Custom pause on/off |
| `[CustomPause] Keybind` | `R2+Start` | see Keybinds |
| `[CustomPause] ShowSave` / `ShowLoadGame` / `ShowJournal` | 1 / 1 / 1 | Which buttons the pause shows (Continue always) |
| `[CustomPause] GameRetryPause` | 1 | In fights the game can restart (Coliseum cups, minigames, missions whose own pause offers Retry / Prepare & Retry), the pause keybind opens the game's own pause, untouched, so Retry works. Plain Start still gives the party menu. |
| `[CustomPause] ForceWorldTravel` | 0 | Offer World Map in the save menu even where the story has travel locked. **Leaving a world mid-story or mid-mission can softlock the game**: keep a separate save from before you travel. |
| `[LoadMenu] Enabled` / `Keybind` | 1 / `L1+Start` | The pause, opened on its Load Game screen |
| `[RemoteShop] Enabled` / `Keybind` | 1 / `L2+Start` | Remote shop on/off, keybind |
| `[RemoteShop] ShopId` | `Last` | `Last` = the last moogle you talked to (kept in `ShroMagicMenu-lastshop.txt`; the Hollow Bastion moogle, 0x13, until you have talked to one), or a shop id such as `0x13`. The log lists every shop id the first time the keybind is used |
| `[RemoteShop] VisitedOnly` | 1 | Only shops whose unlock flag is set in the save (you have met that shopkeeper) |
| `[RemoteShop] StandInSize` | 40 | Sora's size in the shop window, in percent (10 to 100); he stands in for the shopkeeper and is much bigger than a moogle. Set through the shop view's own scale (exe+2FECD0 hook), only while the remote shop is open. |
| `[RemoteShop] StandInLift` | 120 | How far Sora is raised in the shop window, in the menu's screen units (about 448 high; -300 to 300), so he sits centred instead of with his feet cut off. Moves the shop camera's screen centre (the view setup's own arguments, exe+2FEB20 hook). |
| `[PartyMenu] InBattle` | 1 | Start in battle opens the party menu instead of the battle pause |
| `[PartyMenu] InMissions` | 1 | The same in mission battles that have their own pause (outside cutscenes) |
| `[Log] Enabled` | 1 | Write ShroMagicMenu.log |
| `[Log] Verbose` | 0 | Also log every Start press in battle, battle / cutscene changes and the pause slot's bytes when the custom pause opens |

**Keybinds** are buttons joined with `+`: hold the shoulder buttons (`L1 R1 L2 R2`), then press the
last one, `Start` or a D-pad direction (`Up Down Left Right`). Examples: `R1+Start`,
`L1+R1+Start`, `L2+R2+Up`. `None` switches a keybind off. Every keybind needs at least one
shoulder button (plain Start stays the game's own; the D-pad alone drives the command menu), and
exactly those shoulder buttons must be held. If two features share a keybind, the later one in the
file is switched off. Face buttons are not accepted (they would clash with gameplay).

D-pad keybinds go through the same five checks the game runs before it accepts Start (menus,
events and the like), so they only fire where Start would pause. Our keybinds work in missions
even with `InMissions = 0`.

## How the custom pause works

Every pause in KH2 is fed by a **pause slot**: five 0x40-byte slots at exe+2A114A0, each holding
a pause's options. Slot 1 is the battle pause (the byte this module clears for the party menu),
slot 3 is the one KSX's "Save, Load, World Map Anywhere" fills, and slot 4 is cutscenes. When
Start is pressed, the game picks the active slot and hands it to its special-pause routine
(exe+2E4C20), which opens the pause.

The keybind fills slot 3 with the chosen buttons (plus KSX's other two tables and "loadingheart"),
calls the special-pause routine with it. The module owns the whole 64-byte slot while the pause is
up: everything it does not set is zeroed, because a mission pause that used the slot leaves its own
fields behind when it ends, and the pause hangs on them (found after a Game Over -> Continue at the
Sephiroth fight). It and switches the slot's active byte straight back off
(the pause keeps its own pointer; only the Start lookup reads that byte), so the slot never stays
armed. Every value is put back once the pause has closed, restoring only values that are still
ours. The main-loop hook does not run while the pause is up, so the close is seen when the
pause's screen word is -1 for 120 gameplay frames, or straight away on the next keybind / Start
press (no lockout). Some missions use slot 3 for their own pause; then the custom pause goes into a
completely unused slot (2, then 0), so the mission's slot is never touched.

A slot holds four buttons (a fifth would overwrite the word at +0x16). The game's Retry (3) and
Prepare & Retry (0x0C) only work in fights the game has set up for them (Game Over Continue, 4,
crashed from our pause in an ordinary battle), so the custom pause never adds them. Instead, when
the game's own armed slot offers them (its vanilla layout is Continue / Retry / Prepare & Retry /
...), the pause keybind opens that slot as it is. Otherwise the log lists what the game's slot
offered. Re:Fined's Retry options on the Game Over screen are
untouched.

**ForceWorldTravel** is KSX's "savemenu" patch: at exe+31D4F9 the pause sets the save menu's mode to
2 (`mov ecx,2`), and the save menu greys out World Map in mode 2. The module changes that 2 to 3
once at load, and for the save menu keybind clears a leftover mode 2 before opening.

## How the Load keybind and the remote shop work

**Load**: the keybind opens the custom pause, and a hook in the pause's screen task (exe+31D499),
after it has built the button page, makes its first screen 3, the load screen (the one the Load
Game button asks for), instead of 9, the button page. Backing out of Load returns to the button
page as usual. (1.9 switched the screen before the button page was built; backing out of Load
could then crash on stale objects, seen after a shop had been open. Fixed in 1.9.1.)

**Remote shop**: the keybind calls the game's own OpenShop(id) (exe+1518E0), the function its event
code calls when you talk to a shopkeeper. Forcing a Shop reaction command onto Triangle does not
work: a reaction command runs on the object it belongs to (the moogle), so without one nearby the
game crashes. The shop screen shows its shopkeeper as a 3D model and can only use a model that is
already loaded where you stand, so the module puts the game's "current player" object (Sora,
always loaded) in the shopkeeper's place until the shop closes. (1.9 checked the game's model
cache and kept the shopkeeper when its model was listed; a model still listed one room on crashed
the shop in 1.9.1 testing, so Sora now always stands in.) A hook at OpenShop's entry remembers the moogle shops you open by talking to a moogle.

The story flags (Save+2390, exe+9ABC40) decide the rest: each shop's unlock flag for VisitedOnly,
and flag 0x73 (Save+239E) for the Journal button.

## How it works

- At load it logs the exe's build stamp and finds twelve pieces of game code by byte signature,
  with the addresses decoded from inside them. On the build these notes are from (stamp
  `669E384A`), each signature is checked where it is known to be; on any other build the whole
  game code is scanned.
- **Data guard**: some game data can't be found from code (controller, world, battle state,
  cutscene flags, KSX's tables) and is listed at its known address. Every data address the code
  does name is compared with the notes: if all match, fine; if all moved by one amount (as a game
  update does when it shifts its data), the listed ones are moved by that amount too and the log
  says so; if they moved by different amounts, nothing is patched.
- It installs three 5-byte hooks in the game's Start-button and main-loop code, at sites with no
  rip-relative operands:
  - **exe+157304** (`test rax,rax / jne`): where the game decides between a special pause and the
    normal one. In battle the module takes the normal path (party menu, or our keybinds), except
    during cutscenes, on the World Map or while KSX's Lua anywhere pause is armed.
  - **exe+157312** (`mov edx,-1`): Start keybinds.
  - **exe+15732A**: once per main-loop pass: clears the battle pause slot's active byte, D-pad
    keybinds, custom pause tidy-up, and (verbose) battle type and cutscene changes.
  - Optional: **exe+31D499** (the pause's screen task, after the button page is built: Load
    keybind, and the custom pause's tidy-up) and **exe+1518E0** (OpenShop's entry, last moogle).
- Faults are logged as `FAULT` with where (exe offset, registers, call chain). The game catches
  some faults itself, so a `FAULT` line only means a crash if the game closed right after it.
- Every code address is read out of the game's own code at load, with cross-checks. Hooks are
  written once in DllMain, all or nothing. Optional parts (custom pause, D-pad keybinds,
  ForceWorldTravel) switch off on their own if their code looks different. It refuses to run on
  anything but the Steam Global build.

## Build and install

1. Run `build_msvc64.bat`, check for `8664 machine (x64)`.
2. Copy `mod\ShroMagicMenu.dll` into your mod folder under `openkh\mods\kh2\`, rebuild in Mods
   Manager. The log's first line should say `Shro's Magic Menu 1.9.1`, followed by the settings line,
   the build stamp, `Data guard: all 11 data addresses ... match our notes`, five `PATCHED:` lines and a `Ready:` summary of the active keybinds.
3. Remove the old Lua script, if any; this module replaces it.

## Test (from a save)

1. First run: `ShroMagicMenu.ini` appears next to the game exe; defaults behave as in 1.5.
2. Change a keybind to a D-pad one (e.g. `CustomPause Keybind = L2+R2+Down`), restart: it opens
   the pause in the field and in battle, and does nothing while a menu or event is up.
3. Turn a pause button off (`ShowJournal = 0`), restart: the pause shows three buttons.
4. `[PartyMenu] InBattle = 0`: Start in battle gives the battle pause again.
5. Coliseum cup (or any fight whose pause had Retry): R2 + Start shows the game's pause with Retry;
   the log says "the game's pause offers Retry here". Plain Start gives the party menu.
. L1 + Start in the field and in battle: the load screen; backing out shows the pause buttons.
8. Talk to a moogle, then L2 + Start elsewhere: that moogle's shop, with Sora shown in the
   moogle's place. The log says "remembered for the shop keybind". In battle it does nothing.

## Tests (for development)

- `tests/harness5.c`: maps the **real game exe** into a fake process image (`./h5 "KINGDOM HEARTS II
  FINAL MIX.exe"`), scans the full game code, installs the hooks and runs: the keybinds, the
  custom pause (fill, disarm, restore, free slot in missions, no lockout), the settings file
  (default file written and parsed back, keybind parser, bad values, same keybind twice), button
  toggles, custom Start keybinds, the game's Retry pause (Start and D-pad), D-pad keybinds behind the game's Start checks, ForceWorldTravel
  and the party menu toggles. Mutation 1 changes the Load case and checks only the custom pause
  is disabled.
- `tests/harness_win.c` (Windows, MSVC): the same idea for 1.9 onwards: real signature scan (fast
  path and full scan), data guard (even and uneven moves), Load menu, remote shop, story flags,
  SaveInBattle, settings files with BOM / UTF-16, crash log.
- `tests/harness3.c` and `tests/check_signatures.py`: older tests against bytes taken from Cheat Engine
  dumps (`tests/dump_bytes.h`). That file holds game code, so it is not in the repo; generate it from
  your own dumps to run these.

The game exe, disassembly and dumps are never committed (see `.gitignore`). `harness5` reads the exe
from your own install.

## Credits and licence

- **KSX** came up with the idea and wrote the original Lua scripts ("Open Menu in Battle" and
  "Save, Load, World Map Anywhere"). This module contains none of his code. It does use facts taken
  from his scripts: the pause option table layout, the button and text IDs, and the World Map patch
  byte. KSX's scripts are his own work and are not included or licensed here. Ask him for permission
  before reusing them.
- The battle type and world addresses come from the GoA ROM randomizer script.
- Everything else (the signatures, hooks, custom pause, settings and tests) was written by Shro and
  Claude, from our own disassembly and in-game testing.

The code in this repository is licensed under the **GNU General Public License v3.0** (see
`LICENSE`). Kingdom Hearts is owned by Square Enix and Disney. No game files are included.
