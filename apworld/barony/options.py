from dataclasses import dataclass

from Options import (
    Choice, DeathLink, OptionGroup, OptionSet, PerGameCommonOptions, Range, Toggle, DefaultOnToggle,
)

from .data import MAX_KILL_CHECKS, MAX_LEVEL_CHECK, MAX_PICKUPS, MAX_SHOP_CHECKS, TRAP_ITEMS


# ---------------------------------------------------------------- goal & checks
class Goal(Choice):
    """
    Which boss you need to defeat to finish.
    Herx: Baron Herx on floor 20.
    Devil: the Devil at the bottom of Hell.
    Sanctum: both of the twin liches at the very end of the game (needs the expansion floors).
    """
    display_name = "Goal"
    option_herx = 0
    option_devil = 1
    option_sanctum = 2
    default = option_herx


class FloorCheckInterval(Range):
    """
    How often reaching a new floor gives a check.
    1 = every floor, 2 = every second floor, ... up to 10.
    """
    display_name = "Floor Check Interval"
    range_start = 1
    range_end = 10
    default = 1


class LevelChecks(Range):
    """
    Reaching each character level from 2 up to this number is a check. 0 turns them off.
    """
    display_name = "Character Level Checks"
    range_start = 0
    range_end = MAX_LEVEL_CHECK
    default = 15


class KillChecks(Range):
    """
    Number of kill milestone checks. 0 turns them off.
    Kills are counted across all of your runs, not just the current one.
    """
    display_name = "Kill Milestone Checks"
    range_start = 0
    range_end = MAX_KILL_CHECKS
    default = 10


class KillCheckInterval(Range):
    """
    How many kills between each kill milestone check.
    """
    display_name = "Kills Per Milestone"
    range_start = 1
    range_end = 100
    default = 10


class BestiaryChecks(Choice):
    """
    Killing each type of monster for the first time is a check.
    Early: only monsters from the Mines and the Swamp.
    All: every monster you can meet before your goal.
    """
    display_name = "Bestiary Checks"
    option_off = 0
    option_early = 1
    option_all = 2
    default = option_early


class ArchipelagoPickups(Range):
    """
    Archipelago items placed on the floor of the dungeon. Picking one up sends a check.
    0 turns them off.
    """
    display_name = "Archipelago Pickups"
    range_start = 0
    range_end = MAX_PICKUPS
    default = 20


class PickupsPerFloor(Range):
    """
    How many Archipelago pickups appear on each normal floor (while there are still some left to find).
    """
    display_name = "Pickups Per Floor"
    range_start = 1
    range_end = 5
    default = 2


class ShopChecks(Range):
    """
    Shopkeepers sell Archipelago items. Buying one sends a check. 0 turns them off.
    """
    display_name = "Shop Checks"
    range_start = 0
    range_end = MAX_SHOP_CHECKS
    default = 5


class BossChecks(DefaultOnToggle):
    """
    Defeating a Minotaur and any boss before your goal boss is a check.
    """
    display_name = "Boss Checks"


class SpecialAreaChecks(Toggle):
    """
    Visiting each secret area (Sokoban, the Underworld, etc.) is a check.
    Secret areas don't appear on every run, so these can take a while.
    """
    display_name = "Special Area Checks"


# ---------------------------------------------------------------- progression
class StairKeys(Toggle):
    """
    Adds Progressive Stair Keys. Each key opens the stairs down into the next section of
    5 floors, so you can only go as deep as your keys allow.
    """
    display_name = "Stair Keys"


class FloorShuffle(Choice):
    """
    Shuffles which part of the dungeon each floor comes from.
    Off: normal order.
    Sections: whole biomes stay together but their order is shuffled.
    Full: every normal floor is drawn from any biome.
    Boss floors, rest floors and secret areas always stay where they are.
    """
    display_name = "Floor Shuffle"
    option_off = 0
    option_sections = 1
    option_full = 2
    default = option_off


class EnemyRandomizer(Choice):
    """
    Changes which monsters show up.
    Off: normal monsters for each biome.
    Per Floor: on each floor, every kind of monster is swapped for another kind (all rats become
    spiders, say), different on every floor.
    Every Spawn: every monster is picked at random.
    Bosses, shopkeepers and Minotaurs are never changed. With difficulty scaling on, monsters that
    normally live deeper are weakened to match your floor.
    """
    display_name = "Enemy Randomizer"
    option_off = 0
    option_per_floor = 1
    option_every_spawn = 2
    default = option_off


class EnemyPool(Choice):
    """
    Which monsters the Enemy Randomizer can pick from.
    Near Depth: monsters from up to two biomes deeper than where you are.
    Anywhere: any monster from the whole dungeon, even on floor 1.
    """
    display_name = "Enemy Pool"
    option_near_depth = 0
    option_anywhere = 1
    default = option_near_depth


class MusicShuffle(Choice):
    """
    Changes the music on each floor.
    Off: normal music.
    Biome Music: each floor plays the music of a random biome.
    All Music: also includes the music of special areas, like Minetown, Sokoban and the Temple.
    Boss fights keep their own music.
    """
    display_name = "Music Shuffle"
    option_off = 0
    option_biome_music = 1
    option_all_music = 2
    default = option_off


