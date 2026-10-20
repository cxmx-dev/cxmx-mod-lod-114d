# cxmx-mod (Diablo II: LoD 1.14d)

A single-player and LAN mod for Diablo II: Lord of Destruction 1.14d, loaded through PlugY.

> **TEST BUILD.** Dev cheats are on: new characters start at level 25 with every difficulty open, and `/map`, `/$`, `/pot` and Telekinesis on any item work. Single player and LAN (TCP/IP) only. No Battle.net or online play.

## What's new (2026-10-20)
- Jewel-in-jewel nesting up to 7 deep, kept through moves, save and reload.
- A "Contains:" box beside the tooltip of any item that holds socketed items.
- Lost nested items are recovered into the inventory, stash, cube or ground on load.
- "Gold Find +N%" / "Magic Find +N%" boxes on the player and merc panels, drawn under tooltips.
- Find Item and Find Potion for every class at base level 20, 0 mana (untested in game).

See [CHANGELOG.md](CHANGELOG.md).

## You need
- Your own Diablo II + Lord of Destruction, patched to 1.14d
  (Game.exe version 1.14.3.71, SHA-256 `631066C1649C4EA9FFE48BF97E24C00BCA1F7A6759C21150F1A79982589ADAAF`).
- Windows 10/11 and a GPU with OpenGL 3.3.
- [PlugY 14.03](http://plugy.free.fr/en/), [BaseMod 1.13.9 (BaseMod.dll 1.1.3.9)](https://www.moddb.com/mods/basemod/downloads/basemod113), [D2GL 1.3.3](https://github.com/bayaraa/d2gl/releases) and [SGD2FreeRes](https://github.com/mir-diablo-ii-tools/SlashGaming-Diablo-II-Free-Resolution/releases). See [CREDITS.md](CREDITS.md).

## Install
1. Copy your whole Diablo II folder to a new folder, e.g. `%USERPROFILE%\Games\cxmx-mod`. Keep your original untouched.
2. Put PlugY (`PlugY.exe`, `PlugY.dll`, `PlugY\` folder), `BaseMod.dll`, D2GL (`glide3x.dll`, `d2gl.mpq`) and SGD2FreeRes (dll + mpq) into that folder.
3. Copy this mod into the same folder and overwrite the ini files: everything inside `mod\`, plus `Play.bat`, `Play-Windowed.bat`, `patch\` and `cxmxdev.dll`. The release zip already has this layout.
4. Patch your copied Game.exe and BaseMod.dll from that folder:
   ```
   powershell -ExecutionPolicy Bypass -File patch\Apply-GameExePatch.ps1
   powershell -ExecutionPolicy Bypass -File patch\Apply-BaseModPatch.ps1
   ```
   Each one checks the stock SHA-256 first, keeps a `.orig` backup and checks the SHA-256 of the result. Add `-Check` for a dry run or `-Undo` to restore.
5. Run `Play.bat` (fullscreen) or `Play-Windowed.bat`. Close the game fully before changing any file.

Saves go to the `Save\` folder inside the mod folder. Back up any characters you care about.

## LAN (TCP/IP)
- Every player needs the same files and the same patched Game.exe and BaseMod.dll.
- Host: Other Multiplayer > TCP/IP Game > Host Game. Others: Join Game, then enter the host's LAN address.
- The host runs the server-side features (boss spawns, portrait teleport, gold pickup while charging). Allow the game through the Windows firewall on the host.

## Settings
`cxmxdev.ini` turns features on (1) or off (0). It is read at game start.

## Uninstall
Delete the mod folder. Your original Diablo II folder was never changed.

## License
© 2026 cxmx-dev. **cxmx-mod-lod-114d by cxmx-dev** is licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) ([LICENSE](LICENSE)): credit cxmx-dev, no commercial use, share changes under the same license. A copy whose window title shows "UNOFFICIAL BUILD - not by cxmx-dev" has changed mod files.

Third-party tools keep their own licenses ([CREDITS.md](CREDITS.md)). Diablo II and Lord of Destruction are trademarks of Blizzard Entertainment. Not affiliated.
