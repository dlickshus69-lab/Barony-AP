from BaseClasses import ItemClassification

from . import BaronyTestBase


class TestDefault(BaronyTestBase):
    pass


class TestSanctumShuffle(BaronyTestBase):
    options = {
        "floor_keys": True, "floor_shuffle": "full", "goal": "sanctum",
        "bestiary_checks": "all", "special_area_checks": True,
    }


class TestFewChecks(BaronyTestBase):
    """Every 10th floor only, with very few other checks."""
    options = {
        "floor_check_interval": 10, "level_checks": 0, "enemy_sanity": "off",
        "bestiary_checks": "off", "archipelago_pickups": 0, "shop_checks": 0, "boss_checks": False,
    }


class TestFloorKeys(BaronyTestBase):
    """Floor Keys live in your own game: no key items, and every floor is reachable from the start."""
    options = {"floor_keys": True, "goal": "herx"}

    def test_no_key_items(self) -> None:
        self.assertFalse(any(item.name == "Progressive Stair Key" for item in self.multiworld.itempool))
        self.assertTrue(self.can_reach_location("Reach Floor 20"))
        self.assertEqual(self.world.fill_slot_data()["floor_keys"], 1)


class TestRandomizers(BaronyTestBase):
    options = {
        "enemy_sanity": "marked_enemies", "enemy_sanity_per_floor": 3, "enemy_randomizer": "every_spawn", "enemy_pool": "anywhere", "enemy_seed": "reroll", "music_shuffle": "all_music", "music_seed": "always_random",
        "bestiary_checks": "all", "floor_restart": "same_layout", "death_items": "keep_everything", "checkpoint_interval": "every_20_floors", "layout_seed": "new_each_run", "enemy_level_scaling": "match_level",
    }


class TestFillerAndTraps(BaronyTestBase):
    """Filler weights, trap countdowns and durations, boss scaling, XP-only penalty."""
    options = {
        "trap_chance": 0, "xp_boost_weight": 0, "gold_pouch_weight": 0, "healing_potion_weight": 100,
        "food_ration_weight": 0, "random_item_weight": 0,
        "minotaur_trap_weight": 0, "curse_trap_weight": 0,
        "polymorph_trap_duration": 60, "blindness_trap_duration": 20,
        "boss_level_scaling": "stronger_only", "death_penalty": "xp", "minotaur_time": 150, "death_link_mode": "lose_a_life",
    }

    def test_only_weighted_filler(self) -> None:
        names = {item.name for item in self.multiworld.itempool if item.classification == ItemClassification.filler}
        self.assertEqual(names, {"Healing Potion"})

    def test_slot_data(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["blindness_trap_duration"], 20)
        self.assertEqual(slot["boss_level_scaling"], 1)
        self.assertEqual(slot["death_penalty"], 3)
        self.assertEqual(slot["minotaur_time"], 150)
        self.assertEqual(slot["death_link_mode"], 1)

    def test_death_penalty_names(self) -> None:
        from ..options import DeathPenalty
        names = [DeathPenalty.get_option_name(v) for v in DeathPenalty.name_lookup]
        self.assertEqual(names, ["None", "Gold", "XP", "Gold and XP"])


class TestTrapWeights(BaronyTestBase):
    """Only traps with a weight above 0 end up in the pool."""
    options = {
        "trap_chance": 100, "minotaur_trap_weight": 0, "ambush_trap_weight": 0, "polymorph_trap_weight": 0,
        "fumble_trap_weight": 0, "blindness_trap_weight": 50, "teleport_trap_weight": 0, "hunger_trap_weight": 0,
        "darkness_trap_weight": 0, "curse_trap_weight": 0,
    }

    def test_only_weighted_traps(self) -> None:
        traps = {item.name for item in self.multiworld.itempool if item.classification == ItemClassification.trap}
        self.assertEqual(traps, {"Blindness Trap"})
