from dataclasses import dataclass

from Options import (
    Choice, DeathLink, OptionGroup, OptionSet, PerGameCommonOptions, Range, Toggle, DefaultOnToggle,
)

from .data import MAX_KILL_CHECKS, MAX_LEVEL_CHECK, MAX_PICKUPS, MAX_SHOP_CHECKS


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
    Reaching each character level from 2 up to this number is a check (a new character counts its
    levels again, but each level only sends its check once). 0 turns them off. Higher levels take much
    longer to reach, so very high numbers put checks late in the game.
    """
    display_name = "Character Level Checks"
    range_start = 0
    range_end = MAX_LEVEL_CHECK
    default = 15


class EnemySanity(Choice):
    """
    Killing enemies sends checks. Random enemies on each floor glow green (see Marked Enemies Per
    Floor); only green ones count. Bosses, shopkeepers and the Minotaur are never marked.
    Off: no enemy checks.
    Kill Count: every few green kills is a check (see Kills Per Check). Green kills count across all
    your runs.
    Marked Enemies: every green kill is a check.
    """
    display_name = "Enemy Sanity"
    option_off = 0
    option_kill_count = 1
    option_marked_enemies = 2
    default = option_kill_count


class EnemySanityChecks(Range):
    """
    How many Enemy Sanity checks there are in total (ignored when Enemy Sanity is Off).
    Kill Count: a check is sent every Kills Per Check green kills, so 10 checks with 3 kills per check
    means 30 green kills for all of them.
    Marked Enemies: each green kill sends the next check until all of them are found; after that no
    more enemies are marked. With 2 green enemies per floor, that's about half as many floors as checks.
    """
    display_name = "Enemy Sanity Checks"
    range_start = 1
    range_end = MAX_KILL_CHECKS
    default = 10


class EnemySanityKillsPerCheck(Range):
    """
    For Kill Count: how many green (marked) enemies you kill for each check. Green kills add up across
    all your runs, so dying doesn't lose progress. With 2 green enemies per floor, 3 kills per check is
    a check about every one and a half floors. Ignored for Marked Enemies.
    """
    display_name = "Kills Per Check"
    range_start = 1
    range_end = 100
    default = 3


class EnemySanityPerFloor(Range):
    """
    How many random enemies on each floor are marked green. A normal floor starts with
    about 5 to 8 enemies, so higher numbers mark most of them.
    """
    display_name = "Marked Enemies Per Floor"
    range_start = 1
    range_end = 5
    default = 2


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
    Archipelago items found in the dungeon: lying on the floor, inside some chests, and
    sometimes dropped by crates, pots and barrels you break. Picking one up sends a check.
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
    How many Archipelago items shopkeepers sell in total. Every shopkeeper you find stocks one (shown
    with the Archipelago logo) until all of them are bought; buying one sends a check. If you leave one
    in a shop, the next shopkeeper will have it. 0 turns them off.
    """
    display_name = "Shop Checks"
    range_start = 0
    range_end = MAX_SHOP_CHECKS
    default = 5


class BossChecks(DefaultOnToggle):
    """
    Defeating a Minotaur, and each boss you fight before your goal boss (like Baron Herx when the goal
    is the Devil), is a check. The goal boss itself only completes the goal.
    """
    display_name = "Boss Checks"


class SpecialAreaChecks(Toggle):
    """
    Visiting each secret area (Sokoban, the Underworld, etc.) is a check.
    Secret areas don't appear on every run, so these can take a while.
    """
    display_name = "Special Area Checks"


# ---------------------------------------------------------------- progression
class FloorKeys(DefaultOnToggle):
    """
    Every floor from floor 1 on has a Floor Key hidden somewhere on it, and the stairs down stay locked
    until you find it. It's part of your own game only: it is never an Archipelago item, nobody can send
    it to you, and you never wait on another game for it. The starting floor, secret areas and boss
    floors don't have one. If you die and the floor restarts, a key you already found stays found.
    On Minotaur floors you're shown how far the key is and which way, from the time set by Key Compass
    Time.
    """
    display_name = "Floor Keys"


