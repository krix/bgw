# BGW — Bunguru WA native reconstruction

A from-scratch native C++ reconstruction of **Bunguru WA** (ぶんぐるわ), a 2001
Japanese freeware 2D action game by **Buster**. The original is a 32-bit Delphi
executable built with the *Quadruple D* DirectX library. This project rebuilds
the game as a self-contained 64-bit Win32 program using only the original game
data, reverse-engineered behavior, and GDI rendering — no DirectDraw, no Delphi
runtime, and no external DLLs.

The reconstruction is behaviorally and visually matched against the shipped
binary. Every reachable screen has been verified pixel-for-pixel: title/menu,
options, ranking, gameplay field and HUD, miss/game-over, name entry, rank
display, and the clear/scoreboard sequence.

## The game

You control **Insumausu**, a small character who must keep climbing upward.
Purple **Bunguru WA** items award 100, 500, and 1000 points when collected
consecutively and restore a little jump power. The **golden Bunguru WA** at the
top clears the game.

Walls restore power but crumble as you stand on them. Blue, green, and red
walls decay at different rates (red fastest); brown walls never crumble. Each
jump costs power, and you can jump up to three times in succession. Jumping from
the center of a floor gives extra height.

The original runs at **320×240** and targets **60 fps**. This reconstruction
locks the game loop to 60 fps and reproduces the original one-second `TIME`
tick.

### Controls

| Action | Keys |
| --- | --- |
| Move | Left / Right arrows |
| Jump | Space (or Ctrl) |
| Confirm menu item | Space, Z, or X |
| Navigate menus | Arrow keys |
| Back / return to menu | Esc |
| Enter rank name | A–Z, Backspace |

The option screen exposes game level (EASY / NOMAL / HARD), jump button
assignment, WAIT, software VSYNC, and the ranking page. Settings and high scores
are saved to `game.ini` and `data\rank.dat` when the game closes normally.

## Repository layout

```
BGW/
├── README.md
├── original/     Original 2001 release: reference executables, DLLs,
│                 readme, and the pristine BMP/data/map/MIDI/WAV assets.
├── recreated/    Reconstructed C++ sources, build scripts, and analysis docs.
│   ├── src/
│   ├── docs/
│   ├── build-win32.ps1
│   └── build-native.ps1
├── build/        Final runnable reconstruction: bgw.exe plus the
│                 assets it loads and a default game.ini.
├── web/          Optional dependency-free Canvas browser port.
├── tools/        Ghidra helper scripts used during analysis.
└── analysis/     Local Ghidra project (git-ignored, not part of the upload).
```

The three requested groups are deliberately separate: **`original/`** holds the
shipped game, **`recreated/`** holds the reconstructed source, and **`build/`**
holds the finished runnable build.

## Running the reconstruction

Run `build\bgw.exe`. It is a self-contained 64-bit Windows executable
(statically linked MinGW runtime), so no MinGW DLLs are required.

Keep the asset folders next to the executable:

```
build\
├── bgw.exe
├── readme.txt
├── game.ini
├── bmp\  data\  map\  midi\  wav\
```

`game.ini` controls fullscreen/windowed mode and the saved options. Deleting it
restores the defaults. The first launch is windowed with EASY selected.

### Original game

`original\bgw.exe` is the working 2001 binary with the four small Windows 11
compatibility fixups documented in `recreated\docs\NOTES.txt`. Run it from
inside `original\` so it can find its assets. `original\bgw-pristine.exe` is the
untouched release binary, kept as the ground-truth reference.

`original\readme.txt` is the original Shift-JIS readme; `original\readme.en.txt`
is an English translation.

## Building from source

Requirements:

- Windows with PowerShell 5.1 or newer
- MinGW-w64 x86-64 with `g++` and `windres` (the scripts default to
  `C:\mingw64\bin`; edit `$compiler` / `$windres` in the scripts if yours is
  elsewhere)

Build the runnable Win32 target from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File recreated\build-win32.ps1
```

This compiles the sources, embeds the original application icon from
`recreated\src\bgw.rc` / `bgw.ico`, and writes **`build\bgw.exe`**.

Build and run the console probe:

```powershell
powershell -ExecutionPolicy Bypass -File recreated\build-native.ps1
```

The probe loads the original assets and map and drives the reconstructed state
machine without video or audio. It prints surface, sprite, stage, and map
counts, then the startup state.

### Build flags

The Win32 build uses:

```
-Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections
```

`-s` removes the COFF symbol/string table and debug sections, which otherwise
add roughly 2.1 MB to a MinGW executable. The result is about **1.05 MB**. Most
of the remaining size is the static C++ runtime and x64 unwind tables
(`.pdata`/`.xdata`); the original is a 421 KB 32-bit Delphi binary with a much
smaller runtime.

## Web port

`web/` contains a small Canvas/JavaScript port that loads the same BMP, WAV, and
map assets from `build/`.

```powershell
node web/server.mjs
```

Then open <http://127.0.0.1:8123/web/>. The server serves the repository root,
so the asset paths resolve to `build\bmp`, `build\wav`, and `build\map`.

## Reconstruction notes

- **60 fps lock.** The native loop uses a repeating 16/17/17 ms schedule
  (16.667 ms average). The original frame limiter and the reconstructed
  `TIME` counter were matched against captured frames; `TIME` advances once per
  60 frames (~1.01 s measured).
- **Difficulty.** On every new game, the level remap is applied from the
  pristine tile set using the selected difficulty, so EASY/NOMAL/HARD produce
  the correct blue/green/red breakable counts.
- **Icon.** The original `RT_GROUP_ICON "MAINICON"` image (32×32, 4 bpp) is
  embedded and installed as both the large and small window icon. Extracting it
  from the new executable yields a byte-identical `.ico`.
- **Persistence.** `game.ini` and `data\rank.dat` are written only on a graceful
  close, matching the original `FormDestroy` behavior.
- **Rendering.** GDI replaces DirectDraw. The presentation is 320×240 with
  nearest-neighbor scaling.
- **Window title.** The native window is titled `Bunguru WA`.

Detailed evidence, routine addresses, patch notes, and the verification method
live in:

- [`recreated/docs/NATIVE_STATUS.md`](recreated/docs/NATIVE_STATUS.md)
- [`recreated/docs/NOTES.txt`](recreated/docs/NOTES.txt)

## Credits

**Original game:** Bunguru WA by **Buster** (2001, freeware).

The original readme credits these components:

- **Quadruple D** — Copyright SANDMAN
- **KBGM Version 0.08** — Copyright (c) 1996–1999 Kr.Shin
- **KbgmPlayer Ver1.0** — Copyright naruto

**Reconstruction:** reverse-engineered with Ghidra and REA from the shipped
binary, its assets, and captured runtime behavior. The C++ sources, build
scripts, and documentation in `recreated/` were written for this project.

## Legal and asset notice

The original game, its executables, graphics, music, and sound effects remain
the property of their respective authors. They are included in `original/` and
`build/` for preservation, interoperability, and study of the original release.
They are **not** licensed for redistribution by this project, and the original
readme explicitly prohibits unauthorized use of the graphics, music, and WAV
files.

If you are a rights holder and want these files removed, please open an issue
and they will be taken down. If you fork or redistribute this repository,
consider removing `original/` and the asset copies in `build/` unless you have
permission to share them.
