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

Everything you receive is kept across runs. Permanent items (stat boosts, Max HP Up, Starting
Gold Bonus) are applied to every new character you start.

## What is the goal?

Defeat Baron Herx, the Devil, or the twin liches of the Sanctum, depending on your options.

## What items can I receive?

- **Progressive Stair Key** (optional): each one opens the next 5-floor section of the dungeon.
- **Extra Life**: adds a life to your current run.
- **Permanent stat items**, **Max HP Up**, **Starting Gold Bonus**
- **Filler**: XP Boost, Gold Pouch, Healing Potion, Food Ration, Random Item
- **Traps**: Minotaur, Ambush, Polymorph, Fumble, Blindness, Teleport, Hunger, Darkness, Curse

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