class KeyCompassTime(Range):
    """
    On Minotaur floors (with Floor Keys on), how many seconds before the Minotaur arrives the screen starts
    pointing you at the Floor Key ("Floor Key: 18 tiles NE"). 30 is the least help. Higher numbers turn
    it on sooner, but never in the first 30 seconds after his timer starts. The normal wait is 150 to
    210 seconds (changed by Minotaur Time and hard mode), so 180 or more means "30 seconds in" on most
    floors.
    """
    display_name = "Key Compass Time"
    range_start = 30
    range_end = 600
    default = 30

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
    Per Floor: on each floor, every kind of monster is swapped for a random kind (all rats become
    spiders, say). A kind can roll itself, so some stay the same.
    Every Spawn: every monster is picked at random.
    See Enemy Seed for whether this changes when you die.
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


class EnemySeed(Choice):
    """
    For the Enemy Randomizer: are a floor's enemies the same every time?
    Locked: each floor's enemies are set by the seed, so dying, restarting the floor or starting a
    new run gives you the same enemies on that floor again.
    Reroll: every time a floor loads, including after you lose a life, its enemies are rolled again.
    """
    display_name = "Enemy Seed"
    option_locked = 0
    option_reroll = 1
    default = option_locked


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


class MusicSeed(Choice):
    """
    For Music Shuffle.
    Locked: each floor always plays the same music, set by the seed.
    Always Random: every time a song starts, it's a random one.
    """
    display_name = "Music Seed"
    option_locked = 0
    option_always_random = 1
    default = option_locked


class CheckpointInterval(Choice):
    """
    Checkpoints save your whole character (level, stats, skills, items, gold and spells) when you
    reach them. If you die with no lives left, your next run starts at your last checkpoint with
    everything you had when you got there.
    Off: no checkpoints, a new run starts on floor 1.
    Every 5 / 10 / 15 / 20 Floors: a checkpoint on floor 5, 10, 15... (or 10, 20, 30...).
    """
    display_name = "Checkpoint Interval"
    option_off = 0
    option_every_5_floors = 5
    option_every_10_floors = 10
    option_every_15_floors = 15
    option_every_20_floors = 20
    default = option_every_5_floors


class LayoutSeed(Choice):
    """
    Whether each floor's map is part of the seed.
    Locked: floor 7 is always the same map whenever you play this seed, run after run.
    New Each Run: every run builds new maps.
    Floor Restart still decides what you get after losing a life.
    """
    display_name = "Layout Seed"
    option_locked = 0
    option_new_each_run = 1
    default = option_locked


class EnemyLevelScaling(Choice):
    """
    Enemies scale to your character level.
    Off: enemies keep their normal strength.
    Stronger Only: enemies below your level are made stronger to match you; tougher ones stay as they are.
    Match Level: every enemy is set to your level, stronger or weaker.
    Bosses, shopkeepers and the Minotaur aren't changed.
    """
    display_name = "Enemy Level Scaling"
    option_off = 0
    option_stronger_only = 1
    option_match_level = 2
    default = option_off


class BossLevelScaling(Choice):
    """
    Bosses scale to your character level, the same way Enemy Level Scaling does for enemies.
    Off: bosses keep their normal strength.
    Stronger Only: bosses below your level are made stronger to match you.
    Match Level: bosses are set to your level, stronger or weaker.
    """
    display_name = "Boss Level Scaling"
    option_off = 0
    option_stronger_only = 1
    option_match_level = 2
    default = option_off


