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
        "stair_keys": True, "floor_check_interval": 10, "level_checks": 0, "kill_checks": 0,
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
        "enemy_randomizer": "every_spawn", "enemy_pool": "anywhere", "enemy_seed": "reroll", "music_shuffle": "all_music", "music_seed": "always_random",
        "bestiary_checks": "all", "floor_restart": "same_layout", "checkpoint_interval": "every_20_floors", "layout_seed": "new_each_run",
    }
