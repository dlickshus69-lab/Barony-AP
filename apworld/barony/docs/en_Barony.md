# Barony

## What does randomization do to this game?

Barony is a roguelike, so instead of fixed chests, checks come from things you do:

- Reaching floors (every floor, or every 2nd, 3rd ... 10th floor)
- Reaching character levels
- Enemy Sanity: random enemies on each floor glow green; every few green kills (Kill Count, counted
  across runs) or every green kill (Marked Enemies) is a check
- First kill of each monster type (bestiary)
- Archipelago pickups lying on the dungeon floor
- Archipelago items sold by shopkeepers
- Defeating a Minotaur and bosses before your goal
- Visiting secret areas (optional)
- Raising your skills to Basic, Skilled, Expert, Master and Legendary (optional, for the class you
  pick in your options, plus Lore, Trading and Stealth for any class)

Everything you receive is kept across runs. Permanent items (stat boosts, Max HP Up, Starting
Gold Bonus) are applied to every new character you start.

Spell Sanity (optional) makes spells Archipelago items: a spellbook only teaches you its spell once
you've received it, and every new character starts knowing the spells you have (your class's own
starting spells are always yours). Spell Upgrades adds extra copies that make a spell stronger, DLC
Spells adds the DLC classes' spells (taught if you own a DLC), and Spellbook Checks makes reading each
kind of spellbook a check.

Bounty Enemies (optional): on some floors one enemy glows gold. It's tougher, and defeating it gives
extra gold and a bonus item; the first few (Bounty Checks) are checks too.

Champion Floors (Challenges): every few floors a Champion, a much tougher enemy glowing red, guards the
stairs down. With Champion Locks the Stairs you can't go down until it's dead. It drops a good item,
and the first few (Champion Checks) are checks. Boss, town and Sanctum floors never have one.

Progressive Lantern (Extras): the dungeon starts darker (Lantern Darkness sets how much), and three
Progressive Lanterns in the item pool each light it up again, ending brighter than normal. They aren't
inventory items, so you can't lose or drop them.

Death Markers (Lives & Checkpoints, on by default): where you lose a life or die, a gravestone marks it.
Back on that floor number (after the floor restarts, or on a later run) your grave is somewhere on the
floor, usually well away from the start but always somewhere you can walk to. Walk up to it to get back
some of the gold you had (Death Marker Gold), once per grave. Graves you've seen show as a grey cross on
the tracker map.

Merchant Caravan (Extras): sometimes a merchant stops on a floor. He sells normal items like any Barony
shop (prices follow your Trading skill and Charisma), up to two Archipelago items (Caravan Checks), and an
Archipelago Hint that reveals what's at one of your unchecked locations, the ones that matter first. It
doesn't use hint points. He's gone once you take the stairs. He shows as a purple coin on the tracker map.

The Hall of Fame (F5 in game, and shown by itself in the starting area) keeps every run on this seed:
runs and wins, deepest floor, fastest win, fewest lives lost in a win, most kills and gold, and your
last five runs.

Randomize Loot (optional) rerolls the ordinary loot in chests, shops and on the floor: Same Kind keeps
each item's kind (a potion becomes a different potion), Anything lets it become any item.

## What is the goal?

Defeat Baron Herx, the Devil, or the twin liches of the Sanctum, depending on your options.

Custom Dungeon Length lets any goal have a set number of floors (Dungeon Floors: 10 for a short run,
20 for a medium one, 34 for all of Barony's floors, and up to 44 with bonus Caves and Citadel floors
past the end) before the goal boss. Boss floors on the way
down are skipped, and taking the stairs off the last floor takes you to the goal boss.

Boss Rush: play the dungeon (Dungeon Floors, 10 by default), then
take the stairs into the bosses you picked, back to back: Baron Herx, the Devil, then the Twin Liches (or shuffled, with Boss Rush Order).
With Rest Town Between Bosses on, you rest in the town (with its shops) after each boss, and the way
down to the next boss is in the middle of town. Bosses you've already beaten, in any run, stay beaten.