class MinotaurTime(Range):
    """
    How long before the Minotaur arrives on a Minotaur floor (or after a Minotaur Trap), as a percent of
    the normal time. 100 = normal (2.5 to 3.5 minutes), 200 = twice as long. Hard mode still brings him
    a third sooner. The time left is shown on screen.
    """
    display_name = "Minotaur Time"
    range_start = 25
    range_end = 300
    default = 100


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
    How many "Extra Life" items are in the item pool. Each one adds a life to your current run when
    you receive it (they don't carry over to a new run after a game over). Only matters with Lives on.
    """
    display_name = "Extra Lives In Pool"
    range_start = 0
    range_end = 10
    default = 2


class DeathPenalty(Choice):
    """
    What losing a life costs you.
    None: you only lose the life.
    Gold: lose a quarter of your gold.
    XP: lose half of your progress toward the next level.
    Gold and XP: both.
    Lives lost to Death Link cost the same. You can never lose a level, so a seed always stays beatable.
    """
    display_name = "Death Penalty"
    option_none = 0
    option_gold = 1
    option_xp = 3
    option_gold_and_xp = 2
    default = option_none

    @classmethod
    def get_option_name(cls, value) -> str:
        return {0: "None", 1: "Gold", 3: "XP", 2: "Gold and XP"}.get(value, super().get_option_name(value))


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


class DeathItems(Choice):
    """
    What you keep when you lose a life and the floor restarts.
    Floor Start: you go back to the items you had when you entered the floor. Anything you found on
    that floor is lost (and can be found again).
    Keep Everything: you keep everything you're carrying. Items you already picked up and chests you
    already emptied on that floor stay gone after the restart.
    Either way, Archipelago items you already found on the floor don't come back, and a game over
    sends you back to the start (or your checkpoint) with what you had there.
    """
    display_name = "Death Items"
    option_floor_start = 0
    option_keep_everything = 1
    default = option_floor_start


class BaronyDeathLink(DeathLink):
    """
    When you die, everyone else with Death Link dies too, and the other way around.
    What a Death Link does to you is set by Death Link Mode.
    """


class DeathLinkMode(Choice):
    """
    What happens when someone else's death reaches you through Death Link (with a life left).
    Restart Floor: you die, see the death screen, and restart the floor, like any other death.
    Lose a Life: you just lose a life (and pay the Death Penalty) and keep playing where you are.
    Either way the death counter counts it for the player who died, and with no lives left it's a real
    death.
    """
    display_name = "Death Link Mode"
    option_restart_floor = 0
    option_lose_a_life = 1
    default = option_restart_floor


# ---------------------------------------------------------------- skills
class SkillChecks(Choice):
    """
    Raising your skills to Basic (20), Skilled (40), Expert (60), Master (80) and Legendary (100) sends
    checks. Barony classes start with different skills, so pick the class you'll play: its checks are
    its main skills (the ones it starts with at 20 or more), plus Lore, Trading and Stealth (every class
    can raise those). Ranks you already start at or above aren't checks.
    Any Class: only Lore, Trading and Stealth, so it works whichever class you pick in game.
    Playing a different class than the one chosen still counts any skill you raise, but some checks
    may be much harder to get; the game warns you when your run starts.
    """
    display_name = "Skill Checks"
    option_off = 0
    option_any_class = 1
    option_barbarian = 2
    option_warrior = 3
    option_healer = 4
    option_rogue = 5
    option_wanderer = 6
    option_cleric = 7
    option_merchant = 8
    option_wizard = 9
    option_arcanist = 10
    option_joker = 11
    option_sexton = 12
    option_ninja = 13
    option_monk = 14
    default = option_off


class SkillCheckMaxRank(Choice):
    """
    The highest skill rank that's a check. Higher ranks take a long time: Master and Legendary usually
    need most of a run spent using that skill.
    """
    display_name = "Skill Check Max Rank"
    option_basic = 1
    option_skilled = 2
    option_expert = 3
    option_master = 4
    option_legendary = 5
    default = option_expert


# ---------------------------------------------------------------- traps & players
class TrapChance(Range):
    """
    Percent chance that each filler item is replaced with a trap. 0 = no traps, 100 = every filler item
    is a trap. Which trap it is comes from the trap weights below. Hard mode makes traps half again as
    likely.
    """
    display_name = "Trap Chance"
    range_start = 0
    range_end = 100
    default = 20


class TrapWeight(Range):
    range_start = 0
    range_end = 100
    default = 10


class MinotaurTrapWeight(TrapWeight):
    """
    How often a trap is a Minotaur Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    The Minotaur comes for you, with the usual warning and countdown (on the next normal floor if one is already coming).
    """
    display_name = "Minotaur Trap Weight"


class AmbushTrapWeight(TrapWeight):
    """
    How often a trap is a Ambush Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    Two or three monsters from your floor appear around you.
    """
    display_name = "Ambush Trap Weight"


class PolymorphTrapWeight(TrapWeight):
    """
    How often a trap is a Polymorph Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    You turn into a random creature for a while (see Polymorph Trap Duration).
    """
    display_name = "Polymorph Trap Weight"


class FumbleTrapWeight(TrapWeight):
    """
    How often a trap is a Fumble Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    You drop your weapon.
    """
    display_name = "Fumble Trap Weight"


class BlindnessTrapWeight(TrapWeight):
    """
    How often a trap is a Blindness Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    You can't see for a while (see Blindness Trap Duration).
    """
    display_name = "Blindness Trap Weight"


class TeleportTrapWeight(TrapWeight):
    """
    How often a trap is a Teleport Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    You're sent to a random spot on the floor.
    """
    display_name = "Teleport Trap Weight"


class HungerTrapWeight(TrapWeight):
    """
    How often a trap is a Hunger Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    You become very hungry.
    """
    display_name = "Hunger Trap Weight"


class DarknessTrapWeight(TrapWeight):
    """
    How often a trap is a Darkness Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    The next floor is dark.
    """
    display_name = "Darkness Trap Weight"


class CurseTrapWeight(TrapWeight):
    """
    How often a trap is a Curse Trap, compared to the other trap weights: a trap's chance is its weight
    divided by all the trap weights added up (all at 10 = each 1 in 9). 0 = never.
    One piece of your gear becomes cursed.
    """
    display_name = "Curse Trap Weight"


class FillerWeight(Range):
    range_start = 0
    range_end = 100


class XPBoostWeight(FillerWeight):
    """
    How often XP Boost shows up as filler, compared to the other filler weights: an item's chance is
    its weight divided by all the weights added up (30 out of 100 = 30% of filler). 0 = never. If every
    weight is 0, the default weights are used. XP Boost gives 50 experience (half a level).
    """
    display_name = "XP Boost Weight"
    default = 30


class GoldPouchWeight(FillerWeight):
    """
    How often Gold Pouch shows up as filler, compared to the other filler weights. 0 = never.
    Gold Pouch gives 25 gold plus 10 per floor deep you are.
    """
    display_name = "Gold Pouch Weight"
    default = 25


class HealingPotionWeight(FillerWeight):
    """
    How often Healing Potion shows up as filler, compared to the other filler weights. 0 = never.
    Healing Potion puts a potion of healing in your inventory.
    """
    display_name = "Healing Potion Weight"
    default = 20


class FoodRationWeight(FillerWeight):
    """
    How often Food Ration shows up as filler, compared to the other filler weights. 0 = never.
    Food Ration puts a loaf of bread in your inventory.
    """
    display_name = "Food Ration Weight"
    default = 15


class RandomItemWeight(FillerWeight):
    """
    How often Random Item shows up as filler, compared to the other filler weights. 0 = never.
    Random Item gives you a random item suited to your current floor.
    """
    display_name = "Random Item Weight"
    default = 10


class PolymorphTrapDuration(Range):
    """
    How many seconds a Polymorph Trap lasts before you change back.
    """
    display_name = "Polymorph Trap Duration"
    range_start = 5
    range_end = 300
    default = 30


class BlindnessTrapDuration(Range):
    """
    How many seconds a Blindness Trap lasts.
    """
    display_name = "Blindness Trap Duration"
    range_start = 5
    range_end = 300
    default = 15


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
    enemy_sanity: EnemySanity
    enemy_sanity_checks: EnemySanityChecks
    enemy_sanity_kills_per_check: EnemySanityKillsPerCheck
    enemy_sanity_per_floor: EnemySanityPerFloor
    bestiary_checks: BestiaryChecks
    archipelago_pickups: ArchipelagoPickups
    pickups_per_floor: PickupsPerFloor
    shop_checks: ShopChecks
    boss_checks: BossChecks
    special_area_checks: SpecialAreaChecks
    floor_keys: FloorKeys
    skill_checks: SkillChecks
    skill_check_max_rank: SkillCheckMaxRank
    key_compass_time: KeyCompassTime
    floor_shuffle: FloorShuffle
    layout_seed: LayoutSeed
    enemy_randomizer: EnemyRandomizer
    enemy_pool: EnemyPool
    enemy_seed: EnemySeed
    music_shuffle: MusicShuffle
    music_seed: MusicSeed
    enemy_level_scaling: EnemyLevelScaling
    boss_level_scaling: BossLevelScaling
    minotaur_time: MinotaurTime
    difficulty_scaling: DifficultyScaling
    difficulty: Difficulty
    lives: Lives
    extra_lives_in_pool: ExtraLivesInPool
    death_penalty: DeathPenalty
    boss_death: BossDeath
    floor_restart: FloorRestart
    death_items: DeathItems
    checkpoint_interval: CheckpointInterval
    death_link: BaronyDeathLink
    death_link_mode: DeathLinkMode
    trap_chance: TrapChance
    minotaur_trap_weight: MinotaurTrapWeight
    ambush_trap_weight: AmbushTrapWeight
    polymorph_trap_weight: PolymorphTrapWeight
    fumble_trap_weight: FumbleTrapWeight
    blindness_trap_weight: BlindnessTrapWeight
    teleport_trap_weight: TeleportTrapWeight
    hunger_trap_weight: HungerTrapWeight
    darkness_trap_weight: DarknessTrapWeight
    curse_trap_weight: CurseTrapWeight
    polymorph_trap_duration: PolymorphTrapDuration
    blindness_trap_duration: BlindnessTrapDuration
    xp_boost_weight: XPBoostWeight
    gold_pouch_weight: GoldPouchWeight
    healing_potion_weight: HealingPotionWeight
    food_ration_weight: FoodRationWeight
    random_item_weight: RandomItemWeight
    players: Players


option_groups = [
    OptionGroup("Goal & Checks", [
        Goal, FloorCheckInterval, LevelChecks, EnemySanity, EnemySanityChecks, EnemySanityKillsPerCheck,
        EnemySanityPerFloor, BestiaryChecks,
        ArchipelagoPickups, PickupsPerFloor, ShopChecks, BossChecks, SpecialAreaChecks,
    ]),
    OptionGroup("Skills", [SkillChecks, SkillCheckMaxRank]),
    OptionGroup("Dungeon", [FloorKeys, KeyCompassTime, FloorShuffle, LayoutSeed, EnemyRandomizer, EnemyPool, EnemySeed, MusicShuffle, MusicSeed, EnemyLevelScaling, BossLevelScaling, MinotaurTime, DifficultyScaling, Difficulty]),
    OptionGroup("Lives & Checkpoints", [Lives, ExtraLivesInPool, CheckpointInterval]),
    OptionGroup("Death Link", [BaronyDeathLink, DeathLinkMode, DeathPenalty, DeathItems, FloorRestart, BossDeath]),
    OptionGroup("Traps", [TrapChance, MinotaurTrapWeight, AmbushTrapWeight, PolymorphTrapWeight, FumbleTrapWeight, BlindnessTrapWeight, TeleportTrapWeight, HungerTrapWeight, DarknessTrapWeight, CurseTrapWeight,
                          PolymorphTrapDuration, BlindnessTrapDuration]),
    OptionGroup("Filler", [XPBoostWeight, GoldPouchWeight, HealingPotionWeight, FoodRationWeight, RandomItemWeight]),
    OptionGroup("Players", [Players]),
]
