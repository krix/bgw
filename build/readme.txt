Bunguru WA - native reconstruction
==================================

Run bgw.exe to start the game. Keep bgw.exe, game.ini, and the bmp, data,
map, midi, and wav folders together in this directory.

Requirements
------------
64-bit Windows. The executable is self-contained and does not require
MinGW DLLs.

Controls
--------
Left / Right arrows   Move
Space or Ctrl         Jump
Space, Z, or X        Confirm menu item
Arrow keys            Navigate menus
Esc                   Back / return to menu
A-Z, Backspace        Enter rank name

Options
-------
The option screen sets the game level (EASY / NOMAL / HARD), jump button,
WAIT, software VSYNC, and opens the ranking page.

game.ini stores display and option settings. data\rank.dat stores the
high-score table. Both are written when the game closes normally. Delete
game.ini to restore the defaults.

Credits
-------
Original game: Bunguru WA by Buster (2001, freeware).
Reconstruction: reverse-engineered native Win32/C++ port.

The original game, graphics, music, and sound effects remain the property
of their authors. See the repository README.md for details.
