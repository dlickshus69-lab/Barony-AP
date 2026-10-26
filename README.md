# Barony Archipelago

An Archipelago multiworld randomizer for Barony.

| Folder | What it is |
|---|---|
| `apworld/barony/` | The APWorld (Python): options, items, locations, logic. Zip as `barony.apworld`. |
| `client/src/` | The game client (C++), added into Barony's own source |
| `client/barony-archipelago.patch` | All the Barony changes as one `git am` patch (Barony master `962a5ce`) |
| `client/PATCHES.md` | Where each hook goes, chat commands, known limits |
| `client/archipelago.cfg` | Connection settings file to put next to `barony.exe` |
| `tools/gen_cpp_ids.py` | Writes `client/src/archipelago_ids.hpp` from the APWorld's data |
| `BUILDING.md` | How to build the modded `barony.exe` on Windows |

## Status

- **APWorld**: generates. Tested with 40 multiworlds of 3 Barony slots each with fully random options.
- **Client**: written and checked to compile against Barony's headers. Not built into a
  running game or played yet.

## Checks

Floors (every 1 to 10), character levels, kill milestones, bestiary, Archipelago pickups on the floor,
shop items, a Minotaur and bosses before your goal, and (optional) secret areas.

## Items

Progressive Stair Keys (optional), Extra Life, permanent stat items, Max HP Up, Starting Gold Bonus,
XP Boost, Gold Pouch, Healing Potion, Food Ration, Random Item, and the Minotaur, Ambush, Polymorph,
Fumble, Blindness, Teleport, Hunger, Darkness and Curse traps.