## What items can I receive?

- **Extra Life**: adds a life to your current run.
- **Permanent stat items**, **Max HP Up**, **Starting Gold Bonus**
- **Filler**: XP Boost, Gold Pouch, Healing Potion, Food Ration, Random Item
- **Traps**: Minotaur, Ambush, Polymorph, Fumble, Blindness, Teleport, Hunger, Darkness, Curse

## How do Floor Keys work?

With Floor Keys on, every floor from floor 1 has a key lying somewhere you can walk to. The stairs down
stay locked until you pick it up. It's only part of your own game: never an Archipelago item, so you
never wait on someone else for it. The starting floor, secret areas and boss floors don't need one. Defeating every enemy on the floor also opens the stairs. The key looks like a gold key with the
Archipelago colors in its ring; after 5 minutes on a floor you're shown which way it is.

On a Minotaur floor, once the Minotaur is 30 seconds away (or already there), the screen and the
tracker point you at the key: "Floor Key: 18 tiles NE" (directions as on the map, north up).
Key Compass Time turns it on sooner if you want more help (never in the first 30 seconds).

Floor Key Requirement picks what opens the stairs: the key only (default), every enemy only, or
either one (the key or defeating every enemy).

The Compass (optional) points at the Floor Key from the way you're facing, under the XP bar. You can
start with it, find it as an Archipelago item, or find two Progressive Compasses (the first shows the
way, the second also how far). With Ping and Recharge you use it from your bag or hotbar; it points
for Compass Ping Time seconds and then recharges. With Always On it always points.

## How does the Minotaur Trap work?

It starts the normal Minotaur timer on your current floor, with the usual warning. If a Minotaur
is already coming, or you are on a secret area, boss floor or rest floor, the trap waits for the
next normal floor instead.

## How do lives work?

With lives left, dying costs a life and shows a death screen. Press confirm to restart the floor
you were on, with a new layout or the same one (the Floor Restart option). Your inventory goes back
to what you had when you entered that floor: potions you drank and gear that broke come back, and
items you found on that floor are lost. With Death Items set to Keep Everything, you keep everything
you carry instead, and items you already picked up (and chests you emptied) on that floor stay gone.

Each floor has a fixed number of Archipelago items, decided on your first try. After a restart only
the ones you haven't found yet come back. A game over resets everything.

Death Link deaths also cost a life, with the same Death Penalty (none, gold, XP, or gold and XP).
With Death Link Mode set to Lose a Life, a Death Link only takes the life and the penalty; you keep
playing where you are instead of restarting the floor.

After you lose a life (in place, or when the floor restarts) you're safe for 5 seconds, with a
countdown on screen: you take no damage, enemies lose track of you, and if a boulder or a wall has you
pinned you're moved to the nearest open spot, so one hit can't cost two lives.

The top left of the screen shows whether you're connected and how many Archipelago things are left on
the floor. The top right shows your lives and the total death count. F1 shows received items, F2
the full log, F3 deaths per player. BaronyAPTracker.exe opens next to the game with the same
information.

## Secret areas

On a floor with a secret-area entrance you're given a pickaxe and a levitation potion if you have
nothing to dig or float with, so shuffled floors never lock you out of a secret area.

## Traps

Trap Chance sets how often filler becomes a trap, and each trap has its own weight slider (0 turns it
off), so you can make the Minotaur rarer without turning it off. Polymorph and Blindness last as long
as their duration options say. A trap never goes off between floors or on the death screen; it waits
until you're back in the dungeon.

## How do checkpoints work?

With Checkpoint Interval on, reaching floor 5, 10, 15... (or every 10, 15 or 20 floors) saves your
whole character: level, stats, skills, items, gold and spells. If you die with no lives left, your
next run starts at that checkpoint with everything you had when you reached it.

## DLC

The game asks Steam which DLC packs you own (start Barony from Steam), and turns on the classes and
races of the packs you own, just like the normal game. Skill Checks can be set to any class, DLC
classes included; with Lock Class on, every new character is that class.
