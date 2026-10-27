# Where the Archipelago code hooks into Barony

`barony-archipelago.patch` applies on top of Barony's `master` at commit `962a5ce`
(Feb 20, 2026). It adds three files to `src/` and one or two lines to each file below.
Every added line is marked with an `// Archipelago` comment, so they're easy to find again
when updating to a newer Barony.

| File | Function | Hook | What it does |
|---|---|---|---|
| `src/game.cpp` | `gameLogic()` | `AP::tick()` | Runs the client every game tick: connection, received items, floor/level checks, Death Link in, pickups |
| `src/files.cpp` | `physfsLoadMapFile()` | `AP::remapLevelLine()` | Floor shuffle: picks which line of `maps/levels.txt` each floor uses |
| `src/maps.cpp` | `generateDungeon()` | `AP::onDungeonGenerated()` | Applies queued Minotaur and Darkness traps to a new normal floor |
| `src/maps.cpp` | `generateDungeon()`, after the secret-exit choice | `AP::noteSecretExit()` | Gives a pickaxe and levitation potion on floors with a secret entrance |
| `CMakeLists.txt` | Windows | `BaronyAPTracker` target | The tracker window (`src/BaronyAPTracker.cpp`) |
| `src/actmonster.cpp` | `actMonster()` init | `AP::onMonsterInit()` | Shop AP items, difficulty scaling, boss keeps damage |
| `src/actmonster.cpp` | `actMonster()` death | `AP::onMonsterDeath()` | Kill milestones, bestiary, boss checks, goal |
| `src/actplayer.cpp` | `actPlayer()` | `AP::tryConsumeLife()` | Lives: restart the floor instead of dying |
| `src/actplayer.cpp` | `actPlayer()` | `AP::onPlayerDied()` | Sends Death Link on a real death |
| `src/actladder.cpp` | `actLadder()`, `actPortal()` | `AP::canDescend()` | Stair Keys block the way down |
| `src/monster_minotaur.cpp` | `getMinotaurTimeToArrive()` | `AP::adjustMinotaurTime()` | Hard mode: Minotaur arrives sooner |
| `CMakeLists.txt`, `src/CMakeLists.txt` | | | Builds `archipelago_barony.cpp` and links APCpp from `extern/APCpp` |

`files.cpp` is also part of the level editor, so its hook is inside `#ifndef EDITOR`.

## Files added

- `src/archipelago_barony.hpp` / `.cpp`: the client
- `src/archipelago_ids.hpp`: item and location numbers, generated from the APWorld by
  `tools/gen_cpp_ids.py`. Regenerate it whenever `apworld/barony/data.py` changes.

## Console commands

Type these into the in-game chat box (they all start with `/`).

| Command | |
|---|---|
| `/ap_connect host:port slot [password]` | Connect to a room |
| `/ap_disconnect` | Disconnect |
| `/ap_status` | Connection, Stair Keys, lives, kill count, queued traps |
| `/ap_say message` | Chat in the room |
| `/ap_trap name` | Test a trap now: `minotaur`, `ambush`, `polymorph`, `fumble`, `blind`, `teleport`, `hunger`, `darkness`, `curse` |

## Known limits of this first version

- **Connect before you start or continue a run.** Floor shuffle needs the slot data when a
  floor loads. `archipelago.cfg` connects at the main menu, which takes care of this.
- **Co-op is experimental.** Only the host connects. Floor shuffle doesn't work in co-op yet
  (the other player's game would load unshuffled floors), and XP/gold given to the second
  player may not show on their screen until the game syncs it.
- **The pickup and shop item borrow the luckstone model** until there's a logo model.
- Items and checks are only handled while you're in the dungeon; anything received at the
  main menu waits and is applied when you're back in.
