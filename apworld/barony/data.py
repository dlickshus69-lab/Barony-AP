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
GOAL_BOSS_RUSH = 3
GOAL_ALL_BOSSES = 4

# The floor each goal boss lives on.
GOAL_FLOOR = {
    GOAL_HERX: 20,
    GOAL_DEVIL: 24,
    GOAL_SANCTUM: 35,
    GOAL_BOSS_RUSH: 10,  # the default; Dungeon Floors changes it
    GOAL_ALL_BOSSES: 35,
}

MAX_FLOOR = 45  # Barony's 35, plus up to 10 bonus floors with Custom Dungeon Length


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
    GOAL_BOSS_RUSH: ["LICH", "DEVIL", "LICH_ICE", "LICH_FIRE"],
    GOAL_ALL_BOSSES: ["LICH", "DEVIL", "LICH_ICE", "LICH_FIRE"],
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
KILL_OFFSET = 200         # + k (1..50), the k-th Enemy Sanity check
BESTIARY_OFFSET = 300     # + MonsterInfo.index
PICKUP_OFFSET = 400       # + i (1..100)
SHOP_OFFSET = 600         # + j (1..20)
BOSS_OFFSET = 700         # + BossInfo.index
SPECIAL_OFFSET = 800      # + SpecialArea.index
SKILL_OFFSET = 900        # + skill * 5 + rank (rank 1..5)
SPELLBOOK_OFFSET = 1100   # + Barony spell id: reading that spellbook (Spellbook Checks)
BOUNTY_OFFSET = 1200      # + k (1..30): the k-th bounty enemy defeated
MAX_BOUNTY_CHECKS = 30
CHAMPION_OFFSET = 1300    # + k (1..20): the k-th Champion defeated
MAX_CHAMPION_CHECKS = 20

# ---------------------------------------------------------------------------------------
# Skills (Barony 5): index = PRO_ constant, name as the game shows it
# ---------------------------------------------------------------------------------------
SKILLS = ["Tinkering", "Stealth", "Trading", "Lore", "Thaumaturgy", "Leadership", "Mysticism", "Sorcery",
          "Ranged", "Sword", "Mace", "Axe", "Polearm", "Shield", "Unarmed", "Alchemy"]
SKILL_RANKS = [(1, "Basic", 20), (2, "Skilled", 40), (3, "Expert", 60), (4, "Master", 80), (5, "Legendary", 100)]
# Floor a rank is reasonable to expect by (for logic).
SKILL_RANK_FLOOR = {1: 1, 2: 4, 3: 10, 4: 16, 5: 22}
# Skills every class can raise from nothing: identifying items, buying and selling, sneaking.
UNIVERSAL_SKILLS = [3, 2, 1]
# Every class (Barony's PlayerClasses order) and the skills it starts with, with starting values.
# The first 13 are the base game; the rest need a DLC pack (see DLC_PACK_OF_CLASS).
CLASSES = ['Barbarian', 'Warrior', 'Healer', 'Rogue', 'Wanderer', 'Cleric', 'Merchant', 'Wizard', 'Arcanist', 'Joker', 'Sexton', 'Ninja', 'Monk', 'Conjurer', 'Accursed', 'Mesmer', 'Brewer', 'Machinist', 'Punisher', 'Shaman', 'Hunter', 'Bard', 'Sapper', 'Scion', 'Hermit', 'Paladin']
DLC_PACK_OF_CLASS = {'Conjurer': 1, 'Accursed': 1, 'Mesmer': 1, 'Brewer': 1, 'Machinist': 2, 'Punisher': 2, 'Shaman': 2, 'Hunter': 2, 'Bard': 3, 'Sapper': 3, 'Scion': 3, 'Hermit': 3, 'Paladin': 3}  # 1 Myths and Outcasts, 2 Legends and Pariahs, 3 Deserters and Disciples
CLASS_SKILLS: dict[str, dict[int, int]] = {
    "Barbarian": {13: 25, 11: 50, 10: 25, 14: 20, 15: 10},
    "Warrior": {5: 40, 8: 25, 9: 25, 12: 50, 13: 25, 14: 10},
    "Healer": {4: 50, 6: 25, 12: 25, 15: 30, 3: 10, 13: 10},
    "Rogue": {3: 25, 1: 50, 0: 40, 8: 25, 9: 25, 15: 20},
    "Wanderer": {1: 25, 12: 25, 8: 25, 2: 25, 14: 10, 15: 30},
    "Cleric": {10: 25, 4: 40, 5: 20, 15: 20, 13: 10},
    "Merchant": {11: 25, 5: 20, 3: 50, 2: 50, 15: 10, 0: 10},
    "Wizard": {12: 25, 7: 50, 4: 15, 15: 10, 3: 10},
    "Arcanist": {7: 30, 1: 25, 0: 25, 8: 25},
    "Joker": {0: 25, 2: 25, 5: 20, 6: 25, 7: 10, 15: 10, 8: 20, 1: 10},
    "Sexton": {10: 10, 13: 10, 1: 40, 6: 40, 8: 20, 15: 20},
    "Ninja": {1: 60, 9: 60, 8: 40},
    "Monk": {13: 40, 5: 10, 4: 25, 14: 50, 15: 20},
    "Conjurer": {6: 40, 4: 15, 1: 20, 8: 20, 5: 40, 15: 20},
    "Accursed": {7: 60, 6: 20, 1: 40, 3: 20, 14: 40},
    "Mesmer": {6: 60, 7: 25, 12: 20, 5: 60},
    "Brewer": {11: 10, 14: 25, 2: 10, 3: 10, 5: 25, 15: 50},
    "Machinist": {0: 40, 8: 10, 15: 10, 2: 10},
    "Punisher": {7: 20, 6: 20, 8: 25, 11: 25},
    "Shaman": {6: 40, 14: 10, 15: 10, 1: 10},
    "Hunter": {3: 20, 1: 25, 8: 50, 0: 10},
    "Bard": {9: 25, 3: 50, 6: 15, 5: 15, 1: 15, 2: 15},
    "Sapper": {10: 50, 7: 15, 8: 15, 1: 25, 15: 25},
    "Scion": {7: 40, 6: 40, 4: 40, 15: 10, 3: 10},
    "Hermit": {6: 40, 7: 15, 10: 25, 1: 25, 15: 10},
    "Paladin": {9: 50, 4: 25, 3: 25, 13: 10, 5: 10},
}


