"""
Static data for the Barony APWorld.

Every item and location that can ever exist gets a fixed ID here, whether or not the
current options create it. The C++ game client uses the same numbers (see
tools/gen_cpp_ids.py, which writes client/src/archipelago_ids.hpp from this file),
so never renumber anything once a version has been released.
"""
from __future__ import annotations

from typing import NamedTuple

GAME_NAME = "Barony"
BASE_ID = 4_270_000

# Floors are grouped in "sections" of 5, matching Barony's own LENGTH_OF_LEVEL_REGION.
FLOORS_PER_SECTION = 5

# ---------------------------------------------------------------------------------------
# Goals
# ---------------------------------------------------------------------------------------
GOAL_HERX = 0
GOAL_DEVIL = 1
GOAL_SANCTUM = 2

# The floor each goal boss lives on.
GOAL_FLOOR = {
    GOAL_HERX: 20,
    GOAL_DEVIL: 24,
    GOAL_SANCTUM: 35,
}

MAX_FLOOR = 35


def section_of_floor(floor: int) -> int:
    """Floors 1-5 are section 1, 6-10 section 2, and so on."""
    return max(1, (floor - 1) // FLOORS_PER_SECTION + 1)


# ---------------------------------------------------------------------------------------
# Monsters (bestiary + boss checks)
# ---------------------------------------------------------------------------------------
class MonsterInfo(NamedTuple):
    index: int        # stable index used in the location ID; never change
    name: str         # display name in location names
    enum_name: str    # Barony's C++ enum value (monster.hpp)
    depth: int        # conservative "you will have met it by this floor"
    early: bool       # included by bestiary_checks: early


# Depths come from Barony's default monster spawn table (maps.cpp, monsterCurve) and the
# floor order in maps/levels.txt: the first normal floor of the biome where each monster
# spawns. "early" monsters are the Mines and Swamp ones, safe for every goal.
# Rare spawns (Vampire, Cockatrice) are put at their more common, later biome.
BESTIARY = [
    MonsterInfo(1, "Rat", "RAT", 1, True),
    MonsterInfo(2, "Skeleton", "SKELETON", 1, True),
    MonsterInfo(3, "Spider", "SPIDER", 2, True),
    MonsterInfo(4, "Troll", "TROLL", 2, True),
    MonsterInfo(5, "Goblin", "GOBLIN", 6, True),
    MonsterInfo(6, "Slime", "SLIME", 6, True),
    MonsterInfo(7, "Ghoul", "GHOUL", 6, True),
    MonsterInfo(8, "Scorpion", "SCORPION", 11, False),
    MonsterInfo(9, "Scarab", "SCARAB", 11, False),
    MonsterInfo(10, "Insectoid", "INSECTOID", 11, False),
    MonsterInfo(11, "Gnome", "GNOME", 16, False),
    MonsterInfo(12, "Bugbear", "BUGBEAR", 16, False),
    MonsterInfo(13, "Demon", "DEMON", 16, False),
    MonsterInfo(14, "Imp", "CREATURE_IMP", 21, False),
    MonsterInfo(15, "Succubus", "SUCCUBUS", 21, False),
    MonsterInfo(16, "Incubus", "INCUBUS", 21, False),
    MonsterInfo(17, "Goatman", "GOATMAN", 21, False),
    MonsterInfo(18, "Shadow", "SHADOW", 21, False),
    MonsterInfo(19, "Kobold", "KOBOLD", 26, False),
    MonsterInfo(20, "Automaton", "AUTOMATON", 26, False),
    MonsterInfo(21, "Crystal Golem", "CRYSTALGOLEM", 26, False),
    MonsterInfo(22, "Vampire", "VAMPIRE", 31, False),
    MonsterInfo(23, "Cockatrice", "COCKATRICE", 31, False),
]


class BossInfo(NamedTuple):
    index: int
    location_name: str
    enum_name: str
    floor: int


BOSSES = [
    BossInfo(1, "Defeat a Minotaur", "MINOTAUR", 4),
    BossInfo(2, "Defeat Baron Herx", "LICH", 20),
    BossInfo(3, "Defeat the Devil", "DEVIL", 24),
    BossInfo(4, "Defeat the Ice Lich", "LICH_ICE", 35),
    BossInfo(5, "Defeat the Fire Lich", "LICH_FIRE", 35),
]

# Which boss kills count as each goal. The Sanctum goal needs both twins.
GOAL_BOSSES = {
    GOAL_HERX: ["LICH"],
    GOAL_DEVIL: ["DEVIL"],
    GOAL_SANCTUM: ["LICH_ICE", "LICH_FIRE"],
}


# ---------------------------------------------------------------------------------------
# Special areas (secret levels), from maps/secretlevels.txt. The secret level on line N+1
# is the one you reach at depth N, so "depth" is that line's floor. The client matches a
# map by the start of its file name, or its display name for generated ones (Underworld).
# ---------------------------------------------------------------------------------------
class SpecialArea(NamedTuple):
    index: int
    name: str
    map_match: str
    depth: int


SPECIAL_AREAS = [
    SpecialArea(1, "Visit the Gnomish Mines", "gnomishmines", 3),
    SpecialArea(2, "Visit Minetown", "minetown", 4),
    SpecialArea(3, "Visit the Underworld", "Underworld", 6),
    SpecialArea(4, "Visit the Temple", "temple", 8),
    SpecialArea(5, "Visit the Great Castle", "greatcastle", 9),
    SpecialArea(6, "Visit Sokoban", "sokoban", 12),
    SpecialArea(7, "Visit the Minotaur Maze", "minotaur", 14),
    SpecialArea(8, "Visit the Mystic Library", "mysticlibrary", 17),
    SpecialArea(9, "Visit the Cockatrice Lair", "caveslair", 29),
    SpecialArea(10, "Visit Bram's Castle", "bramscastle", 34),
]


# ---------------------------------------------------------------------------------------
# Location ID layout
# ---------------------------------------------------------------------------------------
FLOOR_OFFSET = 0          # + floor (1..35)
LEVEL_OFFSET = 100        # + character level (2..30)
KILL_OFFSET = 200         # + k (1..50), the k-th kill milestone
BESTIARY_OFFSET = 300     # + MonsterInfo.index
PICKUP_OFFSET = 400       # + i (1..100)
SHOP_OFFSET = 600         # + j (1..20)
BOSS_OFFSET = 700         # + BossInfo.index
SPECIAL_OFFSET = 800      # + SpecialArea.index

MAX_LEVEL_CHECK = 30
MAX_KILL_CHECKS = 50
MAX_PICKUPS = 100
MAX_SHOP_CHECKS = 20


def floor_location(floor: int) -> str:
    return f"Reach Floor {floor}"


def level_location(level: int) -> str:
    return f"Reach Character Level {level}"


def kill_location(k: int) -> str:
    return f"Kill Milestone {k}"


def bestiary_location(m: MonsterInfo) -> str:
    return f"Bestiary: {m.name}"


def pickup_location(i: int) -> str:
    return f"Archipelago Pickup {i}"


def shop_location(j: int) -> str:
    return f"Shop Item {j}"


LOCATION_NAME_TO_ID: dict[str, int] = {}
for _f in range(1, MAX_FLOOR + 1):
    LOCATION_NAME_TO_ID[floor_location(_f)] = BASE_ID + FLOOR_OFFSET + _f
for _l in range(2, MAX_LEVEL_CHECK + 1):
    LOCATION_NAME_TO_ID[level_location(_l)] = BASE_ID + LEVEL_OFFSET + _l
for _k in range(1, MAX_KILL_CHECKS + 1):
    LOCATION_NAME_TO_ID[kill_location(_k)] = BASE_ID + KILL_OFFSET + _k
for _m in BESTIARY:
    LOCATION_NAME_TO_ID[bestiary_location(_m)] = BASE_ID + BESTIARY_OFFSET + _m.index
for _i in range(1, MAX_PICKUPS + 1):
    LOCATION_NAME_TO_ID[pickup_location(_i)] = BASE_ID + PICKUP_OFFSET + _i
for _j in range(1, MAX_SHOP_CHECKS + 1):
    LOCATION_NAME_TO_ID[shop_location(_j)] = BASE_ID + SHOP_OFFSET + _j
for _b in BOSSES:
    LOCATION_NAME_TO_ID[_b.location_name] = BASE_ID + BOSS_OFFSET + _b.index
for _s in SPECIAL_AREAS:
    LOCATION_NAME_TO_ID[_s.name] = BASE_ID + SPECIAL_OFFSET + _s.index


# ---------------------------------------------------------------------------------------
# Items
# ---------------------------------------------------------------------------------------
class ItemInfo(NamedTuple):
    code: int          # offset from BASE_ID
    kind: str          # "progression" | "useful" | "filler" | "trap"
    cpp_name: str      # constant name used in the C++ client


ITEMS: dict[str, ItemInfo] = {
    # progression
    "Progressive Stair Key": ItemInfo(1, "progression", "STAIR_KEY"),

    # useful: permanent, applied to every new run
    "Extra Life": ItemInfo(10, "useful", "EXTRA_LIFE"),
    "Permanent Strength": ItemInfo(11, "useful", "PERM_STR"),
    "Permanent Dexterity": ItemInfo(12, "useful", "PERM_DEX"),
    "Permanent Constitution": ItemInfo(13, "useful", "PERM_CON"),
    "Permanent Intelligence": ItemInfo(14, "useful", "PERM_INT"),
    "Permanent Perception": ItemInfo(15, "useful", "PERM_PER"),
    "Permanent Charisma": ItemInfo(16, "useful", "PERM_CHR"),
    "Max HP Up": ItemInfo(17, "useful", "MAX_HP_UP"),
    "Starting Gold Bonus": ItemInfo(18, "useful", "START_GOLD"),

    # filler: one-shot, applied once when received
    "XP Boost": ItemInfo(30, "filler", "XP_BOOST"),
    "Gold Pouch": ItemInfo(31, "filler", "GOLD_POUCH"),
    "Healing Potion": ItemInfo(32, "filler", "HEAL_POTION"),
    "Food Ration": ItemInfo(33, "filler", "FOOD"),
    "Random Item": ItemInfo(34, "filler", "RANDOM_ITEM"),

    # traps
    "Minotaur Trap": ItemInfo(50, "trap", "TRAP_MINOTAUR"),
    "Ambush Trap": ItemInfo(51, "trap", "TRAP_AMBUSH"),
    "Polymorph Trap": ItemInfo(52, "trap", "TRAP_POLYMORPH"),
    "Fumble Trap": ItemInfo(53, "trap", "TRAP_FUMBLE"),
    "Blindness Trap": ItemInfo(54, "trap", "TRAP_BLIND"),
    "Teleport Trap": ItemInfo(55, "trap", "TRAP_TELEPORT"),
    "Hunger Trap": ItemInfo(56, "trap", "TRAP_HUNGER"),
    "Darkness Trap": ItemInfo(57, "trap", "TRAP_DARKNESS"),
    "Curse Trap": ItemInfo(58, "trap", "TRAP_CURSE"),
}

ITEM_NAME_TO_ID = {name: BASE_ID + info.code for name, info in ITEMS.items()}

PERMANENT_STAT_ITEMS = [
    "Permanent Strength", "Permanent Dexterity", "Permanent Constitution",
    "Permanent Intelligence", "Permanent Perception", "Permanent Charisma",
]

FILLER_WEIGHTS = {
    "XP Boost": 30,
    "Gold Pouch": 25,
    "Healing Potion": 20,
    "Food Ration": 15,
    "Random Item": 10,
}

TRAP_ITEMS = [name for name, info in ITEMS.items() if info.kind == "trap"]
