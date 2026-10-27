from __future__ import annotations

import logging
from collections.abc import Mapping
from typing import Any

from BaseClasses import Item, ItemClassification, Location, Region, Tutorial
from worlds.AutoWorld import WebWorld, World

from . import data
from .options import BaronyOptions, option_groups

logger = logging.getLogger("Barony")

# The highest character level a normal run can be expected to reach before each goal.
# Level checks above this are clamped, because XP doesn't carry between runs.
MAX_SENSIBLE_LEVEL = {
    data.GOAL_HERX: 18,
    data.GOAL_DEVIL: 22,
    data.GOAL_SANCTUM: 30,
}

CLASSIFICATION = {
    "progression": ItemClassification.progression,
    "useful": ItemClassification.useful,
    "filler": ItemClassification.filler,
    "trap": ItemClassification.trap,
}


class BaronyItem(Item):
    game = data.GAME_NAME


class BaronyLocation(Location):
    game = data.GAME_NAME


class BaronyWeb(WebWorld):
    theme = "stone"
    option_groups = option_groups
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "How to build the Archipelago version of Barony and connect it to a multiworld.",
        "English",
        "setup_en.md",
        "setup/en",
        ["Barony AP contributors"],
    )]


class BaronyWorld(World):
    """
    Barony is a first-person roguelike dungeon crawler. Descend through the Mines, the Swamp,
    the Labyrinth, the Ruins and beyond to stop the lich Baron Herx. In this randomizer, floors,
    enemies, levels and hidden Archipelago pickups send checks, and anything you receive stays
    with you across runs.
    """

    game = data.GAME_NAME
    web = BaronyWeb()
    options_dataclass = BaronyOptions
    options: BaronyOptions

    item_name_to_id = data.ITEM_NAME_TO_ID
    location_name_to_id = data.LOCATION_NAME_TO_ID
    origin_region_name = "Hamlet"

    item_name_groups = {
        "Traps": set(data.TRAP_ITEMS),
        "Permanent Stats": set(data.PERMANENT_STAT_ITEMS),
    }

    # Filled in generate_early
    goal_floor: int
    num_sections: int
    floor_shuffle_seed: int
    level_check_max: int
    pickup_count: int
    shop_count: int

    # ------------------------------------------------------------------ setup
    def generate_early(self) -> None:
        goal = self.options.goal.value
        self.goal_floor = data.GOAL_FLOOR[goal]
        self.num_sections = data.section_of_floor(self.goal_floor)
        self.floor_shuffle_seed = self.random.randint(1, 2**31 - 1)

        self.level_check_max = self.options.level_checks.value
        cap = MAX_SENSIBLE_LEVEL[goal]
        if self.level_check_max > cap:
            logger.warning(
                f"Barony ({self.player_name}): level_checks {self.level_check_max} is higher than a run "
                f"usually reaches before this goal; lowering to {cap}."
            )
            self.level_check_max = cap

        ppf = self.options.pickups_per_floor.value
        max_pickups = ppf * (self.goal_floor - 1)
        self.pickup_count = min(self.options.archipelago_pickups.value, max_pickups)

        # Roughly half of floors have a shop; expect at most one AP shop item per 2 floors.
        self.shop_count = min(self.options.shop_checks.value, (self.goal_floor - 1) // 2)

    # ------------------------------------------------------------------ helpers
    def _floor_needed(self, floor: int) -> int:
        """Clamp a 'needs floor X' requirement to the floors that exist before the goal."""
        return max(1, min(floor, self.goal_floor))

    def _biome_floor_needed(self, natural_floor: int) -> int:
        """
        For things tied to a biome (secret areas). With floor shuffle on, the biome could land
        anywhere, so logic expects them only once the whole dungeon is open.
        """
        if self.options.floor_shuffle.value != self.options.floor_shuffle.option_off:
            return self.goal_floor
        return self._floor_needed(natural_floor)

    def _monster_floor_needed(self, natural_floor: int) -> int:
        """Bestiary: with the Enemy Randomizer on, a monster type can turn up anywhere or nowhere."""
        if self.options.enemy_randomizer.value != self.options.enemy_randomizer.option_off:
            return self.goal_floor
        return self._biome_floor_needed(natural_floor)

    def _planned_locations(self) -> list[tuple[str, int]]:
        """Every location this slot has, paired with the floor needed to reach it."""
        planned: list[tuple[str, int]] = []
        o = self.options

        interval = o.floor_check_interval.value
        for floor in range(1, self.goal_floor + 1):
            if floor % interval == 0:
                planned.append((data.floor_location(floor), floor))

        for level in range(2, self.level_check_max + 1):
            planned.append((data.level_location(level), self._floor_needed(level)))

        if o.enemy_sanity.value == o.enemy_sanity.option_kill_count:
            for k in range(1, o.enemy_sanity_checks.value + 1):
                planned.append((data.kill_location(k), 1))
        elif o.enemy_sanity.value == o.enemy_sanity.option_marked_enemies:
            per_floor = o.enemy_sanity_per_floor.value
            for k in range(1, o.enemy_sanity_checks.value + 1):
                planned.append((data.kill_location(k), self._floor_needed((k + per_floor - 1) // per_floor)))

        if o.bestiary_checks.value != o.bestiary_checks.option_off:
            include_all = o.bestiary_checks.value == o.bestiary_checks.option_all
            for m in data.BESTIARY:
                if (m.early or include_all) and m.depth < self.goal_floor:
                    planned.append((data.bestiary_location(m), self._monster_floor_needed(m.depth)))

        ppf = o.pickups_per_floor.value
        for i in range(1, self.pickup_count + 1):
            planned.append((data.pickup_location(i), self._floor_needed((i + ppf - 1) // ppf)))

        for j in range(1, self.shop_count + 1):
            planned.append((data.shop_location(j), self._floor_needed(2 * j)))

        if o.boss_checks:
            goal_bosses = data.GOAL_BOSSES[o.goal.value]
            for boss in data.BOSSES:
                if boss.enum_name in goal_bosses or boss.floor > self.goal_floor:
                    continue
                planned.append((boss.location_name, self._floor_needed(boss.floor)))

        if o.special_area_checks:
            for area in data.SPECIAL_AREAS:
                if area.depth < self.goal_floor:
                    planned.append((area.name, self._biome_floor_needed(area.depth)))

        sc = o.skill_checks.value
        if sc != o.skill_checks.option_off:
            class_name = None if sc == o.skill_checks.option_any_class else data.CLASSES[sc - 2]
            for skill, rank in data.skill_checks_for(class_name, o.skill_check_max_rank.value):
                planned.append((data.skill_location(skill, rank),
                                self._floor_needed(min(data.SKILL_RANK_FLOOR[rank], self.goal_floor))))

        return planned

    # ------------------------------------------------------------------ regions
    def create_regions(self) -> None:
        hamlet = Region("Hamlet", self.player, self.multiworld)
        self.multiworld.regions.append(hamlet)

        sections: list[Region] = []
        for s in range(1, self.num_sections + 1):
            first = (s - 1) * data.FLOORS_PER_SECTION + 1
            last = min(s * data.FLOORS_PER_SECTION, self.goal_floor)
            region = Region(f"Floors {first}-{last}", self.player, self.multiworld)
            self.multiworld.regions.append(region)
            sections.append(region)

        # Floor Keys are found on each floor in your own game, so the stairs need nothing from the
        # multiworld.
        hamlet.connect(sections[0], "Enter the Dungeon")
        for s in range(1, len(sections)):
            sections[s - 1].connect(sections[s], f"Stairs to section {s + 1}")

        for name, floor in self._planned_locations():
            region = sections[data.section_of_floor(floor) - 1]
            region.locations.append(BaronyLocation(self.player, name, self.location_name_to_id[name], region))

        # Victory event on the goal floor
        final = sections[-1]
        victory = BaronyLocation(self.player, "Goal Boss Defeated", None, final)
        victory.place_locked_item(BaronyItem("Victory", ItemClassification.progression, None, self.player))
        final.locations.append(victory)

    def set_rules(self) -> None:
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Victory", self.player)

    # ------------------------------------------------------------------ items
    def create_item(self, name: str) -> BaronyItem:
        info = data.ITEMS[name]
        return BaronyItem(name, CLASSIFICATION[info.kind], self.item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        chance = self.options.trap_chance.value
        if self.options.difficulty.value == self.options.difficulty.option_hard:
            chance = min(100, chance * 3 // 2)
        o = self.options
        trap_weights = {
            "Minotaur Trap": o.minotaur_trap_weight.value,
            "Ambush Trap": o.ambush_trap_weight.value,
            "Polymorph Trap": o.polymorph_trap_weight.value,
            "Fumble Trap": o.fumble_trap_weight.value,
            "Blindness Trap": o.blindness_trap_weight.value,
            "Teleport Trap": o.teleport_trap_weight.value,
            "Hunger Trap": o.hunger_trap_weight.value,
            "Darkness Trap": o.darkness_trap_weight.value,
            "Curse Trap": o.curse_trap_weight.value,
        }
        if sum(trap_weights.values()) > 0 and self.random.randint(1, 100) <= chance:
            names = list(trap_weights)
            return self.random.choices(names, weights=[trap_weights[n] for n in names])[0]
        weights = {
            "XP Boost": o.xp_boost_weight.value,
            "Gold Pouch": o.gold_pouch_weight.value,
            "Healing Potion": o.healing_potion_weight.value,
            "Food Ration": o.food_ration_weight.value,
            "Random Item": o.random_item_weight.value,
        }
        if sum(weights.values()) == 0:
            weights = dict(data.FILLER_WEIGHTS)
        names = list(weights)
        return self.random.choices(names, weights=[weights[n] for n in names])[0]

    def create_items(self) -> None:
        total = len(self.multiworld.get_unfilled_locations(self.player))

        required: list[str] = []
        if self.options.floor_keys and self.options.compass.value == self.options.compass.option_archipelago_item:
            required += ["Progressive Compass"]
        elif self.options.floor_keys and self.options.compass.value == self.options.compass.option_progressive:
            required += ["Progressive Compass"] * 2

        useful: list[str] = ["Extra Life"] * self.options.extra_lives_in_pool.value
        useful += data.PERMANENT_STAT_ITEMS
        useful += ["Max HP Up"] * 2
        useful += ["Starting Gold Bonus"]

        room = total - len(required)
        if len(useful) > room:
            self.random.shuffle(useful)
            # Keep extra lives first if we have to trim
            useful.sort(key=lambda n: n != "Extra Life")
            useful = useful[:max(0, room)]

        pool = [self.create_item(n) for n in required + useful]
        pool += [self.create_filler() for _ in range(total - len(pool))]
        self.multiworld.itempool += pool

    # ------------------------------------------------------------------ client data
    def fill_slot_data(self) -> Mapping[str, Any]:
        o = self.options
        return {
            "goal": o.goal.value,
            "goal_floor": self.goal_floor,
            "floor_check_interval": o.floor_check_interval.value,
            "level_checks": self.level_check_max,
            "kill_checks": 0 if o.enemy_sanity.value == o.enemy_sanity.option_off else o.enemy_sanity_checks.value,
            "kill_check_interval": o.enemy_sanity_kills_per_check.value,
            "enemy_sanity": o.enemy_sanity.value,
            "enemy_sanity_per_floor": o.enemy_sanity_per_floor.value,
            "bestiary_checks": o.bestiary_checks.value,
            "archipelago_pickups": self.pickup_count,
            "pickups_per_floor": o.pickups_per_floor.value,
            "shop_checks": self.shop_count,
            "boss_checks": int(bool(o.boss_checks)),
            "special_area_checks": int(bool(o.special_area_checks)),
            "stair_keys": 0,
            "floor_keys": int(bool(o.floor_keys)),
            "key_compass_time": o.key_compass_time.value,
            "floor_key_requirement": o.floor_key_requirement.value,
            "compass": o.compass.value,
            "compass_mode": o.compass_mode.value,
            "compass_ping_time": o.compass_ping_time.value,
            "compass_recharge": o.compass_recharge.value,
            "skill_checks": o.skill_checks.value,
            "enemy_randomizer_secret": int(bool(o.enemy_randomizer_secret_areas)),
            "skill_check_max_rank": o.skill_check_max_rank.value,
            "lock_class": int(bool(o.lock_class)),
            "floor_size_mask": sum(bit for name, bit in (("Normal", 1), ("Bigger", 2), ("Much Bigger", 4))
                                   if name in o.floor_size.value),
            "randomize_loot": o.randomize_loot.value,
            "music_mods": o.music_mods.value,
            "extra_monsters": o.extra_monsters.value,
            "floor_shuffle": o.floor_shuffle.value,
            "floor_shuffle_seed": self.floor_shuffle_seed,
            "difficulty_scaling": int(bool(o.difficulty_scaling)),
            "difficulty": o.difficulty.value,
            "lives": o.lives.value,
            "death_penalty": o.death_penalty.value,
            "boss_death": o.boss_death.value,
            "floor_restart": o.floor_restart.value,
            "death_items": o.death_items.value,
            "death_link_mode": o.death_link_mode.value,
            "boss_level_scaling": o.boss_level_scaling.value,
            "minotaur_time": o.minotaur_time.value,
            "trap_countdown_mask": 0,
            "polymorph_trap_duration": o.polymorph_trap_duration.value,
            "blindness_trap_duration": o.blindness_trap_duration.value,
            "enemy_randomizer": o.enemy_randomizer.value,
            "enemy_pool": o.enemy_pool.value,
            "enemy_seed": o.enemy_seed.value,
            "music_shuffle": o.music_shuffle.value,
            "music_seed": o.music_seed.value,
            "checkpoint_interval": o.checkpoint_interval.value,
            "layout_seed": o.layout_seed.value,
            "enemy_level_scaling": o.enemy_level_scaling.value,
            "death_link": int(bool(o.death_link)),
            "players": o.players.value,
        }
