# Where the Archipelago code hooks into Barony

`barony-archipelago.patch` applies on top of Barony's `master` at commit `962a5ce`
(Feb 20, 2026). It adds four files to `src/` and a few lines to each file below.
Every added line is marked with an `// Archipelago` comment, so they're easy to find again
when updating to a newer Barony. Hooks in files the level editor also builds are inside
`#ifndef EDITOR`.

| File | Hook | What it does |
|---|---|---|
| `src/game.cpp` `main()` | `AP::launcherGate()` | Opens the launcher (tracker window) first and waits for Connect & Play / Play without Archipelago |
| `src/game.cpp` `gameLogic()` | `AP::tick()` | Runs the client every tick: connection, items, checks, Death Link, traps, tracker files |
| `src/game.cpp` | `AP::lockedMapSeed()` | Layout Seed: the same layout for a floor every run |
| `src/game.cpp` | `AP::drawHud()` | Status, lives, deaths, panels, compass and death screen |
| `src/files.cpp` `getCurrentHoliday()` | `AP::holidayThemeAllowed()` | Holiday themes off unless `holiday_theme=1` in archipelago.cfg |
| `src/files.cpp` `physfsLoadMapFile()` | `AP::bossRushNextMap()` | Boss Rush: loads the next boss arena or the rest town |
| `src/files.cpp` `physfsLoadMapFile()` | `AP::remapLevelLine()` | Floor Shuffle, and Boss Rush skipping boss floors on the way down |
| `src/files.cpp` `loadMap()` | `AP::takeFloorSizePercent()` | Floor Size: enlarges the base map as it loads |
| `src/maps.cpp` `generateDungeon()` | `AP::requestFloorSize()`, `AP::scaleFloorEntities()` | Floor Size: more to find on a bigger floor |
| `src/maps.cpp` `generateDungeon()` | `AP::onDungeonGenerated()` | Queued Minotaur and Darkness traps |
| `src/maps.cpp` `generateDungeon()` | `AP::noteSecretExit()` | Pickaxe and levitation on floors with a secret entrance |
| `src/maps.cpp` `monsterCurve()`, placed monsters | `AP::randomizeMonster()`, `AP::randomizePlacedMonster()` | Enemy Randomizer |
| `src/actchest.cpp` `createChestInventory()` | `AP::onChestCreated()` | AP items in chests; Randomize Loot |
| `src/actgeneral.cpp` | `AP::onBreakableDestroyed()` | AP items from crates, pots and barrels |
| `src/actladder.cpp` ladders and portals | `AP::canDescend()` | Floor Keys and the items-left warning |
| `src/actladder.cpp` victory portals | `AP::bossRushSkipVictory()` | Boss Rush: on to the next boss instead of the ending |
| `src/actmonster.cpp` init | `AP::onMonsterInit()` | Shop AP items, Randomize Loot in shops, scaling, boss HP |
| `src/actmonster.cpp` death | `AP::onMonsterDeath()` | Kills, bestiary, boss checks, goal |
| `src/actplayer.cpp` | `AP::tryConsumeLife()`, `AP::onPlayerDied()` | Lives (restart the floor) and Death Link out |
| `src/charclass.cpp` `initClass()` | `AP::lockedClass()` | Lock Class |
| `src/items.cpp` | `AP::isApItem()`, `AP::isFloorKey()`, `AP::isCompassItem()`, `AP::useCompassItem()` | Names, models and use of the AP item, Floor Key and Compass |
| `src/interface/playerinventory.cpp` | same | Their inventory icons |
| `src/monster_minotaur.cpp` | `AP::adjustMinotaurTime()` | Minotaur Time |
| `src/opengl.cpp` | `AP::isMarkedEnemy()` | Enemy Sanity enemies glow green |
| `src/engine/audio/music.cpp` | `AP::pickMusicVersion()` | Music Mods |
| `src/engine/audio/sound_game.cpp` | `AP::MusicOverride` | Music Shuffle |
| `src/init.cpp` | `AP::loadExtraModels()` | The AP item and Floor Key models |
| `src/ui/MainMenu.cpp` | `AP::titleImagePath()` | The title with the Archipelago logo |
| `CMakeLists.txt`, `src/CMakeLists.txt` | | Builds the client and the `BaronyAPTracker` window, links APCpp |

## Files added

- `src/archipelago_barony.hpp` / `.cpp`: the client
- `src/archipelago_ids.hpp`: item and location numbers, generated from the APWorld by
  `tools/gen_cpp_ids.py`. Regenerate it whenever `apworld/barony/data.py` changes.
- `src/BaronyAPTracker.cpp`: the launcher and tracker window (Windows)

## Files the game and the tracker share (in `archipelago/` next to barony.exe)

| File | Written by | |
|---|---|---|
| `connect.txt` | tracker | Server and name to connect with; the game reads and deletes it |
| `tracker.txt` | game | Status, floor, counts, deaths and log for the right-hand column |
| `trackermap.txt` | game | The floor map: explored tiles, items, AP items, green enemies |
| `tracker_login.txt` | tracker | The last server, port, name and room link typed in |
| `<seed>_<slot>.txt` | game | Saved progress for one multiworld slot |

## Console commands

Type these into the in-game chat box (they all start with `/`).

| Command | |
|---|---|
| `/ap_connect host:port slot [password]` | Connect to a room |
| `/ap_disconnect` | Disconnect |
| `/ap_status` | Connection, lives, kill count, queued traps |
| `/ap_say message` | Chat in the room |
| `/ap_trap name` | Test a trap now: `minotaur`, `ambush`, `polymorph`, `fumble`, `blind`, `teleport`, `hunger`, `darkness`, `curse` |

## Known limits

- **Connect before you start or continue a run.** Floor shuffle needs the slot data when a
  floor loads; the launcher takes care of this.
- **Single player.** Co-op isn't supported.
- Items and checks are only handled while you're in the dungeon; anything received at the
  main menu waits and is applied when you're back in.
