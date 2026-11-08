# Native Reconstruction Status

Two native targets are built from the recovered C++ sources:

- **Console probe** (`build-native.ps1`): loads the original asset, sprite, and
  map data and drives the recovered state machine without presentation.
- **Win32 presentation target** (`build-win32.ps1`):
  `build\bgw_native.exe`, the reconstruction used for pixel-level
  verification. It renders the complete title/menu flow, option page, ranking
  page, gameplay field and HUD, miss/game-over overlay, name entry, rank
  display, and the state-10 clear/scoreboard sequence.

Build from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File recreated\build-native.ps1
powershell -ExecutionPolicy Bypass -File recreated\build-win32.ps1
```

The console probe validates:

- Surface, sprite, and stage metadata loading.
- The binary map header and 16-bit tile payload loading.
- Startup transition from state 0 to state 1.
- Static linking of the MinGW runtime so the executable runs without adjacent
  MinGW DLLs.

It does not present video or audio; those paths live in the Win32 target.

The Win32 target embeds the original `bgw.exe` application icon
(`RT_GROUP_ICON` "MAINICON", one 32x32 4bpp image) from
`recreated\src\bgw.rc` / `recreated\src\bgw.ico` and installs it as both the
large and small window icon.

`build-win32.ps1` also compiles with `-Os -s -ffunction-sections
-fdata-sections -Wl,--gc-sections`, producing a **~1.05 MB** stripped
executable. The earlier 3.35 MB build was unstripped: MinGW left a ~2.09 MB
COFF symbol/string table after the last section, plus ~25 KB of DWARF sections.
The remaining size is the statically linked C++ runtime and x64 unwind tables
(`.pdata`/`.xdata`); the shipped 421 KB executable is 32-bit Delphi with a much
smaller RTL and no C++ static runtime.

## Current state and verification (2026-10-25)

The Win32 target is not a partial shell anymore: every screen that is reachable
in the shipped game can be compared against it pixel for pixel.

Comparison method: the shipped game runs in a window and a DPI-unaware
PowerShell process captures the client DC with `BitBlt`. At the test desktop's
150% scaling this yields a 640x480 image that is an exact 2x copy of the
320x240 game frame; comparisons use the 2x-downsampled image (or the native raw
back buffer from `--shot=<dir>`). A pixel counts as different when
`|dR| + |dG| + |dB| > 20`.

Verified against the shipped game with **0 differing pixels**:

- title/menu page, including the selection arrows;
- option page with `game.ini` = EASY (level 1), `BUTTON 0`, wait OFF, vsync OFF;
- menu ranking page;
- gameplay field and HUD (both map layers, player, effects,
  POWER/TIME/HIGH/SCORE);
- the timer-expiry miss and GAME OVER progression (states 4 and 5);
- state 6 name entry and state 7 rank display;
- state 10 clear/scoreboard: initial hold, 100-per-frame bonus count-up, and the
  post-count hold at the corresponding original frames.

The player idle animation is the only moving element that can make a still
comparison differ: `FUN_00453B64` toggles between two 40x40 sprite cells every
9 frames while grounded. The native toggles at the same cadence. The two phases
differ by 1164 px inside that 40x40 cell, and native state-5 captures match
original frame captures in both phases exactly (captures `reg_s5t0b-*` versus
`fast_o2-20` / `fast_o2-21`). State 4 behaves the same way (`reg_s4t0-2` versus
`fast_o2-12`, 0 px). The state-4/5 ground truth comes from the scratch
fast-timer binary, which only changes the timer step from `0x78` frames to 1.
No rendering difference remains in the compared states. Player-hidden behaviour
after a fall miss is handled by deactivating the player render link, as
`FUN_00454214` does in the shipped code.

`original\bgw.exe` is the working copy carrying the four Win11 fixups
documented in `NOTES.txt`; `original\bgw-pristine.exe` is the pristine 2001
binary. Menu, option, and ranking frames were re-verified against the pristine
binary as well (0 px), so the fixups do not affect rendering.

High-score persistence matches the shipped binary: `insertRankScore` and
`writeRankNameByte` only update the in-memory table, and `writeRankFile` runs
after the main loop exits. Only a graceful `WM_CLOSE` rewrites
`data\rank.dat` (verified byte-identical on a clean shutdown). The capture
harness terminates the process, so test runs never save. Both
`original\data\rank.dat` and `build\data\rank.dat` hold the pristine
`34300 "KRI"` + nine zero records (MD5 `5a1d1c1d0b05dc69a1c4016487ae37d2`).
`FormDestroy` also rewrites `game.ini`; the native's `writeIniSettings` now
mirrors that (see "Difficulty selection and config persistence").

The 2026-11-08 fixes (per-new-game difficulty remap, the 60 fps gameplay lock,
and the `FormDestroy` save) are documented below.

State 8 is unreachable in the shipped code. The native paints its
`CONTINUE`/countdown overlay over the retained last field frame, matching
`FUN_00454535` (that state does not clear the back buffer).

Scratch binaries used for ground-truth captures were removed after use. They
were byte-identical to the working `bgw.exe` except for one byte each:

- fast timer: file `0x5486F`, `0x78` -> `0x01` (`--fast-time` mirrors this);
- state 10 on miss: file `0x53C29`, `0x03` -> `0x0A`.

Test hooks in the Win32 target:

| Flag | Effect |
| --- | --- |
| `--shot=<dir>` | Write the raw 320x240 back buffer as 24-bit BMP every 30 frames. |
| `--shot-every=N` | Override the `--shot` interval for frame-exact comparisons. |
| `--state=N` | After `startNewGame`, force state N (2-7 or 10). |
| `--rank=<index>,<len>` | Force the rank row and name length. |
| `--time=N` | Set the remaining play time. |
| `--spawnx=N`, `--spawny=N` | Place the player at an exact world position after `startNewGame` (fixture tests). |
| `--fast-time` | Mirror the scratch fast-timer patch. |
| `--log` | Write `bgw_debug.log`; each frame line also reports live `chunks=`, `popups=`, and `puffs=` counts, and each sound request is logged as `sound <path>`. |

## Gameplay behavior fixes (2026-11-07)

Three defects seen during real play were traced to the reconstructed gameplay
path, not to the presentation layer. All three are now fixed and verified with
frame captures and `--log` traces.

### 1. Performance degraded as the player climbed

Root cause: the reconstructed type-1 player path ran the generic collision /
effect-spawn helpers every frame. In the shipped binary `FUN_00453E50` only
runs `FUN_004535C8`/`FUN_004537DC` for slots above `0x20`, and the type-1
routine `FUN_00453B64` does not spawn effects by itself. The reconstruction also
kept type-3 score popups and type-4 jump puffs active forever, so each
collection/collapse leaked a live slot and replayed `break.wav` on every frame
the player touched a tile. As the run got longer the pool filled and the
per-frame pool walk and draw became visible as a slowdown.

Fix: `updateEntity` type 1 no longer calls `collides`/`spawnEffect`/
`enterMissState`; type 3 now initialises `+0x5c = 0x3c` and deactivates through
the recovered `FUN_00454214` when the cooldown reaches zero; type 4 advances
`+0x18` every five animation ticks and deactivates once it exceeds 1. Wall
collapses still allocate a type-2 chunk, but only once per tile transition.

Type-2 collapse chunks are cull-exempt in the shipped type table (`+0x28 = 1`)
and never deactivate, so a long climb can leave many of them in the pool.
`drawFrame` now skips entity blits whose rectangle lies fully outside the
320x240 client area. GDI would clip those blits anyway, so the rendered output
is unchanged (25/25 screenshots of the standing fixture are pixel-identical to
the build before the skip), while the per-frame blit cost stays bounded.

Evidence (`--log`, counts parsed from the frame lines):

| Run | Result |
| --- | --- |
| Player standing on a timed wall for 36 contact frames | exactly 4 chunks and 4 `break.wav` requests (one per 4→5→6→7→clear transition), 0 popups, 0 puffs; 62.6 frame lines/s |
| Four collectables picked up at spawn | 4 popups, then 0 popups after the 60-frame cooldown; 4 `get.wav` requests |
| 20 s live run, right held across the row-254 timed walls | 1257 frame lines (~63/s, no decay), max 8 chunks / 1 popup / 0 puffs, 8 `break.wav` total; the counts stay flat for the remaining ~1100 frames |

### 2. Timed-wall animation did not match the original

Root cause: the reconstruction advanced tiles per column and reset the contact
counter while walking between columns. `FUN_00452774` actually keeps one shared
counter at `entity+0x40`:

- it increments **once per frame** when any sampled column under the player is
  solid (not once per column), and it is not reset when the player walks onto a
  different column;
- it is reset only when a tile actually transitions;
- family `4..7` transitions when the counter is `> 8`, family `8..11` when
  `> 4`, and family `12..15` when `> 2`;
- columns are walked left to right, so the leftmost transitionable solid column
  changes first, and at most one tile transitions per frame;
- the chains are `4→5→6→7→clear`, `8→9→10→11→clear`, `12→13→14→15→clear`;
- each transition spawns a type-2 chunk at `(col*32+16, playerY+32)` with
  `animFrame = value - 4` and plays `break.wav`.

`advanceAnimatedTile` now implements exactly that ordering and counter
lifetime. Evidence: native capture `shots_state2b` shows the tile under the
player changing at frames 8/17/26/35 (9-frame intervals), then clearing;
the combined fixture trace shows chunks 1→2→3→4 on the first column and
5→6→7→8 on the second column, each group 9 frames apart, with the counter
surviving the column change. The original capture `caps_gt3` shows the same
blue→gray→clear progression and the same standing/fall timing.

### 3. Collectables had an opaque background and no score popup

Root cause: map tiles were blitted opaquely (so the item's black key was drawn
as a black square), and the item path did not spawn the recovered score popup.

Fix: `drawMap` uses `TransparentBlt` with `RGB(0,0,0)` as the color key, which
is correct for every tile in `bg001.bmp`. Collection spawns the recovered type-3
popup at the tile centre `(col*32+16, row*32+16)` with `animFrame = scoreIndex`,
awards `100/500/1000` through the 16-frame streak timer, plays `get.wav`
(`FUN_004523b0` index 2), and lets the popup blink on odd cooldown frames and
deactivate after 60 frames.

Evidence:

- Native item tiles match the original capture `caps_gt3` at **0 differing
  pixels** (both item tiles: exact black 44/2 px, purple 164 px, best alignment
  shift 0,0).
- `chr004` frames are `100` white (83 white px), `500` white (85 white px), and
  `1000` pink (111 pink px); native popups reproduce those exact counts.
- The original popup capture `caps_gt_popup` shows the row-253 popup reaching 83
  white (`100`) and the row-254 popup 85 white (`500`), with the HUD reading
  `600`; after the streak timer lapses a third collection scores `100` and the
  HUD reads `700`, confirming the recovered 100/500/1000 sequence.
- Native `shots_popup_long` shows the popups blinking and disappearing after
  frame 60.

The fixture map used for these runs is the row-254 timed-wall layout restored
from `verify\map\000.map.walltest`; the temporary item/score fixture was only
installed in the scratch `verify` copies, never in `map\000.map`.

## Difficulty selection and config persistence (2026-11-08)

Reported defect: NORMAL and HARD played exactly like EASY.

Root cause: the native applied the `FUN_00456BB4` level remap once at startup
from `game.ini` `[env] gamelv`, then `startNewGame` copied that already-remapped
snapshot. Changing GAME LEVEL in the option page updated the state machine's
difficulty, but the map was never remapped again, so every run used the startup
map. The shipped game reloads the map in `FUN_004562F4` and remaps timed walls
in `FUN_00456D92` on each game start.

Fix: `App::initialTiles` now keeps the pristine post-load map, and
`startNewGame` resets `catalog.map.tiles` from it and calls
`applyLevelRemap(stateMachine.difficulty())` every time. The startup remap call
was removed.

Verified through the real option page with injected scancodes (select EASY /
NORMAL / HARD, then START) by counting the three tile colours in the 320x240
frame:

| Selection | blue `(0,199,255)` | green `(0,254,136)` | red `(254,43,0)` |
| --- | --- | --- | --- |
| EASY | 2625 | 0 | 72 |
| NORMAL | 2330 | 295 | 72 |
| HARD | 0 | 0 | 2697 |

EASY and HARD match the original captures exactly; NORMAL differs from EASY as
the level-2 map does.

Persistence: the shipped `FormDestroy` (VA `0x456F7C`) writes `[button]
shot/dash`, `[env] softvsync/waitvsync/debugflg/gamelv`, and `data\rank.dat`
only on a graceful close. The native now has the same `writeIniSettings`
shutdown step: it updates those keys in place, appends `shot`/`dash` after the
last `[button]` key, preserves unknown sections, and writes the live option-page
`gamelv`, so the selected difficulty survives a restart. Verified against the
original: a probe file with `gamelv=2`, no `shot/dash`, and an `[extra]` section
round-trips identically; selecting HARD and closing writes `gamelv=3`; killing
the process leaves the file untouched.

## Implementation log

The Win32 target started as a native windowed asset/map shell with title
rendering, keyboard movement, map drawing, and player animation, and grew from
there. It exercises the recovered tile-2 score sequence, tile-3 completion,
player fall threshold, and jump-energy HUD. HUD/menu text uses the recovered
bitmap font and jump, break, and clear/death paths use shipped WAV assets. The
recovered `game_state.cpp` and `entity_behavior.cpp` modules are compiled into
the target as validated code. A Win32 `RuntimeApi` adapter drives the recovered
menu/start and escape-to-menu state boundary. Shell death and tile-3 completion
enter the recovered miss and clear states. The player/map loop remains
responsible for gameplay, but the recovered player `EntityRecord` and
`updateEntity` path run each gameplay frame, with Win32 callbacks for miss,
clear, and effects. WinMM MCI opens, plays, stops, and reports completion for
the recovered MIDI tracks used by those state transitions.
A miss (fall or timer expiry) enters state 4 and then the GAME OVER state 5;
there is no lives counter, matching the shipped binary, and the score is
recorded in the in-memory ten-entry descending table.
Collision callbacks use the reconstructed `FUN_004525A4` rectangle overlap
rather than a constant. The recovered solid-tile transition chains are applied
on sustained contact, matching `FUN_00452774`: family `4..7` advances while the
contact counter is greater than 8, family `8..11` while it is greater than 4,
and family `12..15` while it is greater than 2, each clearing the terminal tile.
Tile-2 destruction also allocates and updates a bounded type-2 effect entity
through the recovered entity dispatcher.
High scores load from and save to `data\rank.dat` (ten descending records), and
the menu ranking page presents them.
Goal detection now scans the complete player footprint, matching the recovered
tile-overlap rule for the known goal tile (tile 3, near map column 10, row 6).
The recovered score is now passed into the rank state, with native uppercase
name editing, backspace handling, and a rank prompt rendered after game-over.
New attempts restore the original map snapshot, clear spawned effects, reset
tile animation counters, and reset the camera. Input now recognizes the
recovered `VK_CONTROL` jump path in addition to the space shortcut.
Entity coordinates now use the recovered 5-bit fixed-point scale (`>> 5`),
including player spawn coordinates and falling effect entities.
Rank names are stored in the binary score table itself (four space-padded bytes
per record) and remain paired during score insertion. The map loader now applies
the recovered `FUN_00456BB4` level remap driven by `game.ini` `[env] gamelv`: level
1 rewrites tiles 8 and 12 to 4, and level 3 rewrites tiles 4 and 8 to 12. The
map snapshot used for retries is taken after this remap. The breakable-tile
score sequence now matches `FUN_00453224`: breaking tiles sets a 16-frame
streak timer, the sequence advances `100 -> 500 -> 1000` while it is active,
and it resets to `100` once the timer lapses. Player jumping now follows the
`FUN_00453B64` energy model: jump energy starts at 0, each jump grants 50 points
and adds 10 energy up to a cap of 30, successive jumps are weaker, and energy
resets on landing or respawn. The state-8 countdown from `FUN_00454535` is
rendered (`9 - frameCounter / 0x78`) with the recovered button fast-forward,
resume-gameplay, and expiry transitions; the shipped code never selects state 8,
so this screen is unreachable in normal play. The recovered player spawn at fixed
point `(0x1000, 0x1640)` and the `FUN_00454084` type-1 player creation path are
documented against the native spawn coordinates. The `EntityRecord` layout now
matches the binary offsets exactly (position `+0x60/+0x64`, contact counter
`+0x40`, jump energy `+0x44`, collision count `+0x4c`, sprite index `+0x18`),
and a recovered `EntityPool` implements the `FUN_00454084` slot allocator with
the group ranges `[0,0]`, `[1,0x20]`, and `[0x21,0x220]`. Break effects are now
allocated, updated, and drawn through that pool instead of a fixed array. The
pool loop now matches `FUN_00453E50`: it ticks each live object's cooldown
`+0x5c` every frame, culls objects outside the active vertical band via the
`FUN_004539D4` rule, and blinks objects on odd cooldown frames. Broken tile-2
objects now allocate the recovered type-3 slot with the score index written to
`+0x18`, and collapsed animated solids allocate a falling type-2 slot. The
`FUN_004539D4` cull now uses the recovered half-size bounds (centre inside
`[-4*half, 4*half + screen]`) and honours the type-table `+0x28` exemption, and
`FUN_004525A4` is reconstructed as an axis-aligned rectangle overlap that the
pairwise collision callback now uses instead of returning a constant. State 10
runs the `FUN_00454DB8` three-phase scoreboard: a `0x78`-frame hold, a
100-points-per-frame tally that distributes the total
`breakableCount * 100 * difficulty + timeRemaining * 100`, then a `0x4b0`-frame
hold before the `FUN_0045720C` score insertion chooses the rank or title state.
State 9 loops `midi\clear` on entry and waits for it to finish before the
scoreboard. The recovered play timer from `FUN_004562F4` /
`FUN_00455228` now runs during gameplay: it starts at 99, drops one unit per
`0x78` frames, is drawn near the energy meter, and enters the miss state on
expiry; the surviving units feed the state-10 time bonus. The `FUN_00453B64`
player routine is now matched in full: the vertical-velocity cap of `0xc0`
fixed (6 px/frame),
the `jumpEnergy < 0x15` jump scaling, and the ground-jump type-4 puff are all
present, and the routine short-circuits while state 9 is active. The
`FUN_00452E8C`/`FUN_00452CC0` horizontal solid-tile resolution is now applied to
the player's leading edge, the `FUN_00453224` goal tile is cleared on contact,
and breaking a tile now bleeds 4 jump-energy off the player as recovered.

Map orientation is now recovered correctly: the `.map` header int32s are
`[13, 256, 32, 32, 4, 4]`, so the level is a **13-column by 256-row vertical
shaft** (row-major stride 13), not a horizontal strip. `FUN_00450E60` indexes
`buffer[x + y * 0x6024]`, wrapping `x` at `0x6024` (width 13) and `y` at `0x6020`
(height 256), and `FUN_00456BB4` stores `buffer[i]` at `(i % 13, i / 13)`. The
tile atlas is `bg001.bmp` (128x128, 4x4 cells of 32x32), addressed row-major as
`(col = v % 4, row = v / 4)`; `chr002.BMP` is not the map atlas. Level 1
(`map\000.map`) has solid wall rows at both ends (rows 0-1 and 254-255),
breakable dirt (tile 2) and the goal (tile 3, near column 10, row 6) toward the
top, and an open shaft down the middle; the player spawns on the floor at the
bottom of the shaft (row 254) and tunnels upward to the goal. The Win32 `drawMap` was rewritten to render
every non-zero tile from the `bg001.bmp` atlas with a vertical camera
(`cameraY` clamped to `rows * 32 - 240`), and `solidTile` remains
`v == 1 || (v >= 4 && v <= 15)` so tile-2 dirt is not solid.

The Win32 target now renders the recovered title/menu flow. `game_state.cpp`
gains a `MenuAction` enum and menu movement/navigation (`menuMove`,
`menuActivate`, `menuBack`) over a `menuPage_`/`menuIndex_`/`optionIndex_`
model. `windows_main.cpp` adds `drawMenu` (page 0: `title.bmp` background with
`GAME START`/`OPTION`/`EXIT`, selection arrows, and the `CREATED BY E.HASHIMOTO`
line; page 1: `bg002.bmp` options background with `- OPTION -`, game level, jump
button assign, wait, software vsync, ranking, and exit rows; page 2: the
`BUTTON TO BACK` prompt) and `drawTextCentered`. Menu keys route through
`WM_KEYDOWN` while not playing (Up/Down move, Return/Space activate, Escape
back), and `startNewGame` starts a run from `GAME START`. `drawHud` was rewritten
to the recovered `FUN_00455228` cell layout (`HIGH`/`SCORE`/value rows, `POWER`
label, the 30-segment energy meter and frame, and `TIME`/value). Font band
selection matches `FUN_004524AC`: band 0 is white numbers, band 1 orange labels
and arrows, band 2 yellow titles; the glyph background is black, so the black
color-key transparent blit is correct. The `FUN_00455228` backdrop call
`FUN_004523dc(256, 0, 320, surface, 0, 240)` is now recovered: it is not a
surface blit but a DirectDraw `Blt` with `DDBLT_WAIT | DDBLT_COLORFILL`
(flags `0x1000400`) over rect `(256,0)-(320,240)`, filling with
`DDBLTFX.dwFillColor = 0`, i.e. a solid black right-hand column. `drawHud` now
fills that rect black before blitting the HUD text, and the boundary was
verified against a native capture (map pixels to `x = 255`, pure black from
`x = 256`).

Entity system re-verified directly from the shipped disassembly. Entity records
are `0x88` bytes with `0x200` slots; the base pointer lives at `ds:0x45a068`
(value `0x45B958`, in BSS), slot 0 is the player. The spawner `FUN_00454084(al
= group)` uses group 0 -> slots `[0,0]`, group 1 -> `[1,0x20]`, group 2 ->
`[0x21,0x200]`; it writes slot index `+0x00`, the caller's kind argument to
`+0x04`, `+0x08 = 1` (active), the type argument to `+0x0c`, clears the eight
dwords at `+0x1c` and `+0x3c`, and initialises `+0x18 = 0`, `+0x10 =
0xffffffff`, `+0x60 = x`, `+0x64 = y`, `+0x68 = arg8`, `+0x6c = 0`, `+0x70 =
arg0xc`, `+0x74 = 1`, `+0x78 = 1`, `+0x7c = 1`, `+0x5c = 0`. When a free slot is
found it then copies the type-table dwords `+0x00 -> entity+0x14`,
`+0x04 -> entity+0x78`, `+0x08 -> entity+0x74`.

The entity-type table is static: pointer at `ds:0x45a498` (value `0x459F4C`),
stride `0x34` (13 dwords). No code references `0x459F4C` directly and
`0x45a498` is only ever read, so the table is never reassigned at runtime. Only
types `0..4` are populated; types `5+` overlap unrelated function-pointer data,
so they must not be treated as type records. Field meanings: `+0x00` animation
offset (-> `+0x14`), `+0x04` collision-enable flag (-> `+0x78`), `+0x08` value
(-> `+0x74`), `+0x0c` collision **effect**, `+0x10`/`+0x14` AABB insets,
`+0x28` cull exemption. For types `0..4` the effect field is `0`.

Collision and cull, confirmed exactly:

- `FUN_004535C8(entity)` (entity-vs-player): requires the player active, the
  entity's `table[type]+0x0c != 0`, and the player cooldown `+0x5c == 0`. It
  builds the player and entity AABBs from the sprite dimensions (`FUN_00450290`
  + `FUN_004501F4`/`FUN_00450240`) and the type insets `+0x10`/`+0x14`, tests
  `FUN_004525A4` (plus a `0` margin), and on overlap runs `effect - 1`:
  `effect == 1` sets the player `+0x5c = 0x3c` and state `= 4` (MISS);
  `effect == 2` sets `+0x5c = 0x3c` and adds `0x3e8` (1000) to the score.
- `FUN_004537DC(entity)` (entity-vs-entity): loops the 32 sibling slots from
  `base+0x90` step `0x88`, builds both AABBs, and calls `FUN_004525A4` — but it
  **discards the return value**, so as shipped this routine is a no-op; it is
  gated by the entity's `+0x78` flag.
- `FUN_004539D4(entity)` (cull): centre `c = ([+0x60]>>5, [+0x64]>>5)`, half
  `w = FUN_004501F4()`, `h = FUN_00450240()`; culls (calls `FUN_00454214` to
  deactivate) when `c.x < -4*w`, `c.x > 4*w + 0x140`, `c.y < -4*h`, or
  `c.y > 4*h + 0xf0`.
- `FUN_004525A4`: pure axis-aligned rectangle overlap over two 4-dword rects
  plus a `0` margin argument; returns `al == 1` on overlap (`ret 0x4`).

Important consequence: every spawn call in the binary creates only types `1..4`
— the player (`FUN_00454084(0, type 1, x=0x1000, ...)` at `0x454632`) and three
effect entities created in group 2: type 2 (break/pickup puff, spawned three
times from the tile-collision path at `0x452a84`/`0x452b82`/`0x452c80`), type 3
(delayed effect at `0x453463`), and type 4 (jump puff at `0x453d32`, plus the
`FUN_00453B64` ground jump). There are **no enemy entities** and no entity
contact damage: the static table's effect field `+0x0c` is `0` for all populated
types, so `FUN_004535C8` is inert, and the per-type routines (1..4) only move,
animate, and despawn. Player death comes solely from falling (`y >> 5 > 0x100`
-> state 4) and from the timer expiring. Entity recovery is therefore complete:
the shipped entity system is a small effect/spawn pool, and the native type-2/
type-3/type-4 effect entities already model it.

## Multi-layer map rendering (level 1)

The stage table (`PTR_DAT_0045a2a4`, runtime BSS value `0x46E9A8`) has records
of stride `0x38` indexed by stage. `FUN_004566a0(obj, stage)` decodes a record:
`+0x00` surface-set 0, `+0x04` surface-set 1, `+0x08/0x0c/0x10` the map numbers
for up to **three layers** (`-1` = unused), `+0x14/0x18/0x1c` a per-layer atlas
bitmap index, and `+0x2c/0x30/0x34` a per-layer flag (`0` = scrolled, `1` =
fixed). The records are parsed from the ASCII `data/stage.dat`:

    level 0: surf=0 spr=0 maps=[-1,-1,-1] atlas=[-1,-1,-1] ...
    level 1: surf=1 spr=1 maps=[ 0, 1,-1] atlas=[ 3, 7,-1] ...

`FUN_00456bb4(obj, mapNum, layerIdx, atlasIdx)` frees the old map object,
loads `map\%03d.map`, reads the 6-int header `[cols, rows, tileW, tileH,
atlasCols, atlasRows]`, stores `tileW/tileH/cols/rows` into the scroll state
(`PTR_DAT_0045a374 + layerIdx*0x20`), builds the atlas cells via
`FUN_00450de4` from surface `PTR_DAT_0045a480[atlasIdx]` (row-major:
`srcX = (cell%cols)*tileW`, `srcY = (cell/cols)*tileH`), and reads the tile
buffer `cols*2*rows-1` bytes into `PTR_DAT_0045a390` (note: one byte short).

For level 1 this yields **two layers**:

- layer 0: `map\000.map` = 13x256 tiles of 32x32, atlas `surface[3]` =
  `bg001.bmp` (the terrain atlas; tile `v` -> cell `(v%4, v/4)`).
- layer 1: `map\001.map` = a 1x1 map whose single tile is 320x240, atlas
  `surface[7]` = `bg002.bmp` (a 320x240 blue gradient, `(0,57,143)` at the top
  fading to black). This is a full-screen background layer.

`FUN_00454535` renders the layers in **two passes**, iterating the layer list
**descending (2,1,0)**: pass 1 draws the scrolled layers (flag `0`, integrating
their scroll velocity and setting the map camera `+0x6034/+0x6038 = scroll>>5`),
then pass 2 draws the fixed layers (flag `1`). Because both level-1 layers are
flag `0`, the draw order is layer 2 (unused), layer 1 (`bg002` background),
then layer 0 (terrain). Map tiles blit with flag `1` (transparency; the color
key is black), and only retry with `0x11` when the blit fails — so the terrain
tiles are color-keyed and map tile `0` (the empty shaft, **55%** of the level)
is transparent, revealing the `bg002` background underneath.

The native previously filled the backbuffer black and skipped tile-0 draws, so
the shaft showed black. `drawMap` now blits `app.background` (`bg002.bmp`)
full-screen before drawing the terrain tiles (`app.background` is loaded from
`bmp/bg002.bmp` in `WinMain`). Verified by capture: the playfield now contains
the blue `bg002` gradient where tile-0 is empty, with the olive/pink terrain
and animated decoration tiles composited on top.

## Audio and input

The original imports `dsound.dll`, `Kbgm32.dll` (MIDI), `ddraw.dll`,
`D3DRM.DLL`, and `dinput.dll` (`DirectInputCreateA`). It uses DirectSound for
effects and `Kbgm32` for music; the native reproduces this with WinMM MCI
(`midi\*`) and `PlaySoundA` (`wav\*`) playing the same files, which is
functionally equivalent. It uses DirectInput for the **keyboard and mouse
only**: the `GUID_SysKeyboard` and `GUID_SysMouse` GUIDs are both present
(`0x57BDC`/`0x57BCC`) and `GUID_Joystick` is absent, and there are no joystick
`winmm` imports. There is therefore **no joystick support to add**; the native
keyboard handling already matches the original's input surface.

## Spawn position and frame timing

Two playback bugs were fixed against the recovered source.

**Spawn at the bottom of the shaft.** Entity X/Y are *screen-space pixels*
(1/32 px units) that are added to the layer scroll, so the world position is
`scroll + entity`. `FUN_004562F4` seeds the layer-0 scroll accumulator to
`+0x00 = 0x000A00` (80px) and `+0x04 = 0x03E040` (world 7938px, row ~248), and
`FUN_00454084` spawns the player at entity `(0x1000, 0x1640)` = screen
`(128, 178)`. The player's world spawn is therefore `(80+128, 7938+178)`,
i.e. the bottom of the 256-row shaft. The native previously used the raw
screen values `(128, 178)` as *world* coordinates with the camera at `0`, so
the player appeared at the top of the map. `startNewGame` (and the life-lost
respawn) now seed the world position `(208, 8088)` — resting on the row-254
floor in the cols 5-7 slot — and the camera to `(80, 7938)`. Verified by
capture: the green `chr001` player sprite renders at the bottom of the shaft
with the row-254/255 wall band below it.

**Frame rate.** The `FUN_00454535` tail busy-waits on `timeGetTime()` for
16 ms, but that wait is gated on the SOFTWARE VSYNC flag at `+0x32c`: the
shipped `jne` at VA `0x454ca0` skips the wait when the flag is off. With both
options off the shipped loop free-runs, measured at **~125 fps** on the 120 Hz
reference display. Because the movement, physics, and animation updates are
all per-frame, that also runs the game at roughly twice its 60 fps design; the
`0x78` = 120-frame timer unit is ~1 second only as a side effect of the high
frame rate.

The native is now locked to **60 fps**, the requested gameplay rate. `WinMain`
advances the loop on a repeating 16/17/17 ms schedule (16.667 ms average,
exactly 60 Hz) with `timeBeginPeriod(1)` for accurate sleeps; `WM_PAINT` still
repaints on uncover. `GameStateMachine::finishFrame` no longer busy-waits, so
the Win32 loop owns pacing. To keep the TIME counter at one second per unit at
60 fps, the timer threshold is 60 frames (`0x3B` with the pre-increment
comparison, so the 60th frame ticks) instead of the shipped `0x78` that
assumed the ~122 fps free-run. Measured with a client-DC probe: the rendered
TIME digits advance every **1.01 s**, i.e. the logic is running at ~59.5 Hz.

The per-blit `CreateCompatibleDC`/`DeleteDC` churn in `drawBitmap` was also
replaced with a single cached source DC, and the redundant post-message
`drawFrame` was dropped.

**Camera dead-zone and up-only scroll.** `update()` now follows `FUN_00453B64`
instead of centring the player. The entity is kept in screen space: the camera
scrolls horizontally only once the player leaves the `[0x40,0xC0]` = `[64,192]`
dead-zone, so within that band the player moves and the view is fixed, and past
either edge the view moves while the player is pinned to the boundary. The
vertical camera follows only while the player is inside the top `0x40` = 64px
**and rising**; it never scrolls back down (the stage is climbed upward from the
bottom shaft). Because the camera is up-only, the respawn path must re-seed it
to `(kSpawnCameraX, kSpawnCameraY) = (80, 7938)` (`FUN_004562F4` re-seeds the
layer-0 scroll on every restart). The miss test is likewise screen-based
(`FUN_00453B64`: miss when screen Y > `0x100` = 256px) rather than world-based,
so a player who falls out of the bottom of the view dies as in the original.
Verified by an env-guarded state dump: a jump pins the player at screen Y 64
while `cameraY` tracks it, the camera stays put on the way down, and the fall
past screen Y 256 respawns at the shaft bottom with the camera restored to 7938.

## DirectDraw surface and palette semantics

The original draws through the Quadruple D DirectDraw wrapper. The device
initialiser `FUN_0044DDFC` (entered from `FUN_0044F764`/`FUN_0044F6C0`) runs
`DirectDrawCreateEx` (IDirectDraw7), `GetCaps` (`vtable+0x2c`), then branches on
the fullscreen flag:

- **Windowed** (`game.ini` `fullscreen=OFF`): `SetCooperativeLevel(DDSCL_NORMAL)`
  (`+0x50`) and **no `SetDisplayMode`**. It creates a primary surface
  (`DDSURFACEDESC.dwFlags = DDSD_CAPS`, `ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE`)
  and a separate plain offscreen surface (`dwFlags = DDSD_CAPS|DDSD_HEIGHT|
  DDSD_WIDTH`) sized to the game resolution, then attaches a clipper
  (`CreateClipper` `+0x10`, `SetHWnd` `+0x20`) to the primary (`SetClipper` `+0x70`).
- **Fullscreen**: `SetCooperativeLevel(0x53 =
  FULLSCREEN|EXCLUSIVE|ALLOWREBOOT|ALLOWMODEX)`, then `SetDisplayMode` (`+0x54`),
  then a flipping chain — one primary surface with `dwFlags =
  DDSD_CAPS|DDSD_BACKBUFFERCOUNT`, `dwBackBufferCount = 1`, `ddsCaps.dwCaps =
  DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX` — followed by
  `GetAttachedSurface(DDSCAPS_BACKBUFFER)` (`+0x30`).

**No surface ever specifies a pixel format**, so every surface inherits the
current display-mode format. A **DirectDraw palette is created and attached to
the surfaces only when the display bpp is 8** (`FUN_0044CF90` returns the bpp;
the palette is generated 3-3-2 in fullscreen and the system palette is queried in
windowed). On a modern true-colour desktop (bpp >= 16) no palette surface exists,
so the 8-bit art must be converted by GDI.

Consistently, the 8-bit BMP loader `FUN_0041BF08` is **GDI**, not DirectDraw: it
reads the `BITMAPINFOHEADER` + palette + pixel data from the stream, allocates a
`BITMAPINFO` with room for a 256-entry colour table, and produces a **DIBSection**
(`CreateDIBSection`, thunk `0x406318`) for uncompressed `BI_RGB`/`BI_BITFIELDS`
(a mem DC + `CreateDIBitmap`, thunk `0x406320`, otherwise). The palettised BMPs
therefore become 8-bit DIB sections carrying their own colour table, which GDI
maps to the display format when blitted.

Drawing goes through a device abstraction rather than raw surface calls.
`FUN_0044BC04(surface, x, y, mode, srcRect)` forwards to the active driver
(`surface->[0x90]->[0xbc]`), which intersects the rect with the surface clip
(`FUN_0044BB4C`) and issues the blit (`surface->[4]->[0x1c]`). The low bit of
`mode` selects **colour-key transparency** for that blit and `0x10` is a
repeat/clip flag: map tiles blit with `0` or `1` (the per-tile transparency flag
at tile-entry `+0x14`), the bitmap font with `0x11`, and the HUD backdrop uses
`FUN_0044B988` with `DDBLT_WAIT | DDBLT_COLORFILL` (`0x1000400`). The colour key
is **black** (palette index 0), which is why the black-keyed glyph and tile blits
in the native are correct.

Semantically the native already matches this for the windowed true-colour case:
`LoadImageA(..., LR_CREATEDIBSECTION)` yields the same 8-bit DIB sections, GDI
performs the same colour-table conversion, `TransparentBlt` uses the same black
key, and the `drawHud` colour-fill matches `FUN_0044B988`.

**Present.** `FUN_0044CFEC` is the frame present. In fullscreen it `Flip`s the
primary (`primary[4]->vtable+0x2c`, `DDFLIP_WAIT`); in windowed mode it `Blt`s
the whole offscreen surface onto the primary (`primary[4]->vtable+0x14`,
`DDBLT_WAIT`), with the **source rect = `(0, 0, screenW, screenH)` =
`(0,0,320,240)`** and the **dest rect = the form's client rect translated to
screen coordinates** (the form's `vtable+0x40` client rect plus the client origin
from `FUN_004246CC`). So the original maps the entire 320x240 frame onto the
entire window client area — it **stretches**; it is not a 1:1 present.

Measured empirically, the shipped form `TFrm_main` has a normal (restored)
client of **320x240** (window rect `326x269`) but launches **maximized**
(`showCmd = 3`); on the test desktop that makes its client the full screen
(`2560x1369` on a 2560x1440 display), so the game is stretched to fill the
screen, non-uniformly in aspect. The native's `WinMain` present is the same
model — a `StretchBlt` of the 320x240 back buffer onto the client — just with a
640x480 client (a clean 2x integer scale). Present semantics therefore already
match; only the chosen client size differs.

