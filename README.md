# Barony Archipelago

An Archipelago multiworld randomizer for Barony: an APWorld plus a modded Barony client with a
launcher and tracker window.

| Folder | What it is |
|---|---|
| `apworld/barony/` | The APWorld (Python): options, items, locations, logic. Zip as `barony.apworld`. |
| `client/src/` | The game client (C++) and the tracker window, added into Barony's own source |
| `client/barony-archipelago.patch` | All the Barony changes as one `git am` patch (Barony master `962a5ce`) |
| `client/PATCHES.md` | Where each hook goes, the files the game and tracker share, chat commands |
| `client/game-files/` | Images, icons and models the client adds to the Barony folder |
| `client/archipelago-example.cfg` | Optional settings file (connecting without the tracker, holiday themes) |
| `tools/gen_cpp_ids.py` | Writes `client/src/archipelago_ids.hpp` from the APWorld's data |
| `tools/patch_apcpp.py` | Makes APCpp's message queue thread-safe (applied by the build) |
| `yamls/` | Example player settings |
| `BUILDING.md` | How to build the modded `barony.exe` on Windows (GitHub Actions does it on every push) |

## Playing

Download `BaronyAP-windows.zip` and `barony.apworld` from the `latest-build` release. Copy the
zip's contents into your Barony folder (see `HOW-TO-PLAY.txt` inside), install the apworld, and
start Barony from Steam: the Barony Archipelago window opens first to connect.

## Goals

Baron Herx, the Devil, the Twin Liches of the Sanctum, Chosen Bosses (the ones you pick, spread through
the run), or Boss Rush (a dungeon of 10 to 44 floors, then the bosses you
pick back to back, with an optional rest town between them).

## Checks

Floors (every 1 to 10), character levels, Enemy Sanity (green enemies), bestiary, Archipelago
pickups on the floor and in chests, shop items, a Minotaur and bosses before your goal, secret
areas, and skill ranks by class.

## Items

Extra Life, permanent stat items, Max HP Up, Starting Gold Bonus, Progressive Compass, XP Boost,
Gold Pouch, Healing Potion, Food Ration, Random Item, Progressive Lantern, spells (Spell Sanity), and the Minotaur, Ambush, Polymorph, Fumble,
Blindness, Teleport, Hunger, Darkness and Curse traps.

## In the dungeon

Floor Keys (or defeat every enemy) open the stairs, with a Compass and a key warning on Minotaur
floors. Lives restart the floor instead of ending the run, with Death Link, death penalties and a
death counter. Optional: Floor Shuffle, Enemy Randomizer, Randomize Loot, bigger floors, extra
monsters, Music Shuffle and Workshop music mods, boss and enemy level scaling, Bounty Enemies,
Champion floors (a tough guard at the stairs), Death Markers (your grave gives some gold back), a
Merchant Caravan (a travelling shop with Archipelago items and hints), and a 5-second safe window
after losing a life, and Floor Themes (Dark, Haunted, Swarm, Gold Rush and more).

## Tracker window

Connects you (port or room link, with auto-reconnect), then shows a map of the floor as you
explore it (AP items and green enemies always, other items once seen), what's left on the floor,
the Floor Key and Minotaur, deaths per player, and the item log.

## Status

- **APWorld**: generates and passes its tests plus Archipelago's general world tests; fuzzed
  with hundreds of multiworlds with random options.
- **Client**: built for Windows by GitHub Actions and played on Windows.
