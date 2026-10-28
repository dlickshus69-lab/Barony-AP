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
    options = {"floor_keys": True, "goal": "herx", "key_compass_time": 90}

    def test_no_key_items(self) -> None:
        self.assertFalse(any(item.name == "Progressive Stair Key" for item in self.multiworld.itempool))
        self.assertTrue(self.can_reach_location("Reach Floor 20"))
        self.assertEqual(self.world.fill_slot_data()["floor_keys"], 1)
        self.assertEqual(self.world.fill_slot_data()["key_compass_time"], 90)


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


class TestSkillChecksWarrior(BaronyTestBase):
    options = {"skill_checks": "warrior", "skill_check_max_rank": "expert"}

    def test_warrior_skills(self) -> None:
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Skill: Expert Polearm", names)       # starts at 50
        self.assertNotIn("Skill: Skilled Polearm", names)   # already past it
        self.assertIn("Skill: Basic Lore", names)           # every class
        self.assertNotIn("Skill: Basic Sorcery", names)     # not a warrior skill
        self.assertNotIn("Skill: Master Sword", names)      # above the max rank
        self.assertEqual(self.world.fill_slot_data()["skill_checks"], 3)


class TestSkillChecksAnyClass(BaronyTestBase):
    options = {"skill_checks": "any_class", "skill_check_max_rank": "legendary"}

    def test_universal_only(self) -> None:
        skills = [loc.name for loc in self.multiworld.get_locations(self.player) if loc.name.startswith("Skill: ")]
        self.assertEqual(len(skills), 15)  # Lore, Trading, Stealth x 5 ranks


class TestFloorSizeMix(BaronyTestBase):
    options = {"floor_size": ["Normal", "Much Bigger"]}

    def test_mask(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["floor_size_mask"], 1 | 4)


class TestProgressiveCompass(BaronyTestBase):
    options = {"compass": "progressive", "compass_mode": "always_on", "floor_key_requirement": "key_only"}

    def test_two_compasses(self) -> None:
        count = sum(1 for item in self.multiworld.itempool if item.name == "Progressive Compass")
        self.assertEqual(count, 2)
        slot = self.world.fill_slot_data()
        self.assertEqual(slot["compass"], 3)
        self.assertEqual(slot["compass_mode"], 1)
        self.assertEqual(slot["floor_key_requirement"], 1)


class TestCompassItem(BaronyTestBase):
    options = {"compass": "archipelago_item"}

    def test_one_compass(self) -> None:
        count = sum(1 for item in self.multiworld.itempool if item.name == "Progressive Compass")
        self.assertEqual(count, 1)


class TestBossRush(BaronyTestBase):
    """Boss Rush: a short dungeon, then the picked bosses; nothing past the rush floors."""
    options = {"goal": "boss_rush", "dungeon_floors": 12, "boss_rush_bosses": ["Herx", "Twin Liches"],
               "boss_rush_rest_town": False, "boss_checks": True}

    def test_rush_slot_data(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual(sd["goal"], 3)
        self.assertEqual(sd["goal_floor"], 12)
        self.assertEqual(sd["boss_rush_mask"], 5)
        self.assertEqual(sd["boss_rush_town"], 0)
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertNotIn("Reach Floor 13", names)
        self.assertNotIn("Defeat Baron Herx", names)


class TestBossRushEmptyBosses(BaronyTestBase):
    options = {"goal": "boss_rush", "boss_rush_bosses": []}

    def test_empty_means_all(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["boss_rush_mask"], 7)


class TestBossRushLong(BaronyTestBase):
    options = {"goal": "boss_rush", "dungeon_floors": 34, "floor_shuffle": "full", "boss_checks": True}

    def test_long(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["goal_floor"], 34)
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertNotIn("Defeat the Devil", names)


class TestBossRushRandomOrder(BaronyTestBase):
    options = {"goal": "boss_rush", "boss_rush_order": "shuffled_each_run"}

    def test_order(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual(sd["boss_rush_order"], 2)
        self.assertGreater(sd["boss_rush_seed"], 0)


class TestSpellSanity(BaronyTestBase):
    options = {"spell_sanity": True, "spell_upgrades": 2, "spellbook_checks": True, "spell_sanity_dlc_spells": True,
               "goal": "devil"}

    def test_spells(self) -> None:
        names = [item.name for item in self.multiworld.itempool]
        self.assertIn("Fireball Spell", names)
        self.assertIn("Conjure Skeleton Spell", names)
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Read the Fireball Spellbook", locs)
        self.assertNotIn("Read the Elemental Focus Spellbook", locs)  # depth 30, past the Devil
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["spell_sanity"], sd["spell_upgrades"], sd["spellbook_checks"], sd["spell_dlc"]), (1, 2, 1, 1))


class TestSpellSanityFewLocations(BaronyTestBase):
    """Very few locations: spells are trimmed instead of failing."""
    options = {"spell_sanity": True, "spell_upgrades": 2, "floor_check_interval": 10, "level_checks": 0,
               "enemy_sanity": "off", "bestiary_checks": "off", "archipelago_pickups": 0, "shop_checks": 0,
               "boss_checks": False}


