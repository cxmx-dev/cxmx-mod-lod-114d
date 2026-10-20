# Changelog

## 2026-10-20

- **Find Item and Find Potion for every class.** Every character, Barbarians included, gets Barbarian Find Potion and Find Item at base level 20 on every game join (below 20 or missing goes to 20, above 20 is kept). Both cost 0 mana, and Find Item needs level 1. In LAN games the host must run this DLL.
- **Untested in game:** both skills showing on all classes at level 20 with Mana Cost 0; Barbarians above 20 keeping their level; use on corpses by non-Barbarian classes, shapeshifted Druids and Assassins; LAN guests getting the skills without a crash.
- **Jewel-in-jewel nesting, up to 7 deep.** Socketed jewels can hold socketed jewels. The whole chain now survives equip, unequip, moves between player and merc, save and reload. The save loader reads every level, and item copies keep the full chain. Set the cap with `NestMax` (default 7) in `cxmxdev.ini`.
- **Startup loader fix.** The nested-item loader is ready before the first character loads, so a character or merc carrying nested items loads normally.
- **Gold Find / Magic Find boxes.** The player and merc panels read "Gold Find +N%" and "Magic Find +N%". The boxes are drawn under item tooltips, so tooltips stay readable.
- **Contains: tooltip.** Hovering an item that holds socketed items, in any location, shows a box beside the tooltip: an indented tree of the contents in item quality colours, "Socketed (x/y)" per item, up to 7 levels deep, then "+N more" after 16 lines. It is hidden while an item is on the cursor. Client side. Settings: `CT`, `CTLines`, `CTDepth`, `CTBoxPasses`.
- **Lost-item recovery.** If a nested item no longer fits its parent when a character loads, it is kept with its contents and placed in the inventory, then the stash, then the cube, else on the ground at the player's feet.

## 2026-10-19

- First public test build.
