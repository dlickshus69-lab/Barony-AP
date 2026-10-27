from BaseClasses import ItemClassification

from . import BaronyTestBase


class TestDefault(BaronyTestBase):
    pass


class TestSanctumShuffle(BaronyTestBase):
    options = {
        "stair_keys": True, "floor_shuffle": "full", "goal": "sanctum",
        "bestiary_checks": "all", "special_area_checks": True,
    }


class TestFewChecks(BaronyTestBase):
    """Every 10th floor only: the world has to hand out some Stair Keys at the start."""
    options = {
        "stair_keys": True, "floor_check_interval": 10, "level_checks": 0, "enemy_sanity": "off",
        "bestiary_checks": "off", "archipelago_pickups": 0, "shop_checks": 0, "boss_checks": False,
    }


class TestStairKeys(BaronyTestBase):
    options = {"stair_keys": True, "goal": "herx"}

    def test_floors_need_keys(self) -> None:
        self.assertTrue(self.can_reach_location("Reach Floor 5"))
        self.assertFalse(self.can_reach_location("Reach Floor 6"))
        self.collect_by_name("Progressive Stair Key")
        self.assertTrue(self.can_reach_location("Reach Floor 20"))


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
        "trap_countdown_traps": ["Blindness Trap", "Minotaur Trap"], "trap_countdown": 7,
        "polymorph_trap_duration": 60, "blindness_trap_duration": 20,
        "boss_level_scaling": "stronger_only", "death_penalty": "xp", "minotaur_time": 150,
    }

    def test_only_weighted_filler(self) -> None:
        names = {item.name for item in self.multiworld.itempool if item.classification == ItemClassification.filler}
        self.assertEqual(names, {"Healing Potion"})

    def test_slot_data(self) -> None:
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["trap_countdown_mask"], (1 << 0) | (1 << 4))
        self.assertEqual(slot["trap_countdown"], 7)
        self.assertEqual(slot["blindness_trap_duration"], 20)
        self.assertEqual(slot["boss_level_scaling"], 1)
        self.assertEqual(slot["death_penalty"], 3)
        self.assertEqual(slot["minotaur_time"], 150)