class TestBounty(BaronyTestBase):
    options = {"bounty_enemies": True, "bounty_chance": 50, "bounty_checks": 8}

    def test_bounty(self) -> None:
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Bounty 8", locs)
        self.assertNotIn("Bounty 9", locs)
        self.assertEqual(self.world.fill_slot_data()["bounty_checks"], 8)


class TestChampions(BaronyTestBase):
    """Every 5 floors to the Devil (floor 24): 5, 10, 15 (20 is Herx's floor)."""
    options = {"goal": "devil", "champion_floors": 5, "champion_checks": 10}

    def test_champions(self) -> None:
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Champion 3", locs)
        self.assertNotIn("Champion 4", locs)
        self.assertEqual(self.world.champion_floors(), [5, 10, 15])
        self.assertEqual(self.world.fill_slot_data()["champion_checks"], 3)


class TestChampionsOff(BaronyTestBase):
    options = {"champion_floors": 0, "champion_checks": 10}

    def test_off(self) -> None:
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertNotIn("Champion 1", locs)
        self.assertEqual(self.world.fill_slot_data()["champion_checks"], 0)


class TestLantern(BaronyTestBase):
    options = {"progressive_lantern": True, "lantern_darkness": 3}

    def test_lantern(self) -> None:
        pool = [i.name for i in self.multiworld.itempool if i.player == self.player]
        self.assertEqual(pool.count("Progressive Lantern"), 3)
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["progressive_lantern"], sd["lantern_darkness"]), (1, 3))


class TestNoLantern(BaronyTestBase):
    def test_no_lantern(self) -> None:
        pool = [i.name for i in self.multiworld.itempool if i.player == self.player]
        self.assertNotIn("Progressive Lantern", pool)


class TestCaravan(BaronyTestBase):
    options = {"merchant_caravan": True, "caravan_chance": 50, "caravan_checks": 6, "caravan_hints": True}

    def test_caravan(self) -> None:
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Caravan Purchase 6", locs)
        self.assertNotIn("Caravan Purchase 7", locs)
        from Fill import distribute_items_restrictive
        distribute_items_restrictive(self.multiworld)
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["merchant_caravan"], sd["caravan_checks"], sd["caravan_hints"]), (1, 6, 1))
        own = {loc.address for loc in self.multiworld.get_locations(self.player)}
        self.assertTrue(sd["hint_locations"])
        self.assertTrue(set(sd["hint_locations"]) <= own)


class TestCaravanOff(BaronyTestBase):
    options = {"merchant_caravan": False, "caravan_checks": 6}

    def test_off(self) -> None:
        locs = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertNotIn("Caravan Purchase 1", locs)
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["caravan_checks"], sd["caravan_hints"], sd["hint_locations"]), (0, 0, []))


class TestDeathMarkers(BaronyTestBase):
    options = {"death_markers": True, "death_marker_gold": 35}

    def test_slot(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["death_markers"], sd["death_marker_gold"]), (1, 35))


class TestAllBosses(BaronyTestBase):
    """All Three Bosses: the whole dungeon to floor 35; every boss is part of the goal."""
    options = {"goal": "all_bosses", "boss_checks": True}

    def test_all_bosses(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["goal"], sd["goal_floor"], sd["dungeon_floors"]), (4, 35, 0))
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Reach Floor 35", names)
        self.assertIn("Reach Floor 22", names)  # Hell is on the route
        for boss in ("Defeat Baron Herx", "Defeat the Devil", "Defeat the Ice Lich"):
            self.assertNotIn(boss, names)  # goal bosses aren't checks
        self.assertIn("Defeat a Minotaur", names)


class TestAllBossesShort(BaronyTestBase):
    options = {"goal": "all_bosses", "custom_dungeon_length": True, "dungeon_floors": 15}

    def test_short(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["goal_floor"], sd["dungeon_floors"]), (15, 15))


class TestFloorThemes(BaronyTestBase):
    options = {"floor_themes": True, "floor_theme_chance": 60, "floor_theme_list": ["Dark", "Gold Rush"]}

    def test_themes(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["floor_themes"], sd["floor_theme_chance"]), (1, 60))
        self.assertEqual(sd["floor_theme_mask"], (1 << 0) | (1 << 10))  # Dark is bit 0, Gold Rush bit 10


class TestShortHerx(BaronyTestBase):
    """Dungeon Floors with a normal goal: 8 floors, then Baron Herx."""
    options = {"goal": "herx", "custom_dungeon_length": True, "dungeon_floors": 12, "boss_checks": True}

    def test_short(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["goal_floor"], sd["dungeon_floors"]), (12, 12))
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Reach Floor 12", names)
        self.assertNotIn("Reach Floor 13", names)


class TestBossRushDefaultFloors(BaronyTestBase):
    options = {"goal": "boss_rush"}

    def test_default(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["dungeon_floors"], 10)


class TestNormalLength(BaronyTestBase):
    options = {"goal": "devil", "dungeon_floors": 15}

    def test_normal(self) -> None:
        sd = self.world.fill_slot_data()
        self.assertEqual((sd["goal_floor"], sd["dungeon_floors"]), (24, 0))


class TestBonusFloors(BaronyTestBase):
    options = {"goal": "sanctum", "custom_dungeon_length": True, "dungeon_floors": 44, "floor_shuffle": "full"}

    def test_bonus(self) -> None:
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Reach Floor 44", names)
        self.assertEqual(self.world.fill_slot_data()["goal_floor"], 44)