def skill_location(skill: int, rank: int) -> str:
    return f"Skill: {SKILL_RANKS[rank - 1][1]} {SKILLS[skill]}"


def skill_checks_for(class_name: str | None, max_rank: int) -> list[tuple[int, int]]:
    """(skill, rank) pairs for a class (None = any class: the universal skills only)."""
    if class_name is None:
        start = {s: 0 for s in UNIVERSAL_SKILLS}
    else:
        # A skill the class barely starts with (under 20, like a Wizard's 15 Thaumaturgy) may need
        # spells or tools the run never gives you, so only its main skills count.
        start = {s: v for s, v in CLASS_SKILLS[class_name].items() if v >= 20}
        for s in UNIVERSAL_SKILLS:
            start.setdefault(s, 0)
    out = []
    for skill in sorted(start):
        for rank, _, value in SKILL_RANKS:
            if rank <= max_rank and value > start[skill]:
                out.append((skill, rank))
    return out


MAX_LEVEL_CHECK = 30
MAX_KILL_CHECKS = 50
MAX_PICKUPS = 100
MAX_SHOP_CHECKS = 20


def floor_location(floor: int) -> str:
    return f"Reach Floor {floor}"


def level_location(level: int) -> str:
    return f"Reach Character Level {level}"


def kill_location(k: int) -> str:
    return f"Enemy Sanity {k}"


def bestiary_location(m: MonsterInfo) -> str:
    return f"Bestiary: {m.name}"


def pickup_location(i: int) -> str:
    return f"Archipelago Pickup {i}"


def shop_location(j: int) -> str:
    return f"Shop Item {j}"


# ---------------------------------------------------------------------------------------
# Spells (Spell Sanity). Ids are Barony's own spell ids; depth is the spellbook's item level
# (the floor it starts turning up on). The DLC ones are class spells whose books never drop.
# ---------------------------------------------------------------------------------------
class SpellInfo(NamedTuple):
    spell_id: int
    name: str
    depth: int
    dlc: bool = False