class DifficultyScaling(DefaultOnToggle):
    """
    Monsters are scaled to how deep you actually are, not to where they normally live.
    A monster from a late biome showing up on floor 2 (from Floor Shuffle or the Enemy
    Randomizer) is weakened so a level 1 character can kill it. Ignored in hard mode.
    """
    display_name = "Difficulty Scaling"


class Difficulty(Choice):
    """
    Normal: regular rules.
    Hard: Barony's own hard mode is switched on, difficulty scaling is turned off,
    trap chance goes up by half, and the Minotaur arrives sooner.
    """
    display_name = "Difficulty"
    option_normal = 0
    option_hard = 1
    default = option_normal


# ---------------------------------------------------------------- lives & death
class Lives(Range):
    """
    Extra lives at the start of every run. When you die with a life left, you lose the life,
    see a death screen, and press confirm to restart the floor (see Floor Restart). You keep your
    character, and your inventory goes back to what you carried when you entered that floor.
    0 = normal permadeath.
    """
    display_name = "Starting Lives"
    range_start = 0
    range_end = 10
    default = 0


class ExtraLivesInPool(Range):
    """
    Number of "Extra Life" items in the item pool. Each one adds a life to your current run.
    """
    display_name = "Extra Lives In Pool"
    range_start = 0
    range_end = 10
    default = 2


class DeathPenalty(Choice):
    """
    What losing a life costs you.
    Gold: lose a quarter of your gold.
    Gold and XP: also lose half of your progress toward the next level.
    You can never lose a level, so a seed always stays beatable.
    """
    display_name = "Death Penalty"
    option_none = 0
    option_gold = 1
    option_gold_and_xp = 2
    default = option_none


class BossDeath(Choice):
    """
    What happens to a boss when you lose a life fighting it.
    Reset: the boss comes back at full health.
    Keep Damage: the boss keeps the damage you already did.
    """
    display_name = "Boss Death"
    option_reset = 0
    option_keep_damage = 1
    default = option_reset


class FloorRestart(Choice):
    """
    When you lose a life, you see a death screen, then press confirm to restart the floor.
    New Layout: the floor is generated fresh, with a different layout.
    Same Layout: you get the exact same floor again.
    """
    display_name = "Floor Restart"
    option_new_layout = 0
    option_same_layout = 1
    default = option_new_layout


class BaronyDeathLink(DeathLink):
    """
    When you die, everyone else with Death Link dies too, and the other way around.
    If you have a life left, a Death Link death costs a life and restarts the floor.
    """


# ---------------------------------------------------------------- traps & players
class TrapChance(Range):
    """
    Percent chance that each filler item is replaced with a trap.
    """
    display_name = "Trap Chance"
    range_start = 0
    range_end = 100
    default = 20


class EnabledTraps(OptionSet):
    """
    Which traps can appear.
    """
    display_name = "Enabled Traps"
    valid_keys = frozenset(TRAP_ITEMS)
    default = frozenset(TRAP_ITEMS)


class Players(Range):
    """
    How many people play this slot together. 2 = co-op (both players share checks and items).
    Co-op is experimental.
    """
    display_name = "Players"
    range_start = 1
    range_end = 2
    default = 1


@dataclass
class BaronyOptions(PerGameCommonOptions):
    goal: Goal
    floor_check_interval: FloorCheckInterval
    level_checks: LevelChecks
    kill_checks: KillChecks
    kill_check_interval: KillCheckInterval
    bestiary_checks: BestiaryChecks
    archipelago_pickups: ArchipelagoPickups
    pickups_per_floor: PickupsPerFloor
    shop_checks: ShopChecks
    boss_checks: BossChecks
    special_area_checks: SpecialAreaChecks
    stair_keys: StairKeys
    floor_shuffle: FloorShuffle
    enemy_randomizer: EnemyRandomizer
    enemy_pool: EnemyPool
    music_shuffle: MusicShuffle
    difficulty_scaling: DifficultyScaling
    difficulty: Difficulty
    lives: Lives
    extra_lives_in_pool: ExtraLivesInPool
    death_penalty: DeathPenalty
    boss_death: BossDeath
    floor_restart: FloorRestart
    death_link: BaronyDeathLink
    trap_chance: TrapChance
    enabled_traps: EnabledTraps
    players: Players


option_groups = [
    OptionGroup("Goal & Checks", [
        Goal, FloorCheckInterval, LevelChecks, KillChecks, KillCheckInterval, BestiaryChecks,
        ArchipelagoPickups, PickupsPerFloor, ShopChecks, BossChecks, SpecialAreaChecks,
    ]),
    OptionGroup("Dungeon", [StairKeys, FloorShuffle, EnemyRandomizer, EnemyPool, MusicShuffle, DifficultyScaling, Difficulty]),
    OptionGroup("Lives & Death", [Lives, ExtraLivesInPool, DeathPenalty, BossDeath, FloorRestart, BaronyDeathLink]),
    OptionGroup("Traps & Players", [TrapChance, EnabledTraps, Players]),
]