SPELLS = [
    SpellInfo(1, "Forcebolt", 1), SpellInfo(2, "Magic Missile", 9), SpellInfo(3, "Cold", 6),
    SpellInfo(4, "Fireball", 11), SpellInfo(5, "Lightning", 11), SpellInfo(6, "Remove Curse", 9),
    SpellInfo(7, "Light", 0), SpellInfo(8, "Identify", 6), SpellInfo(9, "Magic Mapping", 9),
    SpellInfo(10, "Sleep", 6), SpellInfo(11, "Confuse", 4), SpellInfo(12, "Slow", 2),
    SpellInfo(13, "Opening", 4), SpellInfo(14, "Locking", 0), SpellInfo(15, "Levitation", 11),
    SpellInfo(16, "Invisibility", 15), SpellInfo(17, "Teleportation", 6), SpellInfo(18, "Healing", 6),
    SpellInfo(19, "Extra Healing", 11), SpellInfo(20, "Cure Ailment", 11), SpellInfo(21, "Dig", 9),
    SpellInfo(24, "Bloodletting", 22), SpellInfo(26, "Reflect Magic", 25), SpellInfo(27, "Spray Acid", 18),
    SpellInfo(32, "Revert Form", 15), SpellInfo(37, "Spray Web", 9), SpellInfo(38, "Poison", 6),
    SpellInfo(39, "Speed", 18), SpellInfo(40, "Fear", 25), SpellInfo(42, "Detect Food", 6),
    SpellInfo(44, "Elemental Focus", 30), SpellInfo(45, "Arcane Mark", 25), SpellInfo(46, "Teleport Other", 28),
    SpellInfo(48, "Troll's Blood", 4), SpellInfo(49, "Salvage", 28), SpellInfo(50, "Flutter", 8),
    SpellInfo(51, "Dash", 8), SpellInfo(52, "Polymorph", 18),
    # DLC class spells
    SpellInfo(22, "Conjure Skeleton", 0, True), SpellInfo(23, "Stone Blood", 0, True),
    SpellInfo(25, "Dominate", 0, True), SpellInfo(28, "Steal Weapon", 0, True),
    SpellInfo(29, "Drain Soul", 0, True), SpellInfo(30, "Vampiric Aura", 0, True),
    SpellInfo(31, "Charm Monster", 0, True), SpellInfo(33, "Rat Form", 0, True),
    SpellInfo(34, "Arthropod Form", 0, True), SpellInfo(35, "Troll Form", 0, True),
    SpellInfo(36, "Imp Form", 0, True), SpellInfo(41, "Power Strike", 0, True),
    SpellInfo(43, "Weakness", 0, True), SpellInfo(47, "Inner Demon", 0, True),
]
SPELL_ITEM_OFFSET = 100   # item code = 100 + spell id


def spell_item_name(spell: SpellInfo) -> str:
    return f"{spell.name} Spell"


def spellbook_location(spell: SpellInfo) -> str:
    return f"Read the {spell.name} Spellbook"


def bounty_location(k: int) -> str:
    return f"Bounty {k}"


CARAVAN_OFFSET = 1400     # + 1..MAX_CARAVAN_CHECKS
MAX_CARAVAN_CHECKS = 20


def caravan_location(k: int) -> str:
    return f"Caravan Purchase {k}"


# Boss, town and Sanctum floors: no Champion there.
NO_CHAMPION_FLOORS = {20, 24, 25, 35}


def champion_location(k: int) -> str:
    return f"Champion {k}"


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
for _sk in range(len(SKILLS)):
    for _r, _, _ in SKILL_RANKS:
        LOCATION_NAME_TO_ID[skill_location(_sk, _r)] = BASE_ID + SKILL_OFFSET + _sk * 5 + _r
for _sp in SPELLS:
    if not _sp.dlc:
        LOCATION_NAME_TO_ID[spellbook_location(_sp)] = BASE_ID + SPELLBOOK_OFFSET + _sp.spell_id
for _bk in range(1, MAX_BOUNTY_CHECKS + 1):
    LOCATION_NAME_TO_ID[bounty_location(_bk)] = BASE_ID + BOUNTY_OFFSET + _bk
for _ck in range(1, MAX_CHAMPION_CHECKS + 1):
    LOCATION_NAME_TO_ID[champion_location(_ck)] = BASE_ID + CHAMPION_OFFSET + _ck
for _cv in range(1, MAX_CARAVAN_CHECKS + 1):
    LOCATION_NAME_TO_ID[caravan_location(_cv)] = BASE_ID + CARAVAN_OFFSET + _cv


# ---------------------------------------------------------------------------------------
# Items
# ---------------------------------------------------------------------------------------
class ItemInfo(NamedTuple):
    code: int          # offset from BASE_ID
    kind: str          # "progression" | "useful" | "filler" | "trap"
    cpp_name: str      # constant name used in the C++ client


ITEMS: dict[str, ItemInfo] = {
    # progression
    "Progressive Stair Key": ItemInfo(1, "progression", "STAIR_KEY"),  # no longer placed (Floor Keys replaced it)

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
    "Progressive Compass": ItemInfo(20, "useful", "COMPASS"),
    "Progressive Lantern": ItemInfo(21, "useful", "LANTERN"),  # Progressive Lantern: more light  # Compass option: points at the Floor Key

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

for _sp in SPELLS:
    ITEMS[spell_item_name(_sp)] = ItemInfo(SPELL_ITEM_OFFSET + _sp.spell_id, "useful",
                                           "SPELL_" + "".join(c if c.isalnum() else "_" for c in _sp.name.upper()))

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
