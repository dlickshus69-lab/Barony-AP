/*-------------------------------------------------------------------------------

	Barony Archipelago client. See archipelago_barony.hpp for the hooks and
	PATCHES.md for where each one is called from.

	Connection:
	  - the tracker window (BaronyAPTracker.exe, opened with the game) has the connect form
	  - without the tracker, archipelago.cfg next to barony.exe (server=, slot=, password=) connects on start
	  - or the console: /ap_connect archipelago.gg:38281 MySlot [password]

	Saved progress (one-shot items already used, kill count, lives, queued traps)
	goes in archipelago/<seed>_<slot>.txt, so nothing is applied twice.

-------------------------------------------------------------------------------*/

#include "main.hpp"
#include "game.hpp"
#include "stat.hpp"
#include "entity.hpp"
#include "items.hpp"
#include "monster.hpp"
#include "engine/audio/sound.hpp"
#include "net.hpp"
#include "collision.hpp"
#include "magic/magic.hpp"
#include "player.hpp"
#include "prng.hpp"
#include "files.hpp"
#include "magic/magic.hpp"
#include "init.hpp"
#include "interface/consolecommand.hpp"
#include "input.hpp"
#include "menu.hpp"
#include "ui/Frame.hpp"
#include "ui/Image.hpp"
#include "ui/Text.hpp"
#include "interface/interface.hpp"

#include "Archipelago.h"
#include "archipelago_barony.hpp"
#include "archipelago_ids.hpp"

#include <algorithm>
#include <initializer_list>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

namespace AP
{
namespace
{
	constexpr const char* GAME_NAME = "Barony";
	constexpr Uint32 AP_FLOOR_APPEARANCE = 0x41504631; // "APF1": an Archipelago floor pickup
	constexpr Uint32 AP_SHOP_APPEARANCE = 0x41505331;  // "APS1": an Archipelago shop item
	constexpr Uint32 AP_CARAVAN_APPEARANCE = 0x41504331; // "APC1": an Archipelago item sold by a caravan
	constexpr Uint32 AP_HINT_APPEARANCE = 0x41504831;  // "APH1": an Archipelago Hint sold by a caravan
	constexpr ItemType AP_ITEM_TYPE = GEM_LUCK;        // borrowed model until there's a logo model
	const Uint32 COLOR_AP = makeColorRGB(255, 166, 77);
	const Uint32 COLOR_TRAP = makeColorRGB(255, 90, 90);

	// --------------------------------------------------------------- options from slot data
	struct SlotOptions
	{
		int goal = 0;
		int goal_floor = 20;
		int floor_check_interval = 1;
		int level_checks = 0;
		int kill_checks = 0;
		int kill_check_interval = 10;
		int enemy_sanity = 1;         // 0 off, 1 kill count, 2 marked enemies
		int enemy_sanity_per_floor = 2;
		int bestiary_checks = 0;      // 0 off, 1 early, 2 all
		int archipelago_pickups = 0;
		int pickups_per_floor = 2;
		int shop_checks = 0;
		int boss_checks = 0;
		int special_area_checks = 0;
		int floor_keys = 0;           // a key on every floor opens its stairs
		int key_compass_time = 30;
		int floor_key_requirement = 0; // 0 key or all enemies, 1 key only, 2 all enemies only
		int compass = 0;              // 0 off, 1 start with it, 2 AP item, 3 progressive
		int compass_mode = 0;         // 0 ping and recharge, 1 always on
		int compass_ping_time = 15;
		int compass_recharge = 90;
		int enemy_randomizer_secret = 0; // Enemy Randomizer also in secret areas (never Minetown)
		int skill_checks = 0;         // 0 off, 1 any class, 2.. a class (Barony class index + 2)
		int skill_check_max_rank = 3; // 1 Basic .. 5 Legendary
		int lock_class = 0;
		int champion_floors = 0;      // every this many floors a Champion guards the stairs (0 off)
		int champion_locks_stairs = 1;
		int champion_checks = 0;
		int progressive_lantern = 0;  // less light, three Progressive Lanterns bring it back and more
		int lantern_darkness = 2;
		int death_markers = 0;        // gravestones where you died give some gold back
		int death_marker_gold = 20;   // percent of the gold you had
		int merchant_caravan = 0;     // a travelling shop on some floors
		int caravan_chance = 25;
		int caravan_checks = 0;
		int caravan_hints = 0;
		std::vector<int64_t> hint_locations; // Caravan Hints: this slot's locations, the ones that matter first
		int bounty_enemies = 0;       // a gold-glowing, tougher enemy on some floors
		int bounty_chance = 50;       // percent of floors with a bounty
		int bounty_checks = 0;        // the first N bounty kills are checks
		int spell_sanity = 0;         // spells are Archipelago items
		int spell_upgrades = 0;       // extra copies make a spell stronger
		int spellbook_checks = 0;     // reading each kind of spellbook is a check
		int spell_dlc = 0;            // DLC class spells are in the pool too
		int floor_size_mask = 1;      // Floor Size: bit 1 Normal, 2 Bigger, 4 Much Bigger
		int boss_rush_mask = 7;       // Boss Rush: 1 Herx, 2 Devil, 4 Twin Liches
		int dungeon_floors = 0;       // Custom Dungeon Length / Boss Rush: floors before the goal boss (0 = usual depth)
		int boss_rush_town = 1;       // Boss Rush: rest in a town between bosses
		int boss_rush_order = 0;      // Boss Rush: 0 game order, 1 random (same every run), 2 random each run
		int boss_rush_seed = 0;       // Boss Rush: the seed's own shuffle
		int floor_key_on_tracker_map = 0; // 1: the tracker map always shows the Floor Key
		int randomize_loot = 0;       // 0 off, 1 same kind, 2 anything
		int music_mods = 0;           // 0 off, 1 mixed with the original, 2 mods only
		int extra_monsters = 0;       // how many more monsters per floor (0 to 20)           // new characters always get the Skill Checks class    // Minotaur floors: seconds before he arrives that the key compass turns on
		int floor_shuffle = 0;        // 0 off, 1 sections, 2 full
		int floor_shuffle_seed = 0;
		int difficulty_scaling = 1;
		int difficulty = 0;           // 0 normal, 1 hard
		int lives = 0;
		int death_penalty = 0;        // 0 none, 1 gold, 2 gold and xp
		int boss_death = 0;           // 0 reset, 1 keep damage
		int floor_restart = 0;        // 0 new layout, 1 same layout
		int enemy_randomizer = 0;     // 0 off, 1 per floor, 2 every spawn
		int enemy_pool = 0;           // 0 near your depth, 1 anywhere
		int enemy_seed = 0;           // 0 locked to the seed, 1 re-rolled every time a floor loads
		int music_shuffle = 0;        // 0 off, 1 biome music, 2 all music
		int music_seed = 0;           // 0 locked per floor, 1 random every time a song starts
		int checkpoint_interval = 0;  // 0 off, else a checkpoint every this many floors
		int layout_seed = 0;          // 0 locked (each floor's map comes from the seed), 1 random
		int enemy_level_scaling = 0;  // 0 off, 1 stronger only, 2 match your level both ways
		int boss_level_scaling = 0;   // the same, for bosses
		int polymorph_trap_duration = 30;
		int blindness_trap_duration = 15;
		int minotaur_time = 100;      // percent of the normal time before the Minotaur arrives
		int death_link_mode = 0;      // 0 die and restart the floor, 1 just lose a life
		int death_items = 0;          // 0 back to what you had entering the floor, 1 keep everything you had
		int players = 1;
	};

	// --------------------------------------------------------------- saved progress
	struct SaveData
	{
		int processedItems = 0;       // received items already used (one-shot items)
		long totalKills = 0;
		long greenKills = 0;          // Enemy Sanity Kill Count: green (marked) enemies killed, across runs
		Uint32 runKey = 0;            // uniqueGameKey of the run permanent items were applied to
		int livesLeft = 0;
		int minotaurQueue = 0;
		int darknessQueue = 0;
		int goalBossesKilled = 0;     // bit per goal boss
		bool goalSent = false;
		long bountyKills = 0;         // bounty enemies defeated (all runs)
		long championKills = 0;       // Champions defeated (all runs)
		struct Grave { int floor; long gold; bool claimed; };
		std::vector<Grave> graves;    // Death Markers
		std::set<int64_t> hinted;     // Caravan Hints already bought
		// Hall of Fame: this run so far, and every run before ("deepest,kills,livesLost,gold,seconds,result")
		bool runActive = false;
		int runDeepest = 0, runKills = 0, runLivesLost = 0;
		long runGoldMax = 0, runStartPlay = 0;
		std::vector<std::string> runs;
		long playSeconds = 0;         // time spent in the dungeon for this seed (not the menus, not paused)
		long finishSeconds = -1;      // playtime when the goal was completed (-1 = not yet)
		std::string rushSeq;          // Boss Rush: this run's maps in order ("boss,hamlet,hellboss")
		int rushPhase = 0;            // Boss Rush: how far along rushSeq this run is (1 = the first)
		int rushLevel = 0;            // Boss Rush: the floor number the current phase was loaded at
		Uint32 rushRunKey = 0;        // the run the rush state belongs to
		int permApplied[8] = {};      // permanent items already given to the current run's character
		std::map<std::string, int> deaths; // death counter: deaths per slot name in this multiworld
	};

	const int64_t PERMANENT_ITEMS[8] = {
		ap_ids::ITEM_PERM_STR, ap_ids::ITEM_PERM_DEX, ap_ids::ITEM_PERM_CON, ap_ids::ITEM_PERM_INT,
		ap_ids::ITEM_PERM_PER, ap_ids::ITEM_PERM_CHR, ap_ids::ITEM_MAX_HP_UP, ap_ids::ITEM_START_GOLD,
	};

	// --------------------------------------------------------------- shared with the network thread
	std::mutex g_netMutex;
	std::vector<int64_t> g_inbox;              // received items not yet handled
	std::vector<std::string> g_deathSources;   // Death Link senders not yet counted
	std::vector<std::string> g_deathTexts;     // "Herx died" lines for the logs, not yet shown
	bool g_inboxClearRequested = false;
	std::set<int64_t> g_serverChecked;         // locations the server says are checked
	SlotOptions g_slotIncoming;
	bool g_slotIncomingReady = false;
	std::atomic<bool> g_deathLinkIn{ false };

	// --------------------------------------------------------------- game-thread state
	bool g_started = false;                    // AP_Start has been called
	bool g_ready = false;                      // authenticated, slot data + save loaded
	bool g_triedAutoConnect = false;
	Uint32 g_lostSince = 0;      // ticks when the connection dropped (0 = connected)
	bool g_apOff = false;        // "Play without Archipelago" in the launcher (or barony.exe -ap-off)
	bool g_viaLauncher = false;  // started from the tracker's launcher: it's already open, connect.txt waiting
	std::string g_server, g_slotName, g_password;
	std::string g_savePath;
	SlotOptions g_slot;
	SaveData g_save;
	std::vector<int64_t> g_received;           // every item received, in order
	std::set<int64_t> g_sent;                  // locations sent this session
	std::vector<int> g_floorLineMap;           // floor shuffle: levels.txt line per floor
	size_t g_floorLineMapSource = 0;
	bool g_dyingFromDeathLink = false;
	bool g_restartPending = false;              // floor reload requested, waiting for it to finish

	// A fixed number of Archipelago items per floor. The first try at a floor decides how many it has
	// (on the ground, in chests, in breakables); a restart only brings back the ones not found yet.
	int g_floorApTotal = 0;        // AP items this floor has had (first try)
	int g_floorApFound = 0;        // of those, picked up on this floor
	int g_attemptApMade = 0;       // AP items made during the current try
	int g_restartApBudget = 0;     // on a restarted floor, how many may still appear
	bool g_floorIsRestart = false; // the current floor is a restart
	bool g_floorReady = false;     // onNewFloor finished; the next generation starts a new count
	int g_floorApUnfound = 0;      // Archipelago items still on the current floor (kept up to date)
	int g_carryPickups = 0;        // left behind on earlier floors: added to the next floor that gets pickups

	bool g_compassGivePending = false; // a new run: hand over the compass once the character is ready
	int g_secretExitThisFloor = 0; // set by generateDungeon, used once in onNewFloor
	bool g_floorHasSecretExit = false; // this floor has a secret-area entrance (for the tracker)
	Uint32 g_floorStartTicks = 0;  // when this floor started (the key hint shows for a little while)
	int g_minotaurSecs = -1;       // seconds until the Minotaur arrives; -1 none coming, -2 he's here
	int g_minotaurElapsed = 0;     // seconds since his timer started

	void updateMinotaurStatus()
	{
		g_minotaurSecs = -1;
		if ( !map.entities ) { return; }
		bool timerActive = false;
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e ) { continue; }
			if ( e->behavior == &actMinotaurTimer )
			{
				if ( e->skill[1] ) { timerActive = true; }
				else
				{
					int left = getMinotaurTimeToArrive() - e->skill[0];
					g_minotaurSecs = std::max(0, left / TICKS_PER_SECOND);
					g_minotaurElapsed = e->skill[0] / TICKS_PER_SECOND;
					return;
				}
			}
		}
		if ( map.creatures )
		{
			for ( node_t* node = map.creatures->first; node; node = node->next )
			{
				Entity* e = (Entity*)node->element;
				Stat* st = e ? e->getStats() : nullptr;
				if ( st && st->type == MINOTAUR && st->HP > 0 ) { g_minotaurSecs = -2; return; }
			}
		}
		(void)timerActive;
	}

	void noteApItemMade()
	{
		++g_attemptApMade;
		if ( !g_floorIsRestart ) { ++g_floorApTotal; }
	}
	Uint32 g_floorLoads = 0;                    // counts floor generations, for re-rolled enemies

	// Death screen: a life was lost, waiting for the player to press confirm.
	bool g_deathScreen = false;
	int g_deathScreenPlayer = 0;
	Uint32 g_deathScreenStart = 0;
	constexpr Uint32 DEATH_SCREEN_MIN_TICKS = TICKS_PER_SECOND * 2;
	Uint32 g_lastPlayerUid = 0;
	bool g_savedGodmode = false;
	bool g_confirmWasDown = true;               // edge detection: needs a fresh press
	void endDeathScreen();
	void warnSkillClass();
	void giveCompassIfMissing();
	void launchTracker();
	void startRunRecord();
	void buyHint();
	std::string g_routeNote; // All Three Bosses: said when the next floor is ready
	void clearFloorUids();
	bool g_hofPanel = false;      // Hall of Fame panel (F5)
	Uint32 g_hofAutoUntil = 0;    // shown by itself for a while at the start of a run
	void placeRushTownExit();
	void syncSpells();
	int bossRushGoalMask();
	Uint32 g_stairsWarnedAt = 0;
	void readCheckpoint();
	int apItemsLyingAround();
	std::set<Uint32> g_countedDeaths;          // monster uids already counted

	// Inventory as it was when the current floor began; losing a life puts it back.
	struct ItemSnapshot
	{
		ItemType type; Status status; Sint16 beatitude; Sint16 count;
		Uint32 appearance; bool identified; Uint32 uid; Sint32 x, y;
		int equipSlot; // index into EQUIP_SLOTS, or -1
	};
	Item* Stat::* const EQUIP_SLOTS[] = {
		&Stat::helmet, &Stat::breastplate, &Stat::gloves, &Stat::shoes, &Stat::shield,
		&Stat::weapon, &Stat::cloak, &Stat::amulet, &Stat::ring, &Stat::mask,
	};
	constexpr int NUM_EQUIP_SLOTS = sizeof(EQUIP_SLOTS) / sizeof(EQUIP_SLOTS[0]);
	std::vector<ItemSnapshot> g_floorStartInventory[MAXPLAYERS];
	bool g_hasFloorSnapshot[MAXPLAYERS] = {};

	// boss HP kept through a lost life
	int g_savedBossFloor = -1;
	std::map<int, Sint32> g_savedBossHP;

	// level tracking
	int g_lastLevel = -1, g_lastSecret = -1;
	Uint32 g_lastMapSeed = 0;
	bool g_generatedFloorKey = false;
	int g_generatedLevel = -1, g_generatedSecret = -1;
	int g_lastCharLevelSent = 1;
	int g_tickCounter = 0;

	// ----------------------------------------------------------- helpers
	// Archipelago log for the F1 / F2 panels and the tracker window.
	struct LogLine { std::string text; Uint32 color; bool item; };
	std::vector<LogLine> g_log;
	constexpr size_t LOG_KEEP = 200;

	bool isItemMessage(const std::string& t)
	{
		static const char* const keys[] = { "Received", "Found an Archipelago", "Trap", " sent to ", " found their ",
			"Marked enemy", "Extra Life", "Lives:", " died", "lose a life" };
		for ( const char* k : keys ) { if ( t.find(k) != std::string::npos ) { return true; } }
		return false;
	}

	void addLog(const std::string& text, Uint32 color)
	{
		g_log.push_back(LogLine{ text, color, isItemMessage(text) });
		if ( g_log.size() > LOG_KEEP ) { g_log.erase(g_log.begin(), g_log.begin() + (g_log.size() - LOG_KEEP)); }
	}

	void say(Uint32 color, const char* fmt, ...)
	{
		char buf[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		messagePlayerColor(clientnum, MESSAGE_HINT, color, "%s", buf);
		printlog("[Archipelago] %s", buf);
		addLog(buf, color);
	}

	bool inGame()
	{
		return !intro && !loading && players[clientnum] && players[clientnum]->entity
			&& stats[clientnum] && multiplayer != CLIENT;
	}

	bool isChecked(int64_t loc)
	{
		if ( g_sent.count(loc) ) { return true; }
		std::lock_guard<std::mutex> lock(g_netMutex);
		return g_serverChecked.count(loc) > 0;
	}

	void sendLocation(int64_t loc)
	{
		if ( !g_ready || isChecked(loc) ) { return; }
		g_sent.insert(loc);
		AP_SendItem(loc);
	}

	int countReceived(int64_t item)
	{
		return (int)std::count(g_received.begin(), g_received.end(), item);
	}

	std::vector<int> activePlayers()
	{
		std::vector<int> result;
		for ( int c = 0; c < MAXPLAYERS; ++c )
		{
			if ( !client_disconnected[c] && players[c] && players[c]->entity && stats[c] && stats[c]->HP > 0 )
			{
				result.push_back(c);
			}
			if ( g_slot.players <= 1 && !result.empty() ) { break; } // solo: the host only
		}
		return result;
	}

	int randomActivePlayer()
	{
		auto list = activePlayers();
		if ( list.empty() ) { return -1; }
		return list[local_rng.rand() % list.size()];
	}

	// ----------------------------------------------------------- save file
	void writeSave()
	{
		if ( g_savePath.empty() ) { return; }
		std::ofstream out(g_savePath, std::ios::trunc);
		if ( !out ) { return; }
		out << "processedItems=" << g_save.processedItems << "\n";
		out << "totalKills=" << g_save.totalKills << "\n";
		out << "greenKills=" << g_save.greenKills << "\n";
		out << "runKey=" << g_save.runKey << "\n";
		out << "livesLeft=" << g_save.livesLeft << "\n";
		out << "minotaurQueue=" << g_save.minotaurQueue << "\n";
		out << "darknessQueue=" << g_save.darknessQueue << "\n";
		out << "goalBossesKilled=" << g_save.goalBossesKilled << "\n";
		out << "goalSent=" << (g_save.goalSent ? 1 : 0) << "\n";
		out << "bountyKills=" << g_save.bountyKills << "\nchampionKills=" << g_save.championKills << "\n";
		out << "runActive=" << (g_save.runActive ? 1 : 0) << "\nrunDeepest=" << g_save.runDeepest << "\nrunKills=" << g_save.runKills
			<< "\nrunLivesLost=" << g_save.runLivesLost << "\nrunGoldMax=" << g_save.runGoldMax << "\nrunStartPlay=" << g_save.runStartPlay << "\n";
		for ( auto& r : g_save.runs ) { out << "run:" << r << "\n"; }
		for ( auto& g : g_save.graves ) { out << "grave:" << g.floor << "," << g.gold << "," << (g.claimed ? 1 : 0) << "\n"; }
		for ( int64_t h : g_save.hinted ) { out << "hinted:" << h << "\n"; }
		out << "playSeconds=" << g_save.playSeconds << "\nfinishSeconds=" << g_save.finishSeconds << "\n";
		out << "carryPickups=" << g_carryPickups << "\nfloorUnfound=" << g_floorApUnfound << "\n";
		out << "rushSeq=" << g_save.rushSeq << "\nrushPhase=" << g_save.rushPhase << "\nrushLevel=" << g_save.rushLevel
			<< "\nrushRunKey=" << g_save.rushRunKey << "\n";
		for ( auto& d : g_save.deaths ) { out << "death:" << d.first << "=" << d.second << "\n"; }
		for ( int i = 0; i < 8; ++i ) { out << "perm" << i << "=" << g_save.permApplied[i] << "\n"; }
	}

	void readSave()
	{
		g_save = SaveData();
		std::ifstream in(g_savePath);
		std::string line;
		while ( std::getline(in, line) )
		{
			if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
			if ( line.compare(0, 4, "run:") == 0 ) { g_save.runs.push_back(line.substr(4)); continue; } // Hall of Fame
			if ( line.compare(0, 6, "grave:") == 0 )
			{
				SaveData::Grave g{};
				int claimed = 0;
				if ( sscanf(line.c_str() + 6, "%d,%ld,%d", &g.floor, &g.gold, &claimed) == 3 ) { g.claimed = claimed != 0; g_save.graves.push_back(g); }
				continue;
			}
			if ( line.compare(0, 7, "hinted:") == 0 ) { g_save.hinted.insert(atoll(line.c_str() + 7)); continue; }
			auto eq = line.rfind('=');
			if ( eq == std::string::npos ) { continue; }
			std::string key = line.substr(0, eq);
			if ( key.compare(0, 6, "death:") == 0 )
			{
				g_save.deaths[key.substr(6)] = atoi(line.c_str() + eq + 1);
				continue;
			}
			long long value = atoll(line.c_str() + eq + 1);
			if ( key == "rushSeq" ) { g_save.rushSeq = line.substr(eq + 1); continue; }
			if ( key == "processedItems" ) { g_save.processedItems = (int)value; }
			else if ( key == "totalKills" ) { g_save.totalKills = (long)value; }
			else if ( key == "greenKills" ) { g_save.greenKills = (long)value; }
			else if ( key == "runKey" ) { g_save.runKey = (Uint32)value; }
			else if ( key == "livesLeft" ) { g_save.livesLeft = (int)value; }
			else if ( key == "minotaurQueue" ) { g_save.minotaurQueue = (int)value; }
			else if ( key == "darknessQueue" ) { g_save.darknessQueue = (int)value; }
			else if ( key == "goalBossesKilled" ) { g_save.goalBossesKilled = (int)value; }
			else if ( key == "goalSent" ) { g_save.goalSent = value != 0; }
			else if ( key == "playSeconds" ) { g_save.playSeconds = (long)value; }
			else if ( key == "bountyKills" ) { g_save.bountyKills = (long)value; }
			else if ( key == "championKills" ) { g_save.championKills = (long)value; }
			else if ( key == "runActive" ) { g_save.runActive = value != 0; }
			else if ( key == "runDeepest" ) { g_save.runDeepest = (int)value; }
			else if ( key == "runKills" ) { g_save.runKills = (int)value; }
			else if ( key == "runLivesLost" ) { g_save.runLivesLost = (int)value; }
			else if ( key == "runGoldMax" ) { g_save.runGoldMax = (long)value; }
			else if ( key == "runStartPlay" ) { g_save.runStartPlay = (long)value; }
			else if ( key == "finishSeconds" ) { g_save.finishSeconds = (long)value; }
			else if ( key == "rushPhase" ) { g_save.rushPhase = (int)value; }
			else if ( key == "carryPickups" ) { g_carryPickups = (int)value; }
			else if ( key == "floorUnfound" ) { g_floorApUnfound = (int)value; }
			else if ( key == "rushLevel" ) { g_save.rushLevel = (int)value; }
			else if ( key == "rushRunKey" ) { g_save.rushRunKey = (Uint32)value; }
			else if ( key.size() == 5 && key.compare(0, 4, "perm") == 0 && key[4] >= '0' && key[4] <= '7' )
			{
				g_save.permApplied[key[4] - '0'] = (int)value;
			}
		}
	}

	// Death counter.
	void recordDeath(const std::string& who)
	{
		if ( who.empty() ) { return; }
		++g_save.deaths[who];
		writeSave();
	}

	int totalDeaths()
	{
		int n = 0;
		for ( auto& d : g_save.deaths ) { n += d.second; }
		return n;
	}

	void pullDeathSources()
	{
		std::vector<std::string> sources, texts;
		{
			std::lock_guard<std::mutex> lock(g_netMutex);
			sources.swap(g_deathSources);
			texts.swap(g_deathTexts);
		}
		for ( auto& src : sources ) { recordDeath(src); }
		for ( auto& t : texts ) { say(COLOR_TRAP, "%s", t.c_str()); }
	}

	std::string sanitize(std::string s)
	{
		for ( auto& ch : s )
		{
			if ( !isalnum((unsigned char)ch) && ch != '-' && ch != '_' ) { ch = '_'; }
		}
		return s;
	}

	// ----------------------------------------------------------- connection
	void registerSlotInt(const char* key, int SlotOptions::* field)
	{
		AP_RegisterSlotDataIntCallback(key, [field](int value) {
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_slotIncoming.*field = value;
			g_slotIncomingReady = true;
		});
	}

	bool g_resendChecks = false; // after a reconnect that kept the run going: send every check again

	void disconnect(bool keepRun = false)
	{
		if ( g_started )
		{
			AP_Shutdown();
		}
		g_started = false;
		if ( keepRun )
		{
			// Same slot, new address (the room came back on a new port): the run keeps going with
			// its slot data and save; the server resends the items and we resend the checks.
			g_resendChecks = true;
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_inbox.clear();
			g_serverChecked.clear();
			return;
		}
		g_ready = false;
		g_received.clear();
		g_sent.clear();
		g_floorLineMap.clear();
		g_floorLineMapSource = 0;
		std::lock_guard<std::mutex> lock(g_netMutex);
		g_inbox.clear();
		g_serverChecked.clear();
		g_slotIncomingReady = false;
	}

	void connect(const std::string& server, const std::string& slot, const std::string& password)
	{
		bool keepRun = g_ready && slot == g_slotName;
		if ( keepRun ) { writeSave(); }
		disconnect(keepRun);
		g_server = server;
		g_slotName = slot;
		g_password = password;

		AP_Init(server.c_str(), GAME_NAME, slot.c_str(), password.c_str());
		AP_SetItemClearCallback([]() {
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_inbox.clear();
			g_inboxClearRequested = true;
		});
		AP_SetItemRecvCallback([](int64_t item, bool) {
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_inbox.push_back(item);
		});
		AP_SetLocationCheckedCallback([](int64_t loc) {
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_serverChecked.insert(loc);
		});
		AP_SetDeathLinkSupported(true);
		AP_SetDeathLinkRecvCallback([](std::string source, std::string cause) {
			{
				std::lock_guard<std::mutex> lock(g_netMutex);
				g_deathSources.push_back(source);
				// The log shows who died (and how, when their game says): "Herx died: fell in lava".
				std::string line = source.empty() ? std::string("Someone died") : source + " died";
				// Most games already write the whole line ("Herx was eaten by a grue"): show that as is.
				if ( !cause.empty() && !source.empty() && cause.find(source) != std::string::npos ) { line = cause; }
				else if ( !cause.empty() && cause != source ) { line += ": " + cause; }
				g_deathTexts.push_back(line);
			}
			g_deathLinkIn = true;
		});

		registerSlotInt("goal", &SlotOptions::goal);
		registerSlotInt("goal_floor", &SlotOptions::goal_floor);
		registerSlotInt("floor_check_interval", &SlotOptions::floor_check_interval);
		registerSlotInt("level_checks", &SlotOptions::level_checks);
		registerSlotInt("kill_checks", &SlotOptions::kill_checks);
		registerSlotInt("kill_check_interval", &SlotOptions::kill_check_interval);
		registerSlotInt("enemy_sanity", &SlotOptions::enemy_sanity);
		registerSlotInt("enemy_sanity_per_floor", &SlotOptions::enemy_sanity_per_floor);
		registerSlotInt("bestiary_checks", &SlotOptions::bestiary_checks);
		registerSlotInt("archipelago_pickups", &SlotOptions::archipelago_pickups);
		registerSlotInt("pickups_per_floor", &SlotOptions::pickups_per_floor);
		registerSlotInt("shop_checks", &SlotOptions::shop_checks);
		registerSlotInt("boss_checks", &SlotOptions::boss_checks);
		registerSlotInt("special_area_checks", &SlotOptions::special_area_checks);
		registerSlotInt("floor_keys", &SlotOptions::floor_keys);
		registerSlotInt("key_compass_time", &SlotOptions::key_compass_time);
		registerSlotInt("floor_key_requirement", &SlotOptions::floor_key_requirement);
		registerSlotInt("compass", &SlotOptions::compass);
		registerSlotInt("compass_mode", &SlotOptions::compass_mode);
		registerSlotInt("compass_ping_time", &SlotOptions::compass_ping_time);
		registerSlotInt("compass_recharge", &SlotOptions::compass_recharge);
		registerSlotInt("skill_checks", &SlotOptions::skill_checks);
		registerSlotInt("enemy_randomizer_secret", &SlotOptions::enemy_randomizer_secret);
		registerSlotInt("skill_check_max_rank", &SlotOptions::skill_check_max_rank);
		registerSlotInt("lock_class", &SlotOptions::lock_class);
		registerSlotInt("champion_floors", &SlotOptions::champion_floors);
		registerSlotInt("champion_locks_stairs", &SlotOptions::champion_locks_stairs);
		registerSlotInt("champion_checks", &SlotOptions::champion_checks);
		registerSlotInt("progressive_lantern", &SlotOptions::progressive_lantern);
		registerSlotInt("lantern_darkness", &SlotOptions::lantern_darkness);
		registerSlotInt("death_markers", &SlotOptions::death_markers);
		registerSlotInt("death_marker_gold", &SlotOptions::death_marker_gold);
		registerSlotInt("merchant_caravan", &SlotOptions::merchant_caravan);
		registerSlotInt("caravan_chance", &SlotOptions::caravan_chance);
		registerSlotInt("caravan_checks", &SlotOptions::caravan_checks);
		registerSlotInt("caravan_hints", &SlotOptions::caravan_hints);
		AP_RegisterSlotDataRawCallback("hint_locations", [](std::string raw) {
			std::vector<int64_t> ids;
			const char* c = raw.c_str();
			while ( *c )
			{
				if ( isdigit((unsigned char)*c) ) { char* end; ids.push_back(strtoll(c, &end, 10)); c = end; }
				else { ++c; }
			}
			std::lock_guard<std::mutex> lock(g_netMutex);
			g_slotIncoming.hint_locations = ids;
		});
		registerSlotInt("bounty_enemies", &SlotOptions::bounty_enemies);
		registerSlotInt("bounty_chance", &SlotOptions::bounty_chance);
		registerSlotInt("bounty_checks", &SlotOptions::bounty_checks);
		registerSlotInt("spell_sanity", &SlotOptions::spell_sanity);
		registerSlotInt("spell_upgrades", &SlotOptions::spell_upgrades);
		registerSlotInt("spellbook_checks", &SlotOptions::spellbook_checks);
		registerSlotInt("spell_dlc", &SlotOptions::spell_dlc);
		registerSlotInt("floor_size_mask", &SlotOptions::floor_size_mask);
		registerSlotInt("music_mods", &SlotOptions::music_mods);
		registerSlotInt("randomize_loot", &SlotOptions::randomize_loot);
		registerSlotInt("floor_key_on_tracker_map", &SlotOptions::floor_key_on_tracker_map);
		registerSlotInt("boss_rush_mask", &SlotOptions::boss_rush_mask);
		registerSlotInt("dungeon_floors", &SlotOptions::dungeon_floors);
		registerSlotInt("boss_rush_town", &SlotOptions::boss_rush_town);
		registerSlotInt("boss_rush_order", &SlotOptions::boss_rush_order);
		registerSlotInt("boss_rush_seed", &SlotOptions::boss_rush_seed);
		registerSlotInt("extra_monsters", &SlotOptions::extra_monsters);
		registerSlotInt("floor_shuffle", &SlotOptions::floor_shuffle);
		registerSlotInt("floor_shuffle_seed", &SlotOptions::floor_shuffle_seed);
		registerSlotInt("difficulty_scaling", &SlotOptions::difficulty_scaling);
		registerSlotInt("difficulty", &SlotOptions::difficulty);
		registerSlotInt("lives", &SlotOptions::lives);
		registerSlotInt("death_penalty", &SlotOptions::death_penalty);
		registerSlotInt("boss_death", &SlotOptions::boss_death);
		registerSlotInt("floor_restart", &SlotOptions::floor_restart);
		registerSlotInt("enemy_randomizer", &SlotOptions::enemy_randomizer);
		registerSlotInt("enemy_pool", &SlotOptions::enemy_pool);
		registerSlotInt("enemy_seed", &SlotOptions::enemy_seed);
		registerSlotInt("music_shuffle", &SlotOptions::music_shuffle);
		registerSlotInt("music_seed", &SlotOptions::music_seed);
		registerSlotInt("checkpoint_interval", &SlotOptions::checkpoint_interval);
		registerSlotInt("layout_seed", &SlotOptions::layout_seed);
		registerSlotInt("enemy_level_scaling", &SlotOptions::enemy_level_scaling);
		registerSlotInt("boss_level_scaling", &SlotOptions::boss_level_scaling);
		registerSlotInt("polymorph_trap_duration", &SlotOptions::polymorph_trap_duration);
		registerSlotInt("blindness_trap_duration", &SlotOptions::blindness_trap_duration);
		registerSlotInt("minotaur_time", &SlotOptions::minotaur_time);
		registerSlotInt("death_items", &SlotOptions::death_items);
		registerSlotInt("death_link_mode", &SlotOptions::death_link_mode);
		registerSlotInt("players", &SlotOptions::players);

		AP_Start();
		g_started = true;
		say(COLOR_AP, "Archipelago: connecting to %s as %s...", server.c_str(), slot.c_str());
	}

	void autoConnectFromConfig()
	{
		g_triedAutoConnect = true;
		std::ifstream in("archipelago.cfg");
		if ( !in ) { return; }
		std::string line, server, slot, password;
		while ( std::getline(in, line) )
		{
			if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
			auto eq = line.find('=');
			if ( eq == std::string::npos || line[0] == '#' ) { continue; }
			std::string key = line.substr(0, eq), value = line.substr(eq + 1);
			if ( key == "server" ) { server = value; }
			else if ( key == "slot" ) { slot = value; }
			else if ( key == "password" ) { password = value; }
		}
		if ( !server.empty() && !slot.empty() )
		{
			connect(server, slot, password);
		}
	}

	// Becomes ready once the server has authenticated us and sent slot data.
	void checkReady()
	{
		if ( g_ready || !g_started ) { return; }
		if ( AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated ) { return; }
		{
			std::lock_guard<std::mutex> lock(g_netMutex);
			if ( !g_slotIncomingReady ) { return; }
			g_slot = g_slotIncoming;
		}
		AP_RoomInfo info;
		AP_GetRoomInfo(&info);
		std::filesystem::create_directories("archipelago");
		g_savePath = "archipelago/" + sanitize(info.seed_name) + "_" + sanitize(g_slotName) + ".txt";
		readSave();
		readCheckpoint();
		g_ready = true;
		g_lastCharLevelSent = 1;
		launchTracker();
		say(COLOR_AP, "Archipelago: connected. Goal: %s.",
			g_slot.goal == 0 ? "defeat Baron Herx" : g_slot.goal == 1 ? "defeat the Devil"
			: g_slot.goal == 3 ? "Boss Rush" : g_slot.goal == 4 ? "defeat Baron Herx, the Devil and the twin liches"
			: "defeat the twin liches");
	}

	// ----------------------------------------------------------- items
	const char* itemName(int64_t item)
	{
		using namespace ap_ids;
		switch ( item )
		{
			case ITEM_STAIR_KEY: return "Progressive Stair Key";
			case ITEM_EXTRA_LIFE: return "Extra Life";
			case ITEM_PERM_STR: return "Permanent Strength";
			case ITEM_PERM_DEX: return "Permanent Dexterity";
			case ITEM_PERM_CON: return "Permanent Constitution";
			case ITEM_PERM_INT: return "Permanent Intelligence";
			case ITEM_PERM_PER: return "Permanent Perception";
			case ITEM_PERM_CHR: return "Permanent Charisma";
			case ITEM_MAX_HP_UP: return "Max HP Up";
			case ITEM_START_GOLD: return "Starting Gold Bonus";
			case ITEM_COMPASS: return "Progressive Compass";
			case ITEM_XP_BOOST: return "XP Boost";
			case ITEM_LANTERN: return "Progressive Lantern";
			case ITEM_GOLD_POUCH: return "Gold Pouch";
			case ITEM_HEAL_POTION: return "Healing Potion";
			case ITEM_FOOD: return "Food Ration";
			case ITEM_RANDOM_ITEM: return "Random Item";
			case ITEM_TRAP_MINOTAUR: return "Minotaur Trap";
			case ITEM_TRAP_AMBUSH: return "Ambush Trap";
			case ITEM_TRAP_POLYMORPH: return "Polymorph Trap";
			case ITEM_TRAP_FUMBLE: return "Fumble Trap";
			case ITEM_TRAP_BLIND: return "Blindness Trap";
			case ITEM_TRAP_TELEPORT: return "Teleport Trap";
			case ITEM_TRAP_HUNGER: return "Hunger Trap";
			case ITEM_TRAP_DARKNESS: return "Darkness Trap";
			case ITEM_TRAP_CURSE: return "Curse Trap";
			default: return "Unknown Item";
		}
	}

	void giveItem(int player, ItemType type, Status status, Sint16 beatitude, bool identified)
	{
		Item* item = newItem(type, status, beatitude, 1, local_rng.rand(), identified, nullptr);
		if ( !item ) { return; }
		Item* stack = itemPickup(player, item);
		if ( stack && players[player]->isLocalPlayer() )
		{
			free(item); // itemPickup copied it into the inventory
		}
	}

	void applyPermanent(int player, int64_t item, int count)
	{
		using namespace ap_ids;
		Stat* s = stats[player];
		if ( count <= 0 || !s ) { return; }
		switch ( item )
		{
			case ITEM_PERM_STR: s->STR += count; break;
			case ITEM_PERM_DEX: s->DEX += count; break;
			case ITEM_PERM_CON: s->CON += count; break;
			case ITEM_PERM_INT: s->INT += count; break;
			case ITEM_PERM_PER: s->PER += count; break;
			case ITEM_PERM_CHR: s->CHR += count; break;
			case ITEM_MAX_HP_UP: s->MAXHP += 10 * count; s->HP += 10 * count; break;
			case ITEM_START_GOLD: s->GOLD += 100 * count; break;
			default: break;
		}
	}

	bool isPermanent(int64_t item)
	{
		using namespace ap_ids;
		return item == ITEM_PERM_STR || item == ITEM_PERM_DEX || item == ITEM_PERM_CON
			|| item == ITEM_PERM_INT || item == ITEM_PERM_PER || item == ITEM_PERM_CHR
			|| item == ITEM_MAX_HP_UP || item == ITEM_START_GOLD;
	}

	// Biome -> first normal floor it appears on in vanilla levels.txt.
	int biomeNaturalFloor()
	{
		static const std::pair<const char*, int> biomes[] = {
			{ "The Mines", 1 }, { "The Swamp", 6 }, { "The Labyrinth", 11 }, { "The Ruins", 16 },
			{ "Hell", 21 }, { "Caves", 26 }, { "Citadel", 31 },
		};
		for ( auto& b : biomes )
		{
			if ( !strncmp(map.name, b.first, strlen(b.first)) ) { return b.second; }
		}
		return currentlevel;
	}

	Monster ambushMonsterForDepth(int depth)
	{
		static const Monster tier1[] = { RAT, SKELETON, SPIDER };
		static const Monster tier2[] = { GOBLIN, SLIME, GHOUL, TROLL };
		static const Monster tier3[] = { SCORPION, SCARAB, INSECTOID, TROLL };
		static const Monster tier4[] = { GNOME, BUGBEAR, GOBLIN };
		static const Monster tier5[] = { CREATURE_IMP, DEMON, GOATMAN, SUCCUBUS };
		static const Monster tier6[] = { KOBOLD, INSECTOID, AUTOMATON, GOATMAN };
		auto pick = [](const Monster* list, size_t n) { return list[local_rng.rand() % n]; };
		if ( depth <= 4 ) { return pick(tier1, 3); }
		if ( depth <= 9 ) { return pick(tier2, 4); }
		if ( depth <= 14 ) { return pick(tier3, 4); }
		if ( depth <= 19 ) { return pick(tier4, 3); }
		if ( depth <= 25 ) { return pick(tier5, 4); }
		return pick(tier6, 4);
	}

	bool currentFloorIsNormalGenerated()
	{
		return g_generatedFloorKey && g_generatedLevel == currentlevel && g_generatedSecret == (int)secretlevel && !secretlevel;
	}

	// Start the Minotaur's usual timer on this floor, or queue it for the next normal floor.
	void triggerMinotaur()
	{
		Entity* playerEntity = players[clientnum]->entity;
		if ( currentFloorIsNormalGenerated() && !minotaurlevel && playerEntity )
		{
			minotaurlevel = 1;
			createMinotaurTimer(playerEntity, &map, local_rng.rand());
			int speech = local_rng.rand() % 3;
			playSoundPlayer(clientnum, 123 + speech, 128);
			messageLocalPlayersColor(makeColorRGB(255, 128, 0), MESSAGE_WORLD, Language::get(537));
			messageLocalPlayersColor(makeColorRGB(255, 128, 0), MESSAGE_WORLD, Language::get(74 + speech));
			say(COLOR_TRAP, "Minotaur Trap! Something is coming for you...");
		}
		else
		{
			++g_save.minotaurQueue;
			say(COLOR_TRAP, "Minotaur Trap! It will find you on the next floor.");
		}
	}

	// Traps wait here until they can go off: only while a player is
	// alive and on a floor (not between floors or on the death screen).
	struct PendingTrap { int64_t item; Uint32 fireAt; bool warned3; };
	std::vector<PendingTrap> g_pendingTraps;

	const char* trapName(int64_t item);

	// Returns false when the trap can't go off right now and should wait.
	bool fireTrap(int64_t item)
	{
		using namespace ap_ids;
		if ( item == ITEM_TRAP_MINOTAUR ) { triggerMinotaur(); return true; }
		if ( item == ITEM_TRAP_DARKNESS )
		{
			++g_save.darknessQueue;
			say(COLOR_TRAP, "Darkness Trap! The next floor will be dark.");
			return true;
		}

		int p = randomActivePlayer();
		if ( p < 0 ) { return false; }
		Entity* ent = players[p]->entity;
		Stat* s = stats[p];
		if ( !ent || !s ) { return false; }

		switch ( item )
		{
			case ITEM_TRAP_AMBUSH:
			{
				int count = 2 + local_rng.rand() % 2;
				for ( int i = 0; i < count; ++i )
				{
					summonMonster(ambushMonsterForDepth(currentlevel), (long)ent->x, (long)ent->y);
				}
				say(COLOR_TRAP, "Ambush Trap!");
				break;
			}
			case ITEM_TRAP_POLYMORPH:
				spellEffectPolymorph(ent, ent, true, TICKS_PER_SECOND * std::max(5, g_slot.polymorph_trap_duration));
				say(COLOR_TRAP, "Polymorph Trap!");
				break;
			case ITEM_TRAP_FUMBLE:
				if ( s->weapon )
				{
					dropItem(s->weapon, p);
					say(COLOR_TRAP, "Fumble Trap! You dropped your weapon.");
				}
				else
				{
					say(COLOR_TRAP, "Fumble Trap! ...but your hands were empty.");
				}
				break;
			case ITEM_TRAP_BLIND:
				ent->setEffect(EFF_BLIND, true, TICKS_PER_SECOND * std::max(5, g_slot.blindness_trap_duration), true);
				say(COLOR_TRAP, "Blindness Trap!");
				break;
			case ITEM_TRAP_TELEPORT:
				ent->teleportRandom();
				say(COLOR_TRAP, "Teleport Trap!");
				break;
			case ITEM_TRAP_HUNGER:
				s->HUNGER = std::min(s->HUNGER, 200);
				say(COLOR_TRAP, "Hunger Trap! You're starving.");
				break;
			case ITEM_TRAP_CURSE:
			{
				Item* slots[] = { s->weapon, s->shield, s->helmet, s->breastplate, s->gloves,
					s->shoes, s->cloak, s->amulet, s->ring, s->mask };
				std::vector<Item*> worn;
				for ( Item* it : slots ) { if ( it ) { worn.push_back(it); } }
				if ( !worn.empty() )
				{
					Item* it = worn[local_rng.rand() % worn.size()];
					it->beatitude = -std::max<Sint16>(1, (Sint16)std::abs(it->beatitude));
					say(COLOR_TRAP, "Curse Trap! Your %s feels wrong.", it->getName());
				}
				else
				{
					say(COLOR_TRAP, "Curse Trap! ...but you wear nothing to curse.");
				}
				break;
			}
			default: break;
		}
		return true;
	}

	const char* trapName(int64_t item)
	{
		using namespace ap_ids;
		switch ( item )
		{
			case ITEM_TRAP_MINOTAUR: return "Minotaur Trap";
			case ITEM_TRAP_AMBUSH: return "Ambush Trap";
			case ITEM_TRAP_POLYMORPH: return "Polymorph Trap";
			case ITEM_TRAP_FUMBLE: return "Fumble Trap";
			case ITEM_TRAP_BLIND: return "Blindness Trap";
			case ITEM_TRAP_TELEPORT: return "Teleport Trap";
			case ITEM_TRAP_HUNGER: return "Hunger Trap";
			case ITEM_TRAP_DARKNESS: return "Darkness Trap";
			case ITEM_TRAP_CURSE: return "Curse Trap";
			default: return "Trap";
		}
	}

	void applyTrap(int64_t item)
	{
		g_pendingTraps.push_back(PendingTrap{ item, 0, true });
	}

	void updatePendingTraps()
	{
		if ( g_pendingTraps.empty() || g_deathScreen || g_restartPending || loadnextlevel ) { return; }
		for ( size_t i = 0; i < g_pendingTraps.size(); )
		{
			PendingTrap& t = g_pendingTraps[i];
			if ( t.fireAt != 0 && ticks < t.fireAt )
			{
				if ( !t.warned3 && t.fireAt - ticks <= (Uint32)(3 * TICKS_PER_SECOND) )
				{
					t.warned3 = true;
					say(COLOR_TRAP, "%s in 3...", trapName(t.item));
				}
				++i;
				continue;
			}
			if ( fireTrap(t.item) ) { g_pendingTraps.erase(g_pendingTraps.begin() + i); }
			else { ++i; }
		}
	}

	// A newly received item, applied once.
	void applyNewItem(int64_t item)
	{
		using namespace ap_ids;
		if ( item >= ITEM_TRAP_MINOTAUR && item <= ITEM_TRAP_CURSE )
		{
			applyTrap(item);
			return;
		}
		say(COLOR_AP, "Received %s.", itemName(item));
		for ( int p : activePlayers() )
		{
			Stat* s = stats[p];
			switch ( item )
			{
				case ITEM_XP_BOOST: s->EXP += 50; break;
				case ITEM_GOLD_POUCH: s->GOLD += 25 + 10 * std::max(1, currentlevel); break;
				case ITEM_HEAL_POTION: giveItem(p, POTION_HEALING, EXCELLENT, 0, true); break;
				case ITEM_FOOD: giveItem(p, FOOD_BREAD, EXCELLENT, 0, true); break;
				case ITEM_RANDOM_ITEM:
				{
					int cat = local_rng.rand() % (Category::CATEGORY_MAX - 2);
					ItemType type = itemLevelCurve(static_cast<Category>(cat), 0, currentlevel + 5, local_rng);
					giveItem(p, type, static_cast<Status>(WORN + local_rng.rand() % 3), 0, false);
					break;
				}
				default:
					break; // permanent items are handled by syncPermanents()
			}
		}
		if ( item == ITEM_LANTERN )
		{
			say(COLOR_AP, "Your lantern burns brighter (%d / 3).", std::min(3, countReceived(ITEM_LANTERN)));
		}
		if ( item == ITEM_EXTRA_LIFE )
		{
			++g_save.livesLeft;
			say(COLOR_AP, "Lives: %d", g_save.livesLeft);
		}
	}

	// Called once per character: permanent items and starting lives.
	void initRun()
	{
		endDeathScreen();
		clearFloorUids(); // a new game numbers its entities from 1 again
		warnSkillClass();
		g_compassGivePending = true;
		g_save.runKey = uniqueGameKey;
		startRunRecord();
		g_hofAutoUntil = ticks + TICKS_PER_SECOND * 20; // the Hall of Fame shows in the starting area for a bit
		g_save.livesLeft = g_slot.lives;
		g_savedBossHP.clear();
		g_savedBossFloor = -1;
		g_lastCharLevelSent = 1;
		for ( int i = 0; i < 8; ++i ) { g_save.permApplied[i] = 0; } // a fresh character has none yet
		if ( g_slot.difficulty == 1 )
		{
			svFlags |= SV_FLAG_HARDCORE;
		}
		if ( g_save.livesLeft > 0 )
		{
			say(COLOR_AP, "New run. Lives: %d", g_save.livesLeft);
		}
		g_carryPickups = 0;
		g_floorApUnfound = 0;
		writeSave();
	}

	// Move items from the network thread into g_received.
	void pullInbox()
	{
		std::vector<int64_t> incoming;
		bool clear = false;
		{
			std::lock_guard<std::mutex> lock(g_netMutex);
			clear = g_inboxClearRequested;
			g_inboxClearRequested = false;
			incoming.swap(g_inbox);
		}
		if ( clear )
		{
			g_received.clear(); // the server is about to resend everything
		}
		g_received.insert(g_received.end(), incoming.begin(), incoming.end());
	}

	// One-shot items (filler, traps, extra lives) are used once, ever.
	void applyNewItems()
	{
		if ( (int)g_received.size() <= g_save.processedItems ) { return; }
		for ( size_t i = (size_t)g_save.processedItems; i < g_received.size(); ++i )
		{
			if ( isPermanent(g_received[i]) )
			{
				say(COLOR_AP, "Received %s.", itemName(g_received[i]));
			}
			else if ( g_received[i] > ap_ids::BASE_ID + ap_ids::SPELL_ITEM_OFFSET
				&& g_received[i] < ap_ids::BASE_ID + ap_ids::SPELL_ITEM_OFFSET + 1000 )
			{
				int id = (int)(g_received[i] - ap_ids::BASE_ID - ap_ids::SPELL_ITEM_OFFSET);
				int copies = (int)std::count(g_received.begin(), g_received.begin() + i + 1, g_received[i]);
				const char* name = "a spell";
				for ( auto& sp : ap_ids::SPELLS ) { if ( sp.spell == id ) { name = sp.name; } }
				if ( copies <= 1 ) { say(COLOR_AP, "Received the %s spell.", name); }
				else { say(COLOR_AP, "Received %s: it's now +%d.", name, copies - 1); }
				syncSpells();
			}
			else if ( g_received[i] == ap_ids::ITEM_COMPASS )
			{
				say(COLOR_AP, "Received a Progressive Compass (%d).", (int)std::count(g_received.begin(), g_received.begin() + i + 1, g_received[i]));
				giveCompassIfMissing();
			}
			else if ( g_received[i] != ap_ids::ITEM_STAIR_KEY ) // Stair Keys aren't placed any more
			{
				applyNewItem(g_received[i]);
			}
		}
		g_save.processedItems = (int)g_received.size();
		writeSave();
	}

	// Permanent items apply to every character: give the current one whatever it's missing.
	void syncPermanents()
	{
		bool dirty = false;
		for ( int i = 0; i < 8; ++i )
		{
			int have = countReceived(PERMANENT_ITEMS[i]);
			int missing = have - g_save.permApplied[i];
			if ( missing > 0 )
			{
				for ( int p : activePlayers() )
				{
					applyPermanent(p, PERMANENT_ITEMS[i], missing);
				}
				g_save.permApplied[i] = have;
				dirty = true;
			}
		}
		if ( dirty ) { writeSave(); }
	}

	// ----------------------------------------------------------- Spell Sanity
	const ap_ids::SpellEntry* sanitySpell(int spellID)
	{
		for ( auto& sp : ap_ids::SPELLS )
		{
			if ( sp.spell == spellID ) { return (!sp.dlc || g_slot.spell_dlc) ? &sp : nullptr; }
		}
		return nullptr;
	}

	int spellCopies(int spellID) { return countReceived(ap_ids::BASE_ID + ap_ids::SPELL_ITEM_OFFSET + spellID); }

	bool knowsSpell(int player, int spellID)
	{
		if ( !players[player] ) { return false; }
		for ( node_t* n = players[player]->magic.spellList.first; n; n = n->next )
		{
			spell_t* sp = (spell_t*)n->element;
			if ( sp && sp->ID == spellID ) { return true; }
		}
		return false;
	}

	bool ownsAnyDlc() { return enabledDLCPack1 || enabledDLCPack2 || enabledDLCPack3; }

	// Every spell received is known by the current character (taught again to each new one).
	void syncSpells()
	{
		if ( !g_slot.spell_sanity || !inGame() ) { return; }
		static bool warnedDlc = false;
		for ( auto& sp : ap_ids::SPELLS )
		{
			if ( (sp.dlc && !g_slot.spell_dlc) || spellCopies(sp.spell) <= 0 ) { continue; }
			if ( sp.dlc && !ownsAnyDlc() )
			{
				if ( !warnedDlc ) { warnedDlc = true; say(COLOR_AP, "DLC spells you receive need a Barony DLC to be learned."); }
				continue;
			}
			for ( int p : activePlayers() )
			{
				if ( !knowsSpell(p, sp.spell) ) { addSpell(sp.spell, p, true); }
			}
		}
	}

	// ----------------------------------------------------------- checks
	void sendFloorChecks()
	{
		int depth = std::min(currentlevel, g_slot.goal_floor);
		int interval = std::max(1, g_slot.floor_check_interval);
		for ( int f = interval; f <= depth; f += interval )
		{
			sendLocation(ap_ids::BASE_ID + ap_ids::FLOOR_OFFSET + f);
		}
	}

	void sendSpecialAreaCheck()
	{
		if ( !g_slot.special_area_checks || !secretlevel ) { return; }
		for ( auto& area : ap_ids::SPECIAL_AREAS )
		{
			size_t n = strlen(area.match);
			if ( !strncmp(map.filename, area.match, n) || !strncmp(map.name, area.match, n) )
			{
				sendLocation(ap_ids::BASE_ID + ap_ids::SPECIAL_OFFSET + area.index);
			}
		}
	}

	// Skill checks: which skills count, from what value, for the class chosen in the YAML.
	// Returns the starting value (0 for a universal skill), or -1 if the skill isn't a check.
	int skillStart(int skill)
	{
		int sc = g_slot.skill_checks;
		if ( sc <= 0 || skill < 0 || skill >= ap_ids::NUM_SKILLS ) { return -1; }
		int start = -1;
		if ( sc >= 2 && sc - 2 < ap_ids::NUM_SKILL_CLASSES ) { start = ap_ids::CLASS_SKILL_START[sc - 2][skill]; }
		if ( start < 0 && ap_ids::SKILL_UNIVERSAL[skill] ) { start = 0; }
		return start;
	}

	void sendSkillChecks()
	{
		if ( g_slot.skill_checks <= 0 || !stats[clientnum] ) { return; }
		for ( int skill = 0; skill < ap_ids::NUM_SKILLS; ++skill )
		{
			int start = skillStart(skill);
			if ( start < 0 ) { continue; }
			int have = stats[clientnum]->getProficiency(skill);
			for ( int rank = 1; rank <= std::min(5, g_slot.skill_check_max_rank); ++rank )
			{
				int value = ap_ids::SKILL_RANK_VALUES[rank];
				if ( value <= start || have < value ) { continue; }
				int64_t loc = ap_ids::BASE_ID + ap_ids::SKILL_OFFSET + skill * 5 + rank;
				if ( !isChecked(loc) )
				{
					sendLocation(loc);
					say(COLOR_AP, "Skill check: %s %s!", ap_ids::SKILL_RANK_NAMES[rank], ap_ids::SKILL_NAMES[skill]);
				}
			}
		}
	}

	void warnSkillClass()
	{
		int sc = g_slot.skill_checks;
		if ( sc < 2 || !stats[clientnum] ) { return; }
		int chosen = sc - 2;
		if ( client_classes[clientnum] != chosen && chosen < ap_ids::NUM_SKILL_CLASSES )
		{
			say(COLOR_TRAP, "Skill Checks were made for a %s. Other classes can still raise those skills, but some checks may be much harder.",
				ap_ids::CLASS_NAMES[chosen]);
		}
	}

	void sendLevelChecks()
	{
		int lvl = std::min<int>(stats[clientnum]->LVL, g_slot.level_checks);
		while ( g_lastCharLevelSent < lvl )
		{
			++g_lastCharLevelSent;
			if ( g_lastCharLevelSent >= 2 )
			{
				sendLocation(ap_ids::BASE_ID + ap_ids::LEVEL_OFFSET + g_lastCharLevelSent);
			}
		}
	}

	// Marked enemies (Enemy Sanity: Marked Enemies): their uids, and how many this floor has.
	std::set<Uint32> g_markedEnemies;
	int g_markedThisFloor = 0;

	// Enemies killed on this floor, across restarts. A restarted floor comes back with that many fewer,
	// so dying and redoing a floor can't be used to farm XP.
	int g_floorKills = 0;

	bool isRemovableEnemy(Entity* e)
	{
		if ( !e || e->behavior != &actMonster ) { return false; }
		Stat* st = e->getStats();
		if ( !st ) { return false; }
		int t = st->type;
		if ( t == SHOPKEEPER || t == MINOTAUR || t == LICH || t == DEVIL || t == LICH_ICE || t == LICH_FIRE || t == NOTHING )
		{
			return false;
		}
		if ( st->leader_uid != 0 || e->monsterAllySummonRank != 0 ) { return false; } // allies and followers
		return true;
	}

	void removeKilledEnemies()
	{
		if ( g_floorKills <= 0 || !map.creatures ) { return; }
		std::vector<Entity*> candidates;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isRemovableEnemy(e) ) { candidates.push_back(e); }
		}
		int removed = 0;
		for ( Entity* e : candidates )
		{
			if ( removed >= g_floorKills ) { break; }
			list_RemoveNode(e->mynode);
			++removed;
		}
		printlog("[Archipelago] Floor restart: removed %d of %d enemies already killed here.", removed, g_floorKills);
	}

	// Death Items: Keep Everything. Items you picked up and chests you emptied on this floor stay
	// gone when it restarts (you're still carrying them), so a restart can't be used to farm items.
	std::set<Uint32> g_floorItemUids, g_floorChestUids;
	int g_floorItemsTaken = 0, g_floorChestsLooted = 0;

	bool isPlainGroundItem(Entity* e)
	{
		return e && e->behavior == &actItem && e->skill[10] != AP_ITEM_TYPE;
	}

	list_t* chestList(Entity* e)
	{
		if ( !e || e->behavior != &actChest || !e->children.first ) { return nullptr; }
		return (list_t*)e->children.first->element;
	}

	bool chestHasPlainItems(list_t* inv)
	{
		if ( !inv ) { return false; }
		for ( node_t* n = inv->first; n; n = n->next )
		{
			Item* it = (Item*)n->element;
			if ( it && !isApItem(it) ) { return true; }
		}
		return false;
	}

	void rememberFloorLoot()
	{
		g_floorItemUids.clear();
		g_floorChestUids.clear();
		if ( !map.entities ) { return; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isPlainGroundItem(e) ) { g_floorItemUids.insert(e->getUID()); }
			else if ( chestHasPlainItems(chestList(e)) ) { g_floorChestUids.insert(e->getUID()); }
		}
	}

	// Called when a life is lost: how much of this floor's loot is gone since it started.
	void countTakenLoot()
	{
		std::set<Uint32> itemsLeft, chestsFull;
		if ( map.entities )
		{
			for ( node_t* node = map.entities->first; node; node = node->next )
			{
				Entity* e = (Entity*)node->element;
				if ( isPlainGroundItem(e) ) { itemsLeft.insert(e->getUID()); }
				else if ( chestHasPlainItems(chestList(e)) ) { chestsFull.insert(e->getUID()); }
			}
		}
		for ( Uint32 uid : g_floorItemUids ) { if ( !itemsLeft.count(uid) ) { ++g_floorItemsTaken; } }
		for ( Uint32 uid : g_floorChestUids ) { if ( !chestsFull.count(uid) ) { ++g_floorChestsLooted; } }
		g_floorItemUids.clear();
		g_floorChestUids.clear();
	}

	void removeTakenLoot()
	{
		if ( !map.entities ) { return; }
		std::vector<Entity*> items, chests;
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isPlainGroundItem(e) ) { items.push_back(e); }
			else if ( chestHasPlainItems(chestList(e)) ) { chests.push_back(e); }
		}
		int removedItems = 0, emptiedChests = 0;
		for ( Entity* e : items )
		{
			if ( removedItems >= g_floorItemsTaken ) { break; }
			list_RemoveNode(e->mynode);
			++removedItems;
		}
		for ( Entity* e : chests )
		{
			if ( emptiedChests >= g_floorChestsLooted ) { break; }
			list_t* inv = chestList(e);
			for ( node_t* n = inv->first; n; )
			{
				node_t* next = n->next;
				Item* it = (Item*)n->element;
				if ( it && !isApItem(it) ) { list_RemoveNode(n); }
				n = next;
			}
			++emptiedChests;
		}
		printlog("[Archipelago] Floor restart: removed %d items and emptied %d chests you already took.", removedItems, emptiedChests);
	}

	void sendKillChecks()
	{
		if ( g_slot.enemy_sanity != 1 ) { return; }
		int interval = std::max(1, g_slot.kill_check_interval);
		for ( int k = 1; k <= g_slot.kill_checks; ++k )
		{
			if ( g_save.greenKills >= (long)k * interval )
			{
				sendLocation(ap_ids::BASE_ID + ap_ids::KILL_OFFSET + k);
			}
		}
	}

	int nextUnchecked(int64_t offset, int count)
	{
		for ( int i = 1; i <= count; ++i )
		{
			if ( !isChecked(ap_ids::BASE_ID + offset + i) ) { return i; }
		}
		return 0;
	}

	int uncheckedCount(int64_t offset, int count)
	{
		int n = 0;
		for ( int i = 1; i <= count; ++i )
		{
			if ( !isChecked(ap_ids::BASE_ID + offset + i) ) { ++n; }
		}
		return n;
	}

	// Archipelago items that ended up in an inventory (picked up or bought) become checks.
	void scanInventories()
	{
		for ( int p = 0; p < MAXPLAYERS; ++p )
		{
			if ( client_disconnected[p] || !stats[p] ) { continue; }
			node_t* next = nullptr;
			for ( node_t* node = stats[p]->inventory.first; node; node = next )
			{
				next = node->next;
				Item* item = (Item*)node->element;
				if ( !item || item->type != AP_ITEM_TYPE ) { continue; }
				bool floor = item->appearance == AP_FLOOR_APPEARANCE;
				bool shop = item->appearance == AP_SHOP_APPEARANCE;
				bool caravan = item->appearance == AP_CARAVAN_APPEARANCE;
				bool hint = item->appearance == AP_HINT_APPEARANCE;
				if ( !floor && !shop && !caravan && !hint ) { continue; }
				if ( itemIsEquipped(item, p) ) { continue; }
				if ( hint || caravan )
				{
					for ( int n = 0; n < std::max(1, (int)item->count); ++n )
					{
						if ( hint ) { buyHint(); continue; }
						int k = nextUnchecked(ap_ids::CARAVAN_OFFSET, g_slot.caravan_checks);
						if ( k > 0 )
						{
							sendLocation(ap_ids::BASE_ID + ap_ids::CARAVAN_OFFSET + k);
							say(COLOR_AP, "Bought an Archipelago item! (Caravan Purchase %d)", k);
						}
						else { say(COLOR_AP, "This Archipelago item was already found."); }
					}
					list_RemoveNode(node);
					continue;
				}

				if ( floor ) { ++g_floorApFound; }
				int index = floor ? nextUnchecked(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups)
					: nextUnchecked(ap_ids::SHOP_OFFSET, g_slot.shop_checks);
				if ( index > 0 )
				{
					sendLocation(ap_ids::BASE_ID + (floor ? ap_ids::PICKUP_OFFSET : ap_ids::SHOP_OFFSET) + index);
					say(COLOR_AP, "Found an Archipelago item! (%s %d)", floor ? "Pickup" : "Shop Item", index);
				}
				else
				{
					say(COLOR_AP, "This Archipelago item was already found.");
				}
				list_RemoveNode(node);
			}
		}
	}

	bool findFloorSpot(int& outX, int& outY)
	{
		if ( !map.tiles || map.width <= 0 || map.height <= 0 ) { return false; }
		Entity* pl = players[clientnum]->entity;
		for ( int tries = 0; tries < 400; ++tries )
		{
			int x = local_rng.rand() % map.width;
			int y = local_rng.rand() % map.height;
			int index = y * MAPLAYERS + x * MAPLAYERS * map.height;
			if ( !map.tiles[index] ) { continue; }                    // no floor
			int floorTile = map.tiles[index];
			if ( (swimmingtiles && swimmingtiles[floorTile]) || (lavatiles && lavatiles[floorTile]) ) { continue; } // water, lava
			if ( map.tiles[OBSTACLELAYER + index] ) { continue; }     // wall
			if ( shoparea && shoparea[y + x * map.height] ) { continue; }
			if ( checkObstacle((x << 4) + 8, (y << 4) + 8, nullptr, nullptr) ) { continue; }
			if ( pl )
			{
				int dx = x - (int)(pl->x / 16), dy = y - (int)(pl->y / 16);
				if ( dx * dx + dy * dy < 25 ) { continue; }          // not right on top of the player
			}
			outX = x; outY = y;
			return true;
		}
		return false;
	}

	// ----------------------------------------------------------- Floor Keys
	// Every floor from floor 1 has a key somewhere on it; the stairs stay locked until it's found.
	// It's part of this game only: never an Archipelago item.
	constexpr ItemType FLOOR_KEY_TYPE = TOOL_SKELETONKEY;
	constexpr Uint32 FLOOR_KEY_APPEARANCE = 0x464B4559; // "FKEY"
	bool g_floorKeyFound = false;
	bool g_floorNeedsKey = false;
	std::string g_keyCompass; // on Minotaur floors near the end: "18 tiles NE" (empty otherwise)

	bool isFloorKeyEntity(Entity* e)
	{
		return e && e->behavior == &actItem && e->skill[10] == FLOOR_KEY_TYPE && (Uint32)e->skill[14] == FLOOR_KEY_APPEARANCE;
	}

	bool isSpecialMap()
	{
		static const char* const SKIP[] = { "Boss", "Hell Boss", "Sanctum", "Hamlet", "Tutorial" };
		for ( const char* name : SKIP )
		{
			if ( !strncmp(map.name, name, strlen(name)) ) { return true; }
		}
		return false;
	}

	bool floorUsesKey()
	{
		return g_slot.floor_keys && currentlevel >= 1 && !secretlevel && !isSpecialMap();
	}

	// A spot the player can walk to from where they stand (doors count as open; walls, chasms and lava
	// don't), preferring ones far away.
	bool findReachableSpot(int& outX, int& outY, bool nearCenter = false)
	{
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		if ( !pl || !map.tiles || map.width <= 0 || map.height <= 0 ) { return false; }
		const int w = map.width, h = map.height;
		auto walkable = [&](int x, int y) {
			int index = y * MAPLAYERS + x * MAPLAYERS * h;
			if ( !map.tiles[index] ) { return false; }
			if ( lavatiles && lavatiles[map.tiles[index]] ) { return false; }
			return map.tiles[OBSTACLELAYER + index] == 0;
		};
		int sx = (int)(pl->x / 16), sy = (int)(pl->y / 16);
		if ( sx < 0 || sy < 0 || sx >= w || sy >= h ) { return false; }
		// Gates (portcullises) only open with a lever, which may be hidden: treat them as walls, so
		// nothing goes behind one. Doors still count as open (they can be opened, picked or kicked in).
		std::vector<bool> gate(w * h, false);
		if ( map.entities )
		{
			for ( node_t* node = map.entities->first; node; node = node->next )
			{
				Entity* e = (Entity*)node->element;
				if ( !e || e->behavior != &actGate ) { continue; }
				int gx = (int)(e->x / 16), gy = (int)(e->y / 16);
				if ( gx >= 0 && gy >= 0 && gx < w && gy < h ) { gate[gx + gy * w] = true; }
			}
		}
		std::vector<int> dist(w * h, -1);
		std::vector<int> queue;
		queue.push_back(sx + sy * w);
		dist[sx + sy * w] = 0;
		for ( size_t q = 0; q < queue.size(); ++q )
		{
			int cx = queue[q] % w, cy = queue[q] / w;
			const int dx[] = { 1, -1, 0, 0 }, dy[] = { 0, 0, 1, -1 };
			for ( int k = 0; k < 4; ++k )
			{
				int nx = cx + dx[k], ny = cy + dy[k];
				if ( nx < 0 || ny < 0 || nx >= w || ny >= h || dist[nx + ny * w] >= 0 ) { continue; }
				if ( !walkable(nx, ny) || gate[nx + ny * w] ) { continue; }
				dist[nx + ny * w] = dist[queue[q]] + 1;
				queue.push_back(nx + ny * w);
			}
		}
		// Candidates: reachable, dry, not in a shop, nothing in the way. Far ones first.
		std::vector<int> far, near;
		int maxDist = 0;
		for ( int c : queue ) { maxDist = std::max(maxDist, dist[c]); }
		for ( int c : queue )
		{
			int x = c % w, y = c / w;
			int index = y * MAPLAYERS + x * MAPLAYERS * h;
			if ( swimmingtiles && swimmingtiles[map.tiles[index]] ) { continue; }
			if ( shoparea && shoparea[y + x * h] ) { continue; }
			if ( dist[c] < 5 ) { continue; }
			if ( checkObstacle((x << 4) + 8, (y << 4) + 8, nullptr, nullptr) ) { continue; }
			(dist[c] >= maxDist / 2 ? far : near).push_back(c);
		}
		if ( nearCenter )
		{
			// The reachable spot closest to the middle of the map (Boss Rush: the town's way down).
			int best = -1;
			double bestD = 1e18;
			for ( auto* list : { &far, &near } )
			{
				for ( int c : *list )
				{
					double ddx = c % w - w / 2.0, ddy = c / w - h / 2.0;
					if ( ddx * ddx + ddy * ddy < bestD ) { bestD = ddx * ddx + ddy * ddy; best = c; }
				}
			}
			if ( best < 0 ) { return false; }
			outX = best % w; outY = best / w;
			return true;
		}
		std::vector<int>& pick = !far.empty() ? far : near;
		if ( pick.empty() ) { return false; }
		int c = pick[local_rng.rand() % pick.size()];
		outX = c % w; outY = c / w;
		return true;
	}

	void spawnFloorKey()
	{
		int x, y;
		if ( !findReachableSpot(x, y) )
		{
			g_floorKeyFound = true; // nowhere to put it: the stairs stay open
			printlog("[Archipelago] No spot for the Floor Key on this floor; stairs left open.");
			return;
		}
		Entity* entity = newEntity(-1, 1, map.entities, nullptr);
		entity->flags[INVISIBLE] = true;
		entity->flags[UPDATENEEDED] = true;
		entity->x = x * 16 + 8;
		entity->y = y * 16 + 8;
		entity->sizex = 4;
		entity->sizey = 4;
		entity->yaw = (local_rng.rand() % 360) * PI / 180.0;
		entity->vel_z = -.5;
		entity->flags[PASSABLE] = true;
		entity->flags[USERFLAG1] = true;
		entity->behavior = &actItem;
		entity->skill[10] = FLOOR_KEY_TYPE;
		entity->skill[11] = EXCELLENT;
		entity->skill[12] = 0;
		entity->skill[13] = 1;
		entity->skill[14] = FLOOR_KEY_APPEARANCE;
		entity->skill[15] = 1;
	}

	// Key compass: once the Minotaur is 30 seconds away (or here), point at the key.
	// ------------------------------------------------------- Compass (points at the Floor Key)
	constexpr ItemType COMPASS_TYPE = TOOL_MIRROR;
	constexpr Uint32 COMPASS_APPEARANCE = 0x434F4D50; // "COMP"
	Uint32 g_compassPingUntil = 0;     // ping mode: pointing until this tick
	Uint32 g_compassReadyAt = 0;       // ping mode: can be used again at this tick
	bool g_compassShowing = false;     // the compass face is on screen
	bool g_compassShowsDistance = false;
	int g_compassArrow = 0;            // 0 ahead, then clockwise in 45 degree steps
	int g_compassTiles = 0;

	// 0 no compass, 1 direction only, 2 direction and distance.
	int compassLevel()
	{
		switch ( g_slot.compass )
		{
			case 1: return 2;
			case 2: return countReceived(ap_ids::ITEM_COMPASS) > 0 ? 2 : 0;
			case 3: return std::min(2, countReceived(ap_ids::ITEM_COMPASS));
			default: return 0;
		}
	}

	bool isCompassItemPrivate(const Item* it)
	{
		return it && it->type == COMPASS_TYPE && it->appearance == COMPASS_APPEARANCE;
	}

	// ------------------------------------------------------- Randomize Loot
	// Chests, shop stock and items lying on the floor are rerolled. Same Kind keeps the kind (a potion
	// stays a potion, a different one); Anything can turn into any kind of item. Gold, Archipelago items,
	// Floor Keys, the Compass and quest items are never touched.
	const Category LOOT_CATEGORIES[] = { WEAPON, ARMOR, AMULET, POTION, SCROLL, MAGICSTAFF, RING, SPELLBOOK,
		GEM, THROWN, TOOL, FOOD, BOOK };

	bool lootRerollable(ItemType type, Uint32 appearance)
	{
		if ( type < 0 || type >= NUMITEMS ) { return false; }
		if ( type == AP_ITEM_TYPE && (appearance == AP_FLOOR_APPEARANCE || appearance == AP_SHOP_APPEARANCE
			|| appearance == AP_CARAVAN_APPEARANCE || appearance == AP_HINT_APPEARANCE) ) { return false; }
		if ( type == FLOOR_KEY_TYPE && appearance == FLOOR_KEY_APPEARANCE ) { return false; }
		if ( type == COMPASS_TYPE && appearance == COMPASS_APPEARANCE ) { return false; }
		if ( type == ARTIFACT_ORB_BLUE || type == ARTIFACT_ORB_RED || type == ARTIFACT_ORB_PURPLE || type == ARTIFACT_ORB_GREEN ) { return false; }
		if ( type == KEY_IRON || type == KEY_BRONZE || type == KEY_SILVER || type == KEY_GOLD ) { return false; }
		Category c = items[type].category;
		for ( Category ok : LOOT_CATEGORIES ) { if ( ok == c ) { return true; } }
		return false;
	}

	// Picks the new item. Returns false when this one stays as it is.
	bool rerollLoot(ItemType& type, Sint16& count, Uint32& appearance)
	{
		if ( !g_ready || g_slot.randomize_loot == 0 || multiplayer == CLIENT || !lootRerollable(type, appearance) ) { return false; }
		Category cat = items[type].category;
		if ( g_slot.randomize_loot == 2 )
		{
			cat = LOOT_CATEGORIES[local_rng.rand() % (sizeof(LOOT_CATEGORIES) / sizeof(LOOT_CATEGORIES[0]))];
		}
		bool sameKind = cat == items[type].category;
		ItemType picked = itemLevelCurve(cat, 0, std::max(1, currentlevel), local_rng);
		if ( !lootRerollable(picked, 0) ) { return false; }
		type = picked;
		appearance = local_rng.rand();
		if ( type == ENCHANTED_FEATHER ) { appearance = 75 + 25 * (local_rng.rand() % 2); }
		if ( !sameKind ) { count = cat == THROWN ? (Sint16)(3 + local_rng.rand() % 5) : 1; }
		return true;
	}

	void rerollItemList(list_t* inv)
	{
		if ( !inv ) { return; }
		for ( node_t* n = inv->first; n; n = n->next )
		{
			Item* it = (Item*)n->element;
			if ( !it ) { continue; }
			ItemType type = it->type;
			Sint16 count = it->count;
			Uint32 appearance = it->appearance;
			if ( rerollLoot(type, count, appearance) )
			{
				it->type = type;
				it->count = count;
				it->appearance = appearance;
				it->identified = false;
			}
		}
	}

	void rerollGroundLoot()
	{
		if ( !g_ready || g_slot.randomize_loot == 0 || multiplayer == CLIENT || !map.entities ) { return; }
		int changed = 0;
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e || e->behavior != &actItem ) { continue; }
			ItemType type = static_cast<ItemType>(e->skill[10]);
			Sint16 count = (Sint16)e->skill[13];
			Uint32 appearance = (Uint32)e->skill[14];
			if ( !rerollLoot(type, count, appearance) ) { continue; }
			e->skill[10] = type;
			e->skill[13] = count;
			e->skill[14] = (Sint32)appearance;
			e->skill[15] = 0;
			Item* item = newItemFromEntity(e, true);
			if ( item )
			{
				e->sprite = itemModel(item);
				free(item);
			}
			++changed;
		}
		if ( changed ) { printlog("[Archipelago] Randomize Loot: %d items on the floor rerolled.", changed); }
	}

	// Puts the compass in your bag if you should have one and don't.
	void giveCompassIfMissing()
	{
		if ( !g_slot.floor_keys || g_slot.floor_key_requirement == 2 || compassLevel() <= 0 || !stats[clientnum] ) { return; }
		for ( node_t* n = stats[clientnum]->inventory.first; n; n = n->next )
		{
			if ( isCompassItemPrivate((Item*)n->element) ) { return; }
		}
		Item* item = newItem(COMPASS_TYPE, EXCELLENT, 0, 1, COMPASS_APPEARANCE, true, nullptr);
		if ( !item ) { return; }
		Item* stack = itemPickup(clientnum, item);
		if ( stack && players[clientnum]->isLocalPlayer() ) { free(item); }
		say(COLOR_AP, "The Compass is in your bag. Use it to find the Floor Key.");
	}

	// The Compass shows only while it's the selected hotbar item.
	bool compassSelected()
	{
		if ( !players[clientnum] ) { return false; }
		auto& hotbar = players[clientnum]->hotbar;
		int cur = hotbar.current_hotbar;
		if ( cur < 0 || cur >= NUM_HOTBAR_SLOTS ) { return false; }
		Item* it = uidToItem(hotbar.slots()[cur].item);
		return isCompassItemPrivate(it);
	}

	// Backup compass: on Minotaur floors near the end of his timer, and after 5 minutes on any floor.
	bool backupCompassOn(int minotaurSecs)
	{
		// Key Compass Time: how many seconds before he arrives it turns on (at least 30), but never in
		// the first 30 seconds of his timer. On a bigger floor the seconds stretch with it.
		int scale = currentFloorScale();
		int when = std::max(30, g_slot.key_compass_time) * scale / 100;
		int least = 30 * scale / 100;
		bool on = minotaurSecs == -2 || (minotaurSecs >= 0 && minotaurSecs <= when && g_minotaurElapsed >= 30);
		if ( minotaurSecs >= 0 && minotaurSecs <= least ) { on = true; }
		if ( ticks - g_floorStartTicks >= (Uint32)(TICKS_PER_SECOND * 300) ) { on = true; }
		return on;
	}

	void updateKeyCompass(int minotaurSecs)
	{
		g_keyCompass.clear();
		g_compassShowing = false;
		if ( !g_floorNeedsKey || g_floorKeyFound || g_slot.floor_key_requirement == 2 ) { return; }
		bool backup = backupCompassOn(minotaurSecs);
		int level = compassLevel();
		bool owned = level > 0;
		bool fromCompass = owned && compassSelected() && (g_slot.compass_mode == 1 || ticks < g_compassPingUntil);
		if ( !backup && !fromCompass ) { return; }
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		if ( !pl || !map.entities ) { return; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !isFloorKeyEntity(e) ) { continue; }
			double dx = (e->x - pl->x) / 16.0, dy = (e->y - pl->y) / 16.0; // tiles; +y is south on the map
			int tiles = (int)std::lround(std::sqrt(dx * dx + dy * dy));
			// Arrow: relative to the way you're facing (yaw 0 = east, turning right = +yaw).
			double rel = std::atan2(dy, dx) - pl->yaw;
			while ( rel <= -PI ) { rel += 2 * PI; }
			while ( rel > PI ) { rel -= 2 * PI; }
			g_compassArrow = ((int)std::lround(rel / (PI / 4)) + 8) % 8;
			g_compassTiles = tiles;
			g_compassShowing = true;
			g_compassShowsDistance = backup || level >= 2;
			static const char* const DIRS[] = { "E", "SE", "S", "SW", "W", "NW", "N", "NE" };
			int octant = ((int)std::lround(std::atan2(dy, dx) / (PI / 4)) + 8) % 8;
			char buf[48];
			if ( tiles <= 1 ) { snprintf(buf, sizeof(buf), "right here"); }
			else if ( g_compassShowsDistance ) { snprintf(buf, sizeof(buf), "%d tiles %s", tiles, DIRS[octant]); }
			else { snprintf(buf, sizeof(buf), "%s", DIRS[octant]); }
			g_keyCompass = buf;
			return;
		}
	}

	// Using the compass from your bag or hotbar.
	bool useCompassPrivate(Item* item, int player)
	{
		if ( !isCompassItemPrivate(item) ) { return false; }
		if ( player != clientnum ) { return true; }
		if ( !g_floorNeedsKey || g_floorKeyFound || g_slot.floor_key_requirement == 2 )
		{
			messagePlayerColor(player, MESSAGE_HINT, COLOR_AP, "The compass needle spins: nothing to find on this floor.");
			return true;
		}
		if ( g_slot.compass_mode == 1 )
		{
			messagePlayerColor(player, MESSAGE_HINT, COLOR_AP, "Your compass always points at the Floor Key.");
			return true;
		}
		if ( ticks < g_compassReadyAt )
		{
			int secs = (int)((g_compassReadyAt - ticks) / TICKS_PER_SECOND) + 1;
			messagePlayerColor(player, MESSAGE_HINT, COLOR_TRAP, "The compass is still recharging (%d:%02d).", secs / 60, secs % 60);
			return true;
		}
		g_compassPingUntil = ticks + (Uint32)(std::max(5, g_slot.compass_ping_time) * TICKS_PER_SECOND);
		g_compassReadyAt = g_compassPingUntil + (Uint32)(std::max(10, g_slot.compass_recharge) * TICKS_PER_SECOND);
		playSound(91, 48);
		messagePlayerColor(player, MESSAGE_HINT, COLOR_AP, "The compass needle swings toward the Floor Key.");
		return true;
	}

	int g_enemiesLeft = -1; // hostile monsters still alive on this floor (-1 = not counted)

	int countHostileEnemies()
	{
		if ( !map.creatures ) { return 0; }
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		int n = 0;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e || e->behavior != &actMonster ) { continue; }
			Stat* st = e->getStats();
			if ( !st || st->HP <= 0 ) { continue; }
			if ( st->type == SHOPKEEPER || st->type == NOTHING || st->type == MINOTAUR ) { continue; }
			if ( e->monsterAllySummonRank != 0 || e->monsterAllyGetPlayerLeader() ) { continue; } // your allies
			if ( pl && e->checkFriend(pl) ) { continue; } // friendly people
			++n;
		}
		return n;
	}

	void removeFloorKeyEntities()
	{
		if ( !map.entities ) { return; }
		for ( node_t* node = map.entities->first; node; )
		{
			node_t* next = node->next;
			if ( isFloorKeyEntity((Entity*)node->element) ) { list_RemoveNode(node); }
			node = next;
		}
	}

	// Picked up? Then the stairs open. Every enemy dead? That opens them too. Lost (a monster took it,
	// it fell somewhere)? Put out a new one.
	void updateFloorKey()
	{
		if ( !g_floorNeedsKey || g_floorKeyFound ) { g_enemiesLeft = -1; return; }
		if ( g_slot.floor_key_requirement != 1 && ticks - g_floorStartTicks > (Uint32)(TICKS_PER_SECOND * 5) ) // monsters are set up by now
		{
			g_enemiesLeft = countHostileEnemies();
			if ( g_enemiesLeft == 0 )
			{
				g_floorKeyFound = true;
				removeFloorKeyEntities();
				messagePlayerColor(clientnum, MESSAGE_HINT, makeColorRGB(120, 230, 120),
					"Every enemy on this floor is defeated! The stairs down are open.");
				addLog("Defeated every enemy: the stairs are open.", makeColorRGB(120, 230, 120));
				playSound(91, 64);
				return;
			}
		}
		for ( int p = 0; p < MAXPLAYERS; ++p )
		{
			if ( client_disconnected[p] || !stats[p] ) { continue; }
			for ( node_t* node = stats[p]->inventory.first; node; node = node->next )
			{
				Item* it = (Item*)node->element;
				if ( it && it->type == FLOOR_KEY_TYPE && it->appearance == FLOOR_KEY_APPEARANCE )
				{
					list_RemoveNode(node);
					g_floorKeyFound = true;
					messagePlayerColor(p, MESSAGE_HINT, makeColorRGB(120, 230, 120), "You found the Floor Key! The stairs down are open.");
					addLog("Found the Floor Key.", makeColorRGB(120, 230, 120));
					playSound(91, 64);
					return;
				}
			}
		}
		if ( map.entities )
		{
			for ( node_t* node = map.entities->first; node; node = node->next )
			{
				if ( isFloorKeyEntity((Entity*)node->element) ) { return; }
			}
		}
		if ( g_slot.floor_key_requirement != 2 ) { spawnFloorKey(); } // gone from the floor without being found
	}

	void spawnFloorPickups()
	{
		if ( g_slot.archipelago_pickups <= 0 || currentlevel < 1 ) { return; } // none on the starting floor
		// Every dungeon floor gets them, generated or hand-made; not boss arenas or the town.
		static const char* const SKIP[] = { "Hamlet", "Tutorial" };
		for ( const char* name : SKIP )
		{
			if ( !strncmp(map.name, name, strlen(name)) ) { return; }
		}
		// Boss arenas get none of their own, but items you left behind still show up there, so a
		// skipped item can't end up somewhere you'll never reach.
		static const char* const BOSS[] = { "Boss", "Hell Boss", "Sanctum" };
		bool arena = false;
		for ( const char* name : BOSS ) { if ( !strncmp(map.name, name, strlen(name)) ) { arena = true; } }
		int carry = g_floorIsRestart ? 0 : g_carryPickups;
		if ( arena && carry <= 0 && !g_floorIsRestart ) { return; }
		int ownShare = arena ? 0 : g_slot.pickups_per_floor;
		// A restarted floor gets back exactly what was still unfound on it (carried ones included).
		int toPlace = std::min(g_floorIsRestart ? g_restartApBudget : ownShare + carry,
			uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups) - apItemsLyingAround());
		if ( g_floorIsRestart ) { toPlace = std::min(toPlace, g_restartApBudget - g_attemptApMade); }
		if ( !g_floorIsRestart ) { g_carryPickups = 0; } // this floor took them (or none are left to place)
		for ( int i = 0; i < toPlace; ++i )
		{
			int x, y;
			if ( !findReachableSpot(x, y) && !findFloorSpot(x, y) ) { break; } // somewhere you can walk to
			Entity* entity = newEntity(-1, 1, map.entities, nullptr); // item entity, like dropItemMonster
			entity->flags[INVISIBLE] = true;
			entity->flags[UPDATENEEDED] = true;
			entity->x = x * 16 + 8;
			entity->y = y * 16 + 8;
			entity->sizex = 4;
			entity->sizey = 4;
			entity->yaw = (local_rng.rand() % 360) * PI / 180.0;
			entity->vel_z = -.5;
			entity->flags[PASSABLE] = true;
			entity->flags[USERFLAG1] = true;
			entity->behavior = &actItem;
			entity->skill[10] = AP_ITEM_TYPE;
			entity->skill[11] = EXCELLENT;
			entity->skill[12] = 0;
			entity->skill[13] = 1;
			entity->skill[14] = AP_FLOOR_APPEARANCE;
			entity->skill[15] = 1;
			noteApItemMade();
		}
		if ( toPlace > 0 )
		{
			say(COLOR_AP, "%d Archipelago item%s on this floor.", toPlace, toPlace == 1 ? " lies" : "s lie");
			if ( carry > 0 ) { say(COLOR_AP, "(%d of them you left behind on an earlier floor.)", std::min(carry, toPlace)); }
		}
		else if ( g_floorIsRestart && g_floorApTotal > 0 && g_restartApBudget <= 0 )
		{
			say(COLOR_AP, "You already found every Archipelago item on this floor.");
		}

	}

	void snapshotInventories()
	{
		for ( int p = 0; p < MAXPLAYERS; ++p )
		{
			g_hasFloorSnapshot[p] = false;
			g_floorStartInventory[p].clear();
			if ( client_disconnected[p] || !stats[p] || !players[p] || !players[p]->isLocalPlayer() ) { continue; }
			for ( node_t* node = stats[p]->inventory.first; node; node = node->next )
			{
				Item* item = (Item*)node->element;
				if ( !item ) { continue; }
				ItemSnapshot snap{ item->type, item->status, item->beatitude, item->count,
					item->appearance, item->identified, item->uid, item->x, item->y, -1 };
				for ( int slot = 0; slot < NUM_EQUIP_SLOTS; ++slot )
				{
					if ( stats[p]->*EQUIP_SLOTS[slot] == item ) { snap.equipSlot = slot; break; }
				}
				g_floorStartInventory[p].push_back(snap);
			}
			g_hasFloorSnapshot[p] = true;
			printlog("[Archipelago] Floor start: saved %d items for player %d (floor %d).",
				(int)g_floorStartInventory[p].size(), p, currentlevel);
		}
	}

	// Put back exactly what the player carried when the floor began: used-up potions and
	// broken or dropped gear come back, and anything found on this floor is gone.
	void restoreInventory(int p)
	{
		if ( !g_hasFloorSnapshot[p] || !stats[p] ) { return; }
		Stat* s = stats[p];
		printlog("[Archipelago] Restoring %d items for player %d (had %d).", (int)g_floorStartInventory[p].size(), p,
			list_Size(&s->inventory));
		for ( int slot = 0; slot < NUM_EQUIP_SLOTS; ++slot ) { s->*EQUIP_SLOTS[slot] = nullptr; }
		if ( auto ui = inputs.getUIInteraction(p) )
		{
			ui->selectedItem = nullptr;
			ui->selectedItemFromChest = 0;
		}
		// Spells are always kept, including ones learned on this floor.
		std::vector<ItemSnapshot> spells;
		for ( node_t* node = s->inventory.first; node; node = node->next )
		{
			Item* item = (Item*)node->element;
			if ( item && item->type == SPELL_ITEM )
			{
				spells.push_back(ItemSnapshot{ item->type, item->status, item->beatitude, item->count,
					item->appearance, item->identified, item->uid, item->x, item->y, -1 });
			}
		}
		list_FreeAll(&s->inventory);
		std::vector<ItemSnapshot> toMake;
		for ( const ItemSnapshot& snap : g_floorStartInventory[p] )
		{
			if ( snap.type != SPELL_ITEM ) { toMake.push_back(snap); }
		}
		toMake.insert(toMake.end(), spells.begin(), spells.end());
		for ( const ItemSnapshot& snap : toMake )
		{
			Item* item = newItem(snap.type, snap.status, snap.beatitude, snap.count, snap.appearance,
				snap.identified, &s->inventory);
			if ( !item ) { continue; }
			item->uid = snap.uid; // keeps hotbar slots pointing at the same items
			item->x = snap.x;
			item->y = snap.y;
			if ( snap.equipSlot >= 0 ) { s->*EQUIP_SLOTS[snap.equipSlot] = item; }
		}
	}

	// Any confirm-like button: A / Space / Enter / attack / use. Uses our own edge detection so
	// another part of the game consuming the press first can't swallow it.
	bool confirmPressed()
	{
		auto& in = Input::inputs[clientnum];
		bool down = in.binary("MenuConfirm") || in.binary("Attack") || in.binary("Use")
			|| keystatus[SDLK_SPACE] || keystatus[SDLK_RETURN] || keystatus[SDLK_KP_ENTER];
		bool pressed = down && !g_confirmWasDown;
		g_confirmWasDown = down;
		return pressed;
	}

	void endDeathScreen()
	{
		if ( !g_deathScreen ) { return; }
		g_deathScreen = false;
		godmode = g_savedGodmode;
	}

	// Death Penalty: 0 just a life, 1 gold, 2 gold and XP, 3 XP. Never costs a level.
	void applyDeathPenalty(Stat* s)
	{
		if ( !s ) { return; }
		if ( g_slot.death_penalty == 1 || g_slot.death_penalty == 2 ) { s->GOLD -= s->GOLD / 4; }
		if ( g_slot.death_penalty == 2 || g_slot.death_penalty == 3 ) { s->EXP /= 2; }
	}

	void restartFloor()
	{
		endDeathScreen();
		int player = g_deathScreenPlayer;
		Stat* s = stats[player];
		if ( !s ) { return; }

		// Back to the inventory from the start of the floor (unless Death Items keeps everything).
		if ( g_slot.death_items == 0 )
		{
			restoreInventory(player);
		}

		// Death penalty. Never costs a level.
		applyDeathPenalty(s);

		for ( int c = 0; c < NUMEFFECTS; ++c )
		{
			s->clearEffect(c);
			s->EFFECTS_TIMERS[c] = 0;
		}
		s->HP = s->MAXHP;
		s->MP = s->MAXMP;
		s->HUNGER = std::max(s->HUNGER, 800);

		// Reload this floor: a new layout, or the same one again.
		g_restartApBudget = std::max(0, g_floorApTotal - g_floorApFound);
		g_attemptApMade = 0;
		g_floorReady = false;
		g_restartPending = true;
		skipLevelsOnLoad = -1;
		forceMapSeed = (g_slot.floor_restart == 1 && mapseed > 0) ? mapseed : ((local_rng.rand() & 0x7FFFFFFF) | 1);
		loadnextlevel = true;
		say(COLOR_TRAP, "Life lost. Lives left: %d", g_save.livesLeft);
		writeSave();
	}

	// ------------------------------------------------------- checkpoints
	struct Checkpoint
	{
		bool valid = false;
		int floor = 0;
		int cls = 0;
		Sint32 LVL = 1, EXP = 0, STR = 0, DEX = 0, CON = 0, INT = 0, PER = 0, CHR = 0;
		Sint32 MAXHP = 10, MAXMP = 10, GOLD = 0;
		Sint32 prof[NUMPROFICIENCIES] = {};
		int permApplied[8] = {};
		std::vector<ItemSnapshot> items;
		std::vector<int> spells;
	};
	Checkpoint g_checkpoint;
	bool g_resumePending = false;

	std::string checkpointPath()
	{
		if ( g_savePath.size() < 4 ) { return ""; }
		return g_savePath.substr(0, g_savePath.size() - 4) + "_checkpoint.txt";
	}

	void writeCheckpoint()
	{
		std::string path = checkpointPath();
		if ( path.empty() ) { return; }
		std::ofstream out(path, std::ios::trunc);
		if ( !out ) { return; }
		const Checkpoint& c = g_checkpoint;
		out << "floor=" << c.floor << "\nclass=" << c.cls << "\nLVL=" << c.LVL << "\nEXP=" << c.EXP
			<< "\nSTR=" << c.STR << "\nDEX=" << c.DEX << "\nCON=" << c.CON << "\nINT=" << c.INT
			<< "\nPER=" << c.PER << "\nCHR=" << c.CHR << "\nMAXHP=" << c.MAXHP << "\nMAXMP=" << c.MAXMP
			<< "\nGOLD=" << c.GOLD << "\n";
		for ( int i = 0; i < NUMPROFICIENCIES; ++i ) { out << "prof" << i << "=" << c.prof[i] << "\n"; }
		for ( int i = 0; i < 8; ++i ) { out << "perm" << i << "=" << c.permApplied[i] << "\n"; }
		for ( auto& it : c.items )
		{
			out << "item=" << (int)it.type << "," << (int)it.status << "," << it.beatitude << "," << it.count << ","
				<< it.appearance << "," << (it.identified ? 1 : 0) << "," << it.uid << "," << it.x << "," << it.y << ","
				<< it.equipSlot << "\n";
		}
		for ( int sp : c.spells ) { out << "spell=" << sp << "\n"; }
	}

	void readCheckpoint()
	{
		g_checkpoint = Checkpoint();
		std::ifstream in(checkpointPath());
		if ( !in ) { return; }
		std::string line;
		while ( std::getline(in, line) )
		{
			auto eq = line.find('=');
			if ( eq == std::string::npos ) { continue; }
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			long long n = atoll(v.c_str());
			Checkpoint& c = g_checkpoint;
			if ( k == "floor" ) { c.floor = (int)n; c.valid = n > 0; }
			else if ( k == "class" ) { c.cls = (int)n; }
			else if ( k == "LVL" ) { c.LVL = (Sint32)n; } else if ( k == "EXP" ) { c.EXP = (Sint32)n; }
			else if ( k == "STR" ) { c.STR = (Sint32)n; } else if ( k == "DEX" ) { c.DEX = (Sint32)n; }
			else if ( k == "CON" ) { c.CON = (Sint32)n; } else if ( k == "INT" ) { c.INT = (Sint32)n; }
			else if ( k == "PER" ) { c.PER = (Sint32)n; } else if ( k == "CHR" ) { c.CHR = (Sint32)n; }
			else if ( k == "MAXHP" ) { c.MAXHP = (Sint32)n; } else if ( k == "MAXMP" ) { c.MAXMP = (Sint32)n; }
			else if ( k == "GOLD" ) { c.GOLD = (Sint32)n; }
			else if ( k.compare(0, 4, "prof") == 0 ) { int i = atoi(k.c_str() + 4); if ( i >= 0 && i < NUMPROFICIENCIES ) { c.prof[i] = (Sint32)n; } }
			else if ( k.compare(0, 4, "perm") == 0 ) { int i = atoi(k.c_str() + 4); if ( i >= 0 && i < 8 ) { c.permApplied[i] = (int)n; } }
			else if ( k == "spell" ) { c.spells.push_back((int)n); }
			else if ( k == "item" )
			{
				long long f[10] = {};
				int fi = 0;
				std::stringstream ss(v);
				std::string part;
				while ( fi < 10 && std::getline(ss, part, ',') ) { f[fi++] = atoll(part.c_str()); }
				if ( fi < 10 ) { continue; }
				ItemSnapshot it{ (ItemType)f[0], (Status)f[1], (Sint16)f[2], (Sint16)f[3], (Uint32)f[4], f[5] != 0,
					(Uint32)f[6], (Sint32)f[7], (Sint32)f[8], (int)f[9] };
				c.items.push_back(it);
			}
		}
	}

	// Saves the character as it entered this floor (the floor-start inventory snapshot).
	void saveCheckpoint(int p)
	{
		Stat* s = stats[p];
		if ( !s || !g_hasFloorSnapshot[p] ) { return; }
		Checkpoint c;
		c.valid = true;
		c.floor = currentlevel;
		c.cls = client_classes[p];
		c.LVL = s->LVL; c.EXP = s->EXP;
		c.STR = s->STR; c.DEX = s->DEX; c.CON = s->CON; c.INT = s->INT; c.PER = s->PER; c.CHR = s->CHR;
		c.MAXHP = s->MAXHP; c.MAXMP = s->MAXMP; c.GOLD = s->GOLD;
		for ( int i = 0; i < NUMPROFICIENCIES; ++i ) { c.prof[i] = s->getProficiency(i); }
		for ( int i = 0; i < 8; ++i ) { c.permApplied[i] = g_save.permApplied[i]; }
		for ( auto& it : g_floorStartInventory[p] )
		{
			if ( it.type == SPELL_ITEM ) { c.spells.push_back((int)it.appearance); }
			else { c.items.push_back(it); }
		}
		g_checkpoint = c;
		writeCheckpoint();
		say(COLOR_AP, "Checkpoint reached! A new run will start here, on floor %d.", c.floor);
	}

	// A new run with a checkpoint saved: bring the character back, then jump to that floor.
	void resumeFromCheckpoint(int p)
	{
		Stat* s = stats[p];
		const Checkpoint& c = g_checkpoint;
		if ( !s || !c.valid ) { return; }
		client_classes[p] = c.cls;
		s->LVL = c.LVL; s->EXP = c.EXP;
		s->STR = c.STR; s->DEX = c.DEX; s->CON = c.CON; s->INT = c.INT; s->PER = c.PER; s->CHR = c.CHR;
		s->MAXHP = c.MAXHP; s->HP = c.MAXHP; s->MAXMP = c.MAXMP; s->MP = c.MAXMP; s->GOLD = c.GOLD;
		for ( int i = 0; i < NUMPROFICIENCIES; ++i ) { s->setProficiencyUnsafe(i, c.prof[i]); }
		for ( int i = 0; i < 8; ++i ) { g_save.permApplied[i] = c.permApplied[i]; }

		// Inventory: exactly the checkpoint's items. Spells: keep the new character's, add the saved ones.
		g_floorStartInventory[p].clear();
		for ( node_t* node = s->inventory.first; node; node = node->next )
		{
			Item* item = (Item*)node->element;
			if ( item && item->type == SPELL_ITEM )
			{
				g_floorStartInventory[p].push_back(ItemSnapshot{ item->type, item->status, item->beatitude, item->count,
					item->appearance, item->identified, item->uid, item->x, item->y, -1 });
			}
		}
		for ( auto& it : c.items ) { g_floorStartInventory[p].push_back(it); }
		g_hasFloorSnapshot[p] = true;
		restoreInventory(p);
		for ( int sp : c.spells ) { addSpell(sp, p, true); }

		skipLevelsOnLoad = c.floor - currentlevel - 1;
		loadnextlevel = true;
		writeSave();
		say(COLOR_AP, "Resuming from your checkpoint on floor %d.", c.floor);
	}

	bool inventoryHas(int player, std::initializer_list<ItemType> types)
	{
		if ( !stats[player] ) { return false; }
		for ( node_t* n = stats[player]->inventory.first; n; n = n->next )
		{
			Item* it = (Item*)n->element;
			if ( !it ) { continue; }
			for ( ItemType t : types ) { if ( it->type == t ) { return true; } }
		}
		return false;
	}

	// A floor with a secret-area entrance: make sure it can be reached even when floors are
	// shuffled, by giving a pickaxe (to dig) and a levitation potion (to float) if they're missing.
	void giveSecretAreaTools()
	{
		int exitType = g_secretExitThisFloor;
		g_secretExitThisFloor = 0;
		g_floorHasSecretExit = exitType != 0 && !secretlevel;
		if ( exitType == 0 || secretlevel ) { return; }
		for ( int p : activePlayers() )
		{
			bool gave = false;
			if ( !inventoryHas(p, { TOOL_PICKAXE }) )
			{
				giveItem(p, TOOL_PICKAXE, SERVICABLE, 0, true);
				gave = true;
			}
			if ( !inventoryHas(p, { POTION_LEVITATION, RING_LEVITATION, STEEL_BOOTS_LEVITATION, SPELLBOOK_LEVITATION }) )
			{
				giveItem(p, POTION_LEVITATION, EXCELLENT, 0, true);
				gave = true;
			}
			if ( gave )
			{
				messagePlayerColor(p, MESSAGE_HINT, COLOR_AP,
					"This floor hides a secret area. You were given what you need to reach it.");
			}
		}
	}

	bool isMarkableEnemy(Entity* e)
	{
		if ( !e || e->behavior != &actMonster ) { return false; }
		Stat* st = e->getStats();
		if ( !st ) { return false; } // (HP isn't set until the monster's first tick)
		int t = st->type;
		if ( t == SHOPKEEPER || t == MINOTAUR || t == LICH || t == DEVIL || t == LICH_ICE || t == LICH_FIRE || t == NOTHING )
		{
			return false;
		}
		return e->monsterAllySummonRank == 0 && !e->monsterAllyGetPlayerLeader() && st->leader_uid == 0;
	}

	// Enemy Sanity (both modes): random enemies on the floor glow green. Marked Enemies: each green
	// kill is a check. Kill Count: green kills count toward the next check.
	void pickMarkedEnemies()
	{
		g_markedEnemies.clear();
		g_markedThisFloor = 0;
		if ( g_slot.enemy_sanity == 0 || g_slot.kill_checks <= 0 || !map.creatures ) { return; }
		int left = uncheckedCount(ap_ids::KILL_OFFSET, g_slot.kill_checks);
		if ( left <= 0 ) { return; }
		int want = std::max(1, g_slot.enemy_sanity_per_floor);
		if ( g_slot.enemy_sanity == 2 ) { want = std::min(want, left); }
		std::vector<Entity*> pool;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isMarkableEnemy(e) ) { pool.push_back(e); }
		}
		for ( size_t i = 0; i + 1 < pool.size(); ++i ) // shuffle
		{
			size_t j = i + local_rng.rand() % (pool.size() - i);
			std::swap(pool[i], pool[j]);
		}
		for ( int i = 0; i < want && i < (int)pool.size(); ++i )
		{
			g_markedEnemies.insert(pool[i]->getUID());
			++g_markedThisFloor;
		}
	}

	// ------------------------------------------------------- Bounty Enemies
	// On some floors one enemy glows gold: tougher, and it drops extra gold and a bonus item.
	Uint32 g_bountyUid = 0;
	bool g_bountyBuffed = false;
	bool g_bountyClaimedHere = false; // killed on this floor: a restart doesn't bring a new one
	int g_bountyFloorLoad = -1;

	void pickBounty(bool restarted)
	{
		g_bountyUid = 0;
		g_bountyBuffed = false;
		if ( !restarted ) { g_bountyClaimedHere = false; }
		if ( !g_slot.bounty_enemies || g_bountyClaimedHere || currentlevel < 1 || secretlevel || isSpecialMap() || !map.creatures ) { return; }
		if ( (int)(local_rng.rand() % 100) >= std::max(0, std::min(100, g_slot.bounty_chance)) ) { return; }
		std::vector<Entity*> pool;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isMarkableEnemy(e) && !g_markedEnemies.count(e->getUID()) ) { pool.push_back(e); }
		}
		if ( pool.empty() ) { return; }
		Entity* pick = pool[local_rng.rand() % pool.size()];
		g_bountyUid = pick->getUID();
		Stat* st = pick->getStats();
		say(COLOR_AP, "A bounty lurks on this floor: a %s glowing gold. Defeat it for a reward!",
			st ? getMonsterLocalizedName((Monster)st->type).c_str() : "monster");
	}

	// Once the monster has set itself up (its first ticks), make it tougher.
	void updateBounty()
	{
		if ( !g_bountyUid || g_bountyBuffed ) { return; }
		Entity* e = uidToEntity(g_bountyUid);
		if ( !e ) { g_bountyUid = 0; return; }
		Stat* st = e->getStats();
		if ( !st || e->ticks < 5 ) { return; }
		st->MAXHP = st->MAXHP * 3 / 2;
		st->HP = st->MAXHP;
		st->LVL += 3;
		g_bountyBuffed = true;
	}

	void claimBounty(Entity* my)
	{
		g_bountyUid = 0;
		g_bountyClaimedHere = true;
		++g_save.bountyKills;
		int gold = 60 + 25 * std::max(1, currentlevel);
		for ( int p : activePlayers() ) { if ( stats[p] ) { stats[p]->GOLD += gold; } }
		// A bonus item, a few levels better than the floor usually gives.
		Category cat = LOOT_CATEGORIES[local_rng.rand() % (sizeof(LOOT_CATEGORIES) / sizeof(LOOT_CATEGORIES[0]))];
		ItemType type = itemLevelCurve(cat, 0, std::max(1, currentlevel) + 5, local_rng);
		if ( lootRerollable(type, 0) )
		{
			Status status = static_cast<Status>(DECREPIT + local_rng.rand() % (EXCELLENT - DECREPIT + 1));
			if ( status < WORN ) { status = WORN; }
			if ( Item* item = newItem(type, status, local_rng.rand() % 2, 1, local_rng.rand(), false, nullptr) )
			{
				dropItemMonster(item, my, nullptr);
			}
		}
		say(COLOR_AP, "Bounty claimed! +%d gold and a bonus item.", gold);
		if ( g_slot.bounty_checks > 0 && g_save.bountyKills <= g_slot.bounty_checks )
		{
			sendLocation(ap_ids::BASE_ID + ap_ids::BOUNTY_OFFSET + g_save.bountyKills);
			say(COLOR_AP, "Bounty check %ld of %d sent.", g_save.bountyKills, g_slot.bounty_checks);
		}
		writeSave();
	}

	// ------------------------------------------------------- Grace after losing a life
	// For a few seconds after losing a life and playing on (or a floor restart): no damage (Barony's
	// buddha mode, so an Amulet of Life Saving isn't used up either), unseen by enemies, and moved out
	// from under a boulder, so one hit can't take two lives.
	const int GRACE_SECONDS = 5;
	Uint32 g_graceUntil = 0;
	Sint32 g_graceHP = 0;
	bool g_graceBuddha = false;     // we turned buddha mode on (and turn it off again)
	bool g_graceInvisible = false;  // we made the player invisible (and take it off again)

	bool boulderOn(double x, double y)
	{
		for ( node_t* node = map.entities ? map.entities->first : nullptr; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( e && e->behavior == &actBoulder && std::fabs(e->x - x) < 14 && std::fabs(e->y - y) < 14 ) { return true; }
		}
		return false;
	}

	bool safeFloorTile(int x, int y)
	{
		if ( x < 0 || y < 0 || x >= (int)map.width || y >= (int)map.height ) { return false; }
		int index = y * MAPLAYERS + x * MAPLAYERS * map.height;
		if ( !map.tiles[index] || map.tiles[OBSTACLELAYER + index] ) { return false; } // pit or wall
		if ( lavatiles && lavatiles[map.tiles[index]] ) { return false; }
		if ( swimmingtiles && swimmingtiles[map.tiles[index]] ) { return false; }
		return true;
	}

	void unstickPlayer(Entity* pl)
	{
		if ( !pl || !map.tiles ) { return; }
		if ( !boulderOn(pl->x, pl->y) ) { return; }
		// Walk outward over floor you could walk on (never through walls) to the nearest free tile.
		const int w = map.width, h = map.height;
		int sx = (int)(pl->x / 16), sy = (int)(pl->y / 16);
		if ( sx < 0 || sy < 0 || sx >= w || sy >= h ) { return; }
		std::vector<int> dist(w * h, -1), queue{ sx + sy * w };
		dist[sx + sy * w] = 0;
		for ( size_t q = 0; q < queue.size(); ++q )
		{
			int cx = queue[q] % w, cy = queue[q] / w;
			if ( dist[queue[q]] > 0 )
			{
				double nx = cx * 16 + 8, ny = cy * 16 + 8;
				if ( !boulderOn(nx, ny) && !checkObstacle((long)nx, (long)ny, pl, nullptr) )
				{
					pl->x = nx; pl->y = ny;
					pl->vel_x = pl->vel_y = 0;
					return;
				}
			}
			if ( dist[queue[q]] >= 6 ) { continue; }
			const int dx[] = { 1, -1, 0, 0 }, dy[] = { 0, 0, 1, -1 };
			for ( int k = 0; k < 4; ++k )
			{
				int nx = cx + dx[k], ny = cy + dy[k];
				if ( nx < 0 || ny < 0 || nx >= w || ny >= h || dist[nx + ny * w] >= 0 || !safeFloorTile(nx, ny) ) { continue; }
				dist[nx + ny * w] = dist[queue[q]] + 1;
				queue.push_back(nx + ny * w);
			}
		}
	}

	void endGrace()
	{
		g_graceUntil = 0;
		if ( g_graceBuddha ) { buddhamode = false; g_graceBuddha = false; }
		if ( g_graceInvisible )
		{
			g_graceInvisible = false;
			Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
			if ( pl && stats[clientnum] && !g_deathScreen ) { pl->setEffect(EFF_INVISIBLE, false, 0, false); }
		}
	}

	void startGrace()
	{
		Stat* s = stats[clientnum];
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		if ( !s || !pl ) { return; }
		g_graceUntil = ticks + TICKS_PER_SECOND * GRACE_SECONDS;
		g_graceHP = std::max<Sint32>(1, s->HP);
		if ( !buddhamode ) { buddhamode = true; g_graceBuddha = true; }
		if ( !s->getEffectActive(EFF_INVISIBLE) )
		{
			pl->setEffect(EFF_INVISIBLE, true, TICKS_PER_SECOND * GRACE_SECONDS, false);
			g_graceInvisible = true;
		}
		unstickPlayer(pl);
	}

	bool graceActive() { return g_graceUntil && ticks < g_graceUntil; }

	void updateGrace()
	{
		if ( !g_graceUntil ) { return; }
		if ( !graceActive() || g_deathScreen || g_restartPending ) { endGrace(); return; }
		Stat* s = stats[clientnum];
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		if ( !s || !pl ) { return; }
		if ( s->HP < g_graceHP ) { s->HP = g_graceHP; } // healing still counts
		g_graceHP = std::max(g_graceHP, s->HP);
		unstickPlayer(pl);
		for ( node_t* node = map.creatures ? map.creatures->first : nullptr; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( e && e != pl && e->getStats() && (Uint32)e->monsterTarget == pl->getUID() ) { e->monsterReleaseAttackTarget(true); }
		}
	}

	// ------------------------------------------------------- Death Markers
	// Where you lose a life or die, a grave remembers the floor number and some of your gold. Back on
	// that floor number (a restart or a later run) the grave stands somewhere well away from the start
	// that you can walk to; walking up to it gives the gold back, once.
	const int GRAVE_SPRITE = 224; // gravestone_faded.vox
	const size_t MAX_GRAVES = 40;
	std::map<Uint32, size_t> g_graveEntities; // grave entity uid -> index in g_save.graves

	void actApGrave(Entity* my) { (void)my; } // just stands there; the client claims it

	// Returns false when no grave was recorded. Graves standing on this floor keep pointing at theirs.
	bool recordGrave(Stat* s)
	{
		if ( !g_slot.death_markers || !s || currentlevel < 1 || secretlevel || isSpecialMap() ) { return false; }
		long gold = std::max(10L, (long)s->GOLD * std::max(1, g_slot.death_marker_gold) / 100);
		if ( g_save.graves.size() >= MAX_GRAVES )
		{
			// Make room: the oldest claimed grave goes first, else the oldest.
			auto it = std::find_if(g_save.graves.begin(), g_save.graves.end(), [](const SaveData::Grave& g) { return g.claimed; });
			if ( it == g_save.graves.end() ) { it = g_save.graves.begin(); }
			size_t gone = (size_t)(it - g_save.graves.begin());
			g_save.graves.erase(it);
			for ( auto m = g_graveEntities.begin(); m != g_graveEntities.end(); )
			{
				if ( m->second == gone ) { m = g_graveEntities.erase(m); continue; }
				if ( m->second > gone ) { --m->second; }
				++m;
			}
		}
		g_save.graves.push_back({ currentlevel, gold, false });
		return true;
	}

	// A grave stands somewhere in the far half of the floor you can walk to: never right next to you.
	bool spawnGraveEntity(size_t i)
	{
		int x, y;
		if ( !findReachableSpot(x, y) ) { return false; }
		Entity* e = newEntity(GRAVE_SPRITE, 1, map.entities, nullptr);
		if ( !e ) { return false; }
		e->x = x * 16 + 8;
		e->y = y * 16 + 8;
		e->sizex = 4;
		e->sizey = 4;
		e->z = 8 - (models[GRAVE_SPRITE] ? models[GRAVE_SPRITE]->sizez / 4.0 : 2);
		e->yaw = (local_rng.rand() % 4) * PI / 2;
		e->flags[PASSABLE] = true; // never blocks a corridor
		e->flags[NOUPDATE] = true;
		e->behavior = &actApGrave;
		g_graveEntities[e->getUID()] = i;
		return true;
	}

	void spawnGravesHere()
	{
		if ( !g_slot.death_markers || currentlevel < 1 || secretlevel || isSpecialMap() ) { return; }
		int placed = 0;
		for ( size_t i = g_save.graves.size(); i-- > 0 && placed < 3; )
		{
			SaveData::Grave& g = g_save.graves[i];
			if ( g.claimed || g.floor != currentlevel ) { continue; }
			if ( !spawnGraveEntity(i) ) { break; }
			++placed;
		}
		if ( placed == 0 ) { return; }
		say(COLOR_AP, placed == 1 ? "Your grave from an earlier death is somewhere on this floor."
			: "Your graves from earlier deaths are somewhere on this floor.");
	}

	// Lost a life and kept playing (Death Link: Lose a Life): the grave appears now, elsewhere on the floor.
	void graveInPlace(Stat* s)
	{
		if ( !recordGrave(s) ) { return; }
		if ( spawnGraveEntity(g_save.graves.size() - 1) )
		{
			say(COLOR_AP, "Your gravestone rose somewhere else on this floor.");
		}
		writeSave();
	}

	void spawnGraves()
	{
		g_graveEntities.clear();
		spawnGravesHere();
	}

	void updateGraves()
	{
		if ( g_graveEntities.empty() ) { return; }
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		Stat* s = stats[clientnum];
		if ( !pl || !s || s->HP <= 0 ) { return; }
		for ( auto it = g_graveEntities.begin(); it != g_graveEntities.end(); )
		{
			Entity* e = uidToEntity(it->first);
			if ( !e || it->second >= g_save.graves.size() ) { it = g_graveEntities.erase(it); continue; }
			double dx = e->x - pl->x, dy = e->y - pl->y;
			if ( dx * dx + dy * dy > 20 * 20 ) { ++it; continue; }
			SaveData::Grave& g = g_save.graves[it->second];
			if ( !g.claimed )
			{
				g.claimed = true;
				s->GOLD += g.gold;
				say(COLOR_AP, "You find your own grave, and %ld gold you left behind.", g.gold);
				playSoundEntityLocal(pl, 242 + local_rng.rand() % 4, 64); // coins
				writeSave();
			}
			it = g_graveEntities.erase(it);
		}
	}

	// ------------------------------------------------------- Merchant Caravan
	// Sometimes a travelling merchant stops on a floor: a normal Barony shop (Trading and Charisma set
	// the prices) plus Archipelago items and a hint. It moves on when you take the stairs.
	Uint32 g_caravanUid = 0;
	bool g_caravanHere = false;

	bool openAround(int x, int y)
	{
		for ( int dy = -1; dy <= 1; ++dy )
		{
			for ( int dx = -1; dx <= 1; ++dx )
			{
				int nx = x + dx, ny = y + dy;
				if ( nx < 0 || ny < 0 || nx >= (int)map.width || ny >= (int)map.height ) { return false; }
				int index = ny * MAPLAYERS + nx * MAPLAYERS * map.height;
				if ( !map.tiles[index] || map.tiles[OBSTACLELAYER + index] ) { return false; }
				if ( lavatiles && lavatiles[map.tiles[index]] ) { return false; }
				if ( swimmingtiles && swimmingtiles[map.tiles[index]] ) { return false; }
				if ( shoparea && shoparea[ny + nx * map.height] ) { return false; }
			}
		}
		return true;
	}

	void spawnCaravan(bool restarted)
	{
		g_caravanUid = 0;
		bool eligible = g_slot.merchant_caravan && currentlevel >= 1 && !secretlevel && !isSpecialMap();
		if ( !restarted ) { g_caravanHere = eligible && (int)(local_rng.rand() % 100) < g_slot.caravan_chance; }
		if ( !eligible || !g_caravanHere ) { return; }
		int x = -1, y = -1;
		for ( int tries = 0; tries < 40 && x < 0; ++tries )
		{
			int cx, cy;
			if ( !findReachableSpot(cx, cy) ) { break; }
			if ( openAround(cx, cy) ) { x = cx; y = cy; } // in a room, never a corridor he'd block
		}
		if ( x < 0 ) { return; }
		Entity* m = summonMonsterNoSmoke(SHOPKEEPER, x * 16 + 8, y * 16 + 8, true);
		if ( !m ) { return; }
		g_caravanUid = m->getUID();
		if ( Stat* st = m->getStats() ) { strncpy(st->name, "Caravan Merchant", sizeof(st->name) - 1); }
		say(COLOR_AP, "A merchant caravan has stopped on this floor.");
	}

	bool caravanAlive()
	{
		Entity* e = g_caravanUid ? uidToEntity(g_caravanUid) : nullptr;
		Stat* st = e ? e->getStats() : nullptr;
		return st && st->HP > 0;
	}

	// A free slot in the shop's grid (or the cheapest item's slot when it's full).
	void placeInShop(Stat* st, Item* item)
	{
		const int maxX = Player::ShopGUI_t::MAX_SHOP_X, maxY = Player::ShopGUI_t::MAX_SHOP_Y;
		std::set<int> taken;
		Item* last = nullptr;
		for ( node_t* n = st->inventory.first; n; n = n->next )
		{
			Item* it = (Item*)n->element;
			if ( !it || it == item ) { continue; }
			if ( it->x >= 0 && it->x < maxX && it->y >= 0 && it->y < maxY )
			{
				taken.insert(it->x + it->y * 100);
				if ( !isApItem(it) && (!last || it->y > last->y || (it->y == last->y && it->x > last->x)) ) { last = it; }
			}
		}
		for ( int y = 0; y < maxY; ++y )
		{
			for ( int x = 0; x < maxX; ++x )
			{
				if ( !taken.count(x + y * 100) ) { item->x = x; item->y = y; return; }
			}
		}
		if ( last ) { item->x = last->x; item->y = last->y; list_RemoveNode(last->node); }
	}

	int hintsLeft()
	{
		int n = 0;
		for ( int64_t id : g_slot.hint_locations ) { if ( !isChecked(id) && !g_save.hinted.count(id) ) { ++n; } }
		return n;
	}

	void stockCaravan(Stat* st)
	{
		int apStock = std::min(2, uncheckedCount(ap_ids::CARAVAN_OFFSET, g_slot.caravan_checks));
		for ( int i = 0; i < apStock; ++i )
		{
			// A different blessing each, so two bought ones never stack into one.
			if ( Item* it = newItem(AP_ITEM_TYPE, EXCELLENT, (Sint16)i, 1, AP_CARAVAN_APPEARANCE, true, &st->inventory) ) { placeInShop(st, it); }
		}
		if ( g_slot.caravan_hints && hintsLeft() > 0 )
		{
			if ( Item* it = newItem(AP_ITEM_TYPE, EXCELLENT, 0, 1, AP_HINT_APPEARANCE, true, &st->inventory) ) { placeInShop(st, it); }
		}
	}

	void buyHint()
	{
		for ( int64_t id : g_slot.hint_locations )
		{
			if ( isChecked(id) || g_save.hinted.count(id) ) { continue; }
			g_save.hinted.insert(id);
			std::set<int64_t> one{ id };
			AP_SendLocationScouts(one, 2);
			say(COLOR_AP, "Bought an Archipelago Hint: it shows in the log.");
			writeSave();
			return;
		}
		say(COLOR_AP, "There's nothing left to hint.");
	}

	// ------------------------------------------------------- Champions
	Uint32 g_championUid = 0;
	void clearFloorUids()
	{
		g_championUid = 0;
		g_caravanUid = 0;
		g_bountyUid = 0;
		g_graveEntities.clear();
		endGrace();
	}

	// Every Champion Floors floors, a much tougher monster guards the stairs down.
	bool g_championBuffed = false;
	bool g_championDeadHere = false;

	Entity* findDownStairs()
	{
		for ( node_t* node = map.entities ? map.entities->first : nullptr; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( e && e->behavior == &actLadder && e->skill[3] == 0 ) { return e; }
		}
		return nullptr;
	}

	void pickChampion(bool restarted)
	{
		g_championUid = 0;
		g_championBuffed = false;
		if ( !restarted ) { g_championDeadHere = false; }
		int every = g_slot.champion_floors;
		if ( every <= 0 || g_championDeadHere || currentlevel < 1 || currentlevel % every != 0 || secretlevel
			|| isSpecialMap() || !map.creatures ) { return; }
		Entity* stairs = findDownStairs();
		std::vector<Entity*> pool;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( isMarkableEnemy(e) && !g_markedEnemies.count(e->getUID()) && e->getUID() != g_bountyUid ) { pool.push_back(e); }
		}
		Entity* pick = nullptr;
		if ( !pool.empty() ) { pick = pool[local_rng.rand() % pool.size()]; }
		else if ( stairs )
		{
			Monster type = static_cast<Monster>(monsterCurve(currentlevel));
			if ( type != NOTHING && type != SHOPKEEPER ) { pick = summonMonsterNoSmoke(type, stairs->x, stairs->y); }
		}
		if ( !pick ) { return; }
		if ( stairs )
		{
			// Stand guard next to the stairs: the first open tile around them.
			const int dx[] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dy[] = { 0, 0, 1, -1, 1, 1, -1, -1 };
			for ( int k = 0; k < 8; ++k )
			{
				double nx = stairs->x + dx[k] * 16, ny = stairs->y + dy[k] * 16;
				if ( !checkObstacle((long)nx, (long)ny, pick, nullptr) ) { pick->x = nx; pick->y = ny; break; }
			}
		}
		g_championUid = pick->getUID();
		Stat* st = pick->getStats();
		say(COLOR_TRAP, "A Champion %s guards the stairs down!", st ? getMonsterLocalizedName((Monster)st->type).c_str() : "monster");
	}

	void updateChampion()
	{
		if ( !g_championUid || g_championBuffed ) { return; }
		Entity* e = uidToEntity(g_championUid);
		if ( !e ) { g_championUid = 0; return; }
		Stat* st = e->getStats();
		if ( !st || e->ticks < 5 ) { return; }
		st->MAXHP *= 2;
		st->HP = st->MAXHP;
		st->LVL += 5;
		g_championBuffed = true;
	}

	bool championAlive()
	{
		if ( !g_championUid ) { return false; }
		Entity* e = uidToEntity(g_championUid);
		Stat* st = e ? e->getStats() : nullptr;
		if ( st && (st->leader_uid != 0 || e->monsterAllyGetPlayerLeader()) ) { return false; } // tamed: it's yours now
		return st && st->HP > 0;
	}

	void claimChampion(Entity* my)
	{
		g_championUid = 0;
		g_championDeadHere = true;
		++g_save.championKills;
		// A guaranteed good item: well above the floor's level, excellent and blessed.
		for ( int tries = 0; tries < 10; ++tries )
		{
			Category cat = LOOT_CATEGORIES[local_rng.rand() % (sizeof(LOOT_CATEGORIES) / sizeof(LOOT_CATEGORIES[0]))];
			if ( cat == FOOD || cat == BOOK || cat == GEM ) { continue; }
			ItemType type = itemLevelCurve(cat, std::max(1, currentlevel), std::max(1, currentlevel) + 8, local_rng);
			if ( !lootRerollable(type, 0) ) { continue; }
			if ( Item* item = newItem(type, EXCELLENT, 1 + local_rng.rand() % 2, 1, local_rng.rand(), false, nullptr) )
			{
				dropItemMonster(item, my, nullptr);
			}
			break;
		}
		say(COLOR_AP, "The Champion falls! It dropped something good. The stairs are free.");
		if ( g_slot.champion_checks > 0 && g_save.championKills <= g_slot.champion_checks )
		{
			sendLocation(ap_ids::BASE_ID + ap_ids::CHAMPION_OFFSET + g_save.championKills);
			say(COLOR_AP, "Champion check %ld of %d sent.", g_save.championKills, g_slot.champion_checks);
		}
		writeSave();
	}

	// ------------------------------------------------------- Hall of Fame
	// Every run on this seed: deepest floor, kills, lives lost, most gold, time, and how it ended.
	enum RunResult { RUN_ONGOING = 0, RUN_GAME_OVER = 1, RUN_WON = 2, RUN_LEFT = 3 };

	void endRunRecord(int result)
	{
		if ( !g_save.runActive ) { return; }
		g_save.runActive = false;
		long secs = std::max(0L, g_save.playSeconds - g_save.runStartPlay);
		char buf[128];
		snprintf(buf, sizeof(buf), "%d,%d,%d,%ld,%ld,%d", g_save.runDeepest, g_save.runKills, g_save.runLivesLost,
			g_save.runGoldMax, secs, result);
		g_save.runs.push_back(buf);
		if ( g_save.runs.size() > 100 ) { g_save.runs.erase(g_save.runs.begin()); }
		writeSave();
	}

	void startRunRecord()
	{
		endRunRecord(RUN_LEFT); // the last run never ended (you started over or quit)
		g_save.runActive = true;
		g_save.runDeepest = 0;
		g_save.runKills = 0;
		g_save.runLivesLost = 0;
		g_save.runGoldMax = 0;
		g_save.runStartPlay = g_save.playSeconds;
	}

	struct RunRow { int deepest, kills, lives; long gold, secs; int result; };
	std::vector<RunRow> pastRuns()
	{
		std::vector<RunRow> rows;
		for ( auto& r : g_save.runs )
		{
			RunRow row{};
			if ( sscanf(r.c_str(), "%d,%d,%d,%ld,%ld,%d", &row.deepest, &row.kills, &row.lives, &row.gold, &row.secs, &row.result) == 6 )
			{
				rows.push_back(row);
			}
		}
		return rows;
	}

	// Extra Monsters: more enemies on every floor from floor 1 (not the safe places, secret areas or
	// boss arenas), picked like the floor's own spawns and put somewhere you can walk to.
	void spawnExtraMonsters()
	{
		if ( g_slot.extra_monsters <= 0 || currentlevel < 1 || secretlevel || isSpecialMap() || !map.creatures ) { return; }
		int existing = 0;
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			if ( isMarkableEnemy((Entity*)node->element) ) { ++existing; }
		}
		// Extra Monsters: this many more on the floor (0 to 20).
		(void)existing;
		int extra = std::clamp(g_slot.extra_monsters, 0, 20);
		int made = 0;
		for ( int i = 0; i < extra; ++i )
		{
			int x, y;
			if ( !findReachableSpot(x, y) ) { break; }
			Monster type = static_cast<Monster>(monsterCurve(currentlevel));
			if ( type == NOTHING || type == SHOPKEEPER ) { continue; }
			if ( summonMonsterNoSmoke(type, x * 16 + 8, y * 16 + 8) ) { ++made; }
		}
		if ( made ) { printlog("[Archipelago] Extra Monsters: %d more on this floor.", made); }
	}

	void onNewFloor()
	{
		bool restarted = g_restartPending;
		endDeathScreen();
		if ( g_floorReady ) { g_attemptApMade = 0; } // no chest started the count this time
		g_floorIsRestart = restarted;
		if ( !restarted )
		{
			// Archipelago items left on the floor you just left move to the next one, so none are lost.
			if ( g_floorApUnfound > 0 )
			{
				g_carryPickups += g_floorApUnfound;
				printlog("[Archipelago] %d Archipelago item(s) left behind; they move to the next floor.", g_floorApUnfound);
			}
			g_floorApUnfound = 0;
			g_floorApTotal = g_attemptApMade; // chests already made theirs during generation
			g_floorApFound = 0;
		}
		spawnExtraMonsters(); // before the kill removal, so extra monsters you killed stay dead too
		if ( restarted ) { removeKilledEnemies(); }
		else { g_floorKills = 0; }
		if ( restarted && g_slot.death_items == 1 ) { removeTakenLoot(); }
		else if ( !restarted ) { g_floorItemsTaken = 0; g_floorChestsLooted = 0; }
		rerollGroundLoot();
		rememberFloorLoot();
		g_markedEnemies.clear();
		g_markedThisFloor = 0;
		g_stairsWarnedAt = 0;
		g_floorStartTicks = ticks;
		sendFloorChecks();
		sendSpecialAreaCheck();
		spawnFloorPickups();
		giveSecretAreaTools();
		placeRushTownExit();
		if ( !restarted ) { g_floorKeyFound = false; }
		g_floorNeedsKey = floorUsesKey();
		if ( g_floorNeedsKey && !g_floorKeyFound )
		{
			if ( g_slot.floor_key_requirement == 2 )
			{
				say(COLOR_AP, "Defeat every enemy on this floor to open the stairs down.");
			}
			else
			{
				spawnFloorKey();
				if ( !g_floorKeyFound ) { say(COLOR_AP, "Find this floor's Floor Key to open the stairs down."); }
			}
		}
		g_compassPingUntil = 0; // a new floor: the compass is ready
		g_compassReadyAt = 0;
		pickMarkedEnemies();
		pickBounty(restarted);
		pickChampion(restarted);
		if ( restarted ) { startGrace(); }
		if ( !g_routeNote.empty() ) { say(COLOR_AP, "%s", g_routeNote.c_str()); g_routeNote.clear(); }
		spawnGraves();
		spawnCaravan(restarted);
		g_save.runDeepest = std::max(g_save.runDeepest, currentlevel);
		if ( g_markedThisFloor > 0 )
		{
			say(COLOR_AP, g_slot.enemy_sanity == 2 ? "Enemies glowing green are Archipelago checks."
				: "Kill the enemies glowing green for Enemy Sanity.");
		}
		g_restartPending = false;
		g_floorReady = true;
		snapshotInventories();
		if ( g_slot.checkpoint_interval > 0 && !secretlevel && currentlevel > 0
			&& currentlevel % g_slot.checkpoint_interval == 0 && currentlevel != g_checkpoint.floor )
		{
			saveCheckpoint(clientnum);
		}
		if ( g_savedBossFloor != currentlevel )
		{
			g_savedBossHP.clear();
		}
	}

	void handleDeathLinkIn()
	{
		if ( !g_deathLinkIn.exchange(false) ) { return; }
		AP_DeathLinkClear();
		int p = clientnum;
		if ( !inGame() || !stats[p] || stats[p]->HP <= 0 || g_deathScreen || g_restartPending ) { return; }
		// Death Link Mode "Lose a Life": just a life and the penalty; you keep playing where you are.
		if ( g_slot.death_link_mode == 1 && g_save.livesLeft > 0 )
		{
			--g_save.livesLeft;
			++g_save.runLivesLost;
			applyDeathPenalty(stats[p]);
			say(COLOR_TRAP, "You lose a life (%d left). Safe for %d seconds.", g_save.livesLeft, GRACE_SECONDS);
			startGrace();
			graveInPlace(stats[p]);
			printlog("[Archipelago] Death Link: lost a life. Lives left: %d", g_save.livesLeft);
			writeSave();
			return;
		}
		say(COLOR_TRAP, "...and they take you with them.");
		g_dyingFromDeathLink = true;
		stats[p]->HP = 0;
	}

	void showServerMessages()
	{
		int shown = 0;
		while ( AP_IsMessagePending() && shown < 4 )
		{
			AP_Message* msg = AP_GetLatestMessage();
			if ( msg && (msg->type == AP_MessageType::ItemSend || msg->type == AP_MessageType::Hint
				|| msg->type == AP_MessageType::Chat || msg->type == AP_MessageType::ServerChat
				|| msg->type == AP_MessageType::Countdown) )
			{
				messagePlayerColor(clientnum, MESSAGE_MISC, COLOR_AP, "%s", msg->text.c_str());
				addLog(msg->text, makeColorRGB(200, 200, 255));
			}
			AP_ClearLatestMessage();
			++shown;
		}
	}

	// ----------------------------------------------------------- what's left on this floor
	struct FloorCounts { int ground = 0, chests = 0, shop = 0, marked = 0; int total() const { return ground + chests + shop + marked; } };

	bool listHasApItem(list_t* list)
	{
		if ( !list ) { return false; }
		for ( node_t* n = list->first; n; n = n->next )
		{
			Item* it = (Item*)n->element;
			if ( it && it->type == AP_ITEM_TYPE && (it->appearance == AP_FLOOR_APPEARANCE || it->appearance == AP_SHOP_APPEARANCE
				|| it->appearance == AP_CARAVAN_APPEARANCE) )
			{
				return true;
			}
		}
		return false;
	}

	// Ordinary loot lying on the floor (not Archipelago items), for the tracker window.
	struct LootCounts { int cat[CATEGORY_MAX] = {}; int gold = 0; int chests = 0; int total = 0; };
	LootCounts g_loot;
	int g_lootToggles = 0; // F4 presses; the tracker flips between full and short when this changes

	LootCounts countLoot()
	{
		LootCounts c;
		if ( !map.entities ) { return c; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e ) { continue; }
			if ( e->behavior == &actItem && e->skill[10] != AP_ITEM_TYPE && !isFloorKeyEntity(e) )
			{
				int type = e->skill[10];
				if ( type >= 0 && type < NUMITEMS )
				{
					int cat = items[type].category;
					if ( cat >= 0 && cat < CATEGORY_MAX ) { ++c.cat[cat]; ++c.total; }
				}
			}
			else if ( e->behavior == &actGoldBag ) { ++c.gold; }
			else if ( e->behavior == &actChest && e->children.first )
			{
				list_t* inv = (list_t*)e->children.first->element;
				if ( inv )
				{
					for ( node_t* n = inv->first; n; n = n->next )
					{
						Item* it = (Item*)n->element;
						if ( it && it->type != AP_ITEM_TYPE ) { ++c.chests; break; }
					}
				}
			}
		}
		return c;
	}

	FloorCounts countFloor()
	{
		FloorCounts c;
		if ( !map.entities ) { return c; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e ) { continue; }
			if ( e->behavior == &actItem && e->skill[10] == AP_ITEM_TYPE
				&& ((Uint32)e->skill[14] == AP_FLOOR_APPEARANCE || (Uint32)e->skill[14] == AP_SHOP_APPEARANCE) )
			{
				++c.ground;
			}
			else if ( e->behavior == &actChest && e->children.first && listHasApItem((list_t*)e->children.first->element) )
			{
				++c.chests;
			}
			else if ( e->behavior == &actMonster )
			{
				Stat* st = e->getStats();
				if ( st && st->type == SHOPKEEPER && st->HP > 0 && listHasApItem(&st->inventory) ) { ++c.shop; }
			}
		}
		for ( Uint32 uid : g_markedEnemies )
		{
			Entity* e = uidToEntity(uid);
			if ( e && e->getStats() && e->getStats()->HP > 0 ) { ++c.marked; }
		}
		return c;
	}

	std::string floorCountsText(const FloorCounts& c)
	{
		if ( c.total() == 0 ) { return "Nothing left on this floor"; }
		std::string t = "Left on this floor:";
		char buf[64];
		if ( c.ground ) { snprintf(buf, sizeof(buf), " %d on the ground", c.ground); t += buf; }
		if ( c.chests ) { snprintf(buf, sizeof(buf), "%s %d in chests", c.ground ? "," : "", c.chests); t += buf; }
		if ( c.shop ) { snprintf(buf, sizeof(buf), "%s %d in the shop", (c.ground || c.chests) ? "," : "", c.shop); t += buf; }
		if ( c.marked ) { snprintf(buf, sizeof(buf), "%s %d green enem%s", (c.ground || c.chests || c.shop) ? "," : "", c.marked, c.marked == 1 ? "y" : "ies"); t += buf; }
		return t;
	}

	// Writes a file the tracker reads in one go (a temp file renamed over it), so the tracker
	// never sees half of it.
	bool writeWhole(const std::string& path, const std::string& text)
	{
		std::string tmp = path + ".tmp";
		{
			std::ofstream file(tmp, std::ios::trunc | std::ios::binary);
			if ( !file ) { return false; }
			file << text;
		}
		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if ( !ec ) { return true; }
		// The tracker has it open right now: write it in place instead.
		std::filesystem::remove(tmp, ec);
		std::ofstream file(path, std::ios::trunc | std::ios::binary);
		if ( !file ) { return false; }
		file << text;
		return true;
	}

	FloorCounts g_floorCounts;
	int g_logPanel = 0; // 0 hidden, 1 items only (F1), 2 everything (F2), 3 death counter (F3)

	// The tracker window (BaronyAPTracker.exe) reads this file.
	void writeTrackerFile()
	{
		// Built in memory and written only when something changed, so the disk is left alone.
		static std::string lastWritten;
		std::ostringstream out;
		bool authed = AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated;
		out << "connected=" << (authed ? 1 : 0) << "\n";
		if ( authed ) { g_lostSince = 0; }
		out << "status=" << (authed ? "connected" : "lost") << "\nserver=" << g_server << "\ngame=1\n";
		out << "lostfor=" << (g_lostSince ? (ticks - g_lostSince) / TICKS_PER_SECOND : 0) << "\n";
		out << "slot=" << g_slotName << "\n";
		out << "ingame=" << (inGame() ? 1 : 0) << "\n";
		out << "floor=" << currentlevel << "\n";
		out << "ground=" << g_floorCounts.ground << "\nchests=" << g_floorCounts.chests << "\nshop=" << g_floorCounts.shop
			<< "\nmarked=" << g_floorCounts.marked << "\n";
		out << "lives=" << g_save.livesLeft << "\n";
		out << "pickups_left=" << uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups) << "\n";
		out << "deaths_total=" << totalDeaths() << "\n";
		out << "playtime=" << g_save.playSeconds << "\nfinished=" << g_save.finishSeconds << "\n";
		out << "minotaur=" << g_minotaurSecs << "\n";
		out << "floorkey=" << (!g_floorNeedsKey ? 2 : g_floorKeyFound ? 1 : 0) << "\n";
		out << "keycompass=" << g_keyCompass << "\n";
		out << "enemiesleft=" << g_enemiesLeft << "\n";
		out << "keyreq=" << g_slot.floor_key_requirement << "\n";
		{
			Entity* b = g_bountyUid ? uidToEntity(g_bountyUid) : nullptr;
			Stat* bs = b ? b->getStats() : nullptr;
			out << "bounty=" << (bs && bs->HP > 0 ? getMonsterLocalizedName((Monster)bs->type) : std::string()) << "\n";
			Entity* c = g_championUid ? uidToEntity(g_championUid) : nullptr;
			Stat* cs = c ? c->getStats() : nullptr;
			out << "champion=" << (cs && cs->HP > 0 ? getMonsterLocalizedName((Monster)cs->type) : std::string()) << "\n";
			out << "caravan=" << (caravanAlive() ? 1 : 0) << "\ngraves=" << g_graveEntities.size() << "\n";
			if ( g_slot.progressive_lantern ) { out << "lantern=" << std::min(3, countReceived(ap_ids::ITEM_LANTERN)) << "\n"; }
		}
		out << "secretexit=" << (g_floorHasSecretExit && !secretlevel ? 1 : 0) << "\n";
		out << "secretarea=" << (secretlevel ? 1 : 0) << "\n";
		{
			int per = std::max(1, g_slot.kill_check_interval);
			out << "es_mode=" << g_slot.enemy_sanity << "\nes_left=" << uncheckedCount(ap_ids::KILL_OFFSET, g_slot.kill_checks)
				<< "\nes_progress=" << (g_save.greenKills % per) << "\nes_per=" << per << "\n";
		}
		out << "loot_toggle=" << g_lootToggles << "\nloot_total=" << g_loot.total << "\nloot_gold=" << g_loot.gold
			<< "\nloot_chests=" << g_loot.chests << "\n";
		static const char* const CAT_KEYS[] = { "weapon", "armor", "amulet", "potion", "scroll", "staff", "ring",
			"spellbook", "gem", "thrown", "tool", "food", "book", "spell", "tome" };
		for ( int i = 0; i < CATEGORY_MAX && i < (int)(sizeof(CAT_KEYS) / sizeof(CAT_KEYS[0])); ++i )
		{
			out << "loot_" << CAT_KEYS[i] << "=" << g_loot.cat[i] << "\n";
		}
		// Spells tab: name, school (S/M/T), copies received, known from the class, spellbook read.
		if ( g_slot.spell_sanity || g_slot.spellbook_checks )
		{
			out << "spellsanity=" << g_slot.spell_sanity << "\nspellupgrades=" << g_slot.spell_upgrades
				<< "\nspellbooks=" << g_slot.spellbook_checks << "\n";
			for ( auto& sp : ap_ids::SPELLS )
			{
				if ( sp.dlc && !g_slot.spell_dlc ) { continue; }
				if ( !g_slot.spell_sanity && sp.dlc ) { continue; }
				spell_t* def = getSpellFromID(sp.spell);
				char school = 'S';
				if ( def && def->skillID == PRO_MYSTICISM ) { school = 'M'; }
				else if ( def && def->skillID == PRO_THAUMATURGY ) { school = 'T'; }
				if ( sp.dlc ) { school = 'D'; }
				int copies = spellCopies(sp.spell);
				bool fromClass = copies == 0 && inGame() && knowsSpell(clientnum, sp.spell);
				bool read = !sp.dlc && isChecked(ap_ids::BASE_ID + ap_ids::SPELLBOOK_OFFSET + sp.spell);
				out << "spell=" << sp.name << "\t" << school << "\t" << copies << "\t" << (fromClass ? 1 : 0)
					<< "\t" << (read ? 1 : 0) << "\n";
			}
		}
		for ( auto& d : g_save.deaths ) { out << "death=" << d.first << "\t" << d.second << "\n"; }
		size_t start = g_log.size() > 40 ? g_log.size() - 40 : 0;
		for ( size_t i = start; i < g_log.size(); ++i )
		{
			out << (g_log[i].item ? "item=" : "log=") << g_log[i].text << "\n";
		}
		std::string text = out.str();
		if ( text == lastWritten ) { return; }
		if ( writeWhole("archipelago/tracker.txt", text) ) { lastWritten = text; }
	}

	// ------------------------------------------------------- Boss Rush (Goal)
	// The dungeon down to Boss Rush Floors, then the bosses you picked back to back (in game order:
	// Herx, the Devil, the Twin Liches; or shuffled), with a rest in the town between them if you like. Bosses you've
	// already beaten (in any run) are skipped.
	// The end of the run comes from us: Boss Rush, or Dungeon Floors with any goal (a set number of
	// floors, then the goal boss's arena). Single player only.
	bool bossRushOn() { return g_ready && (g_slot.goal == 3 || g_slot.dungeon_floors > 0) && multiplayer == SINGLE; }
	bool g_rushSaveDirty = false; // the rush state changed while a floor was loading: save it on the next tick

	int bossRushGoalMask()
	{
		int m = g_slot.boss_rush_mask & 7;
		if ( m == 0 ) { m = 7; }
		return (m & 1 ? 1 : 0) | (m & 2 ? 2 : 0) | (m & 4 ? 12 : 0);
	}

	std::vector<std::string> splitCommas(const std::string& v)
	{
		std::vector<std::string> out;
		std::string cur;
		for ( char c : v ) { if ( c == ',' ) { if ( !cur.empty() ) { out.push_back(cur); } cur.clear(); } else { cur += c; } }
		if ( !cur.empty() ) { out.push_back(cur); }
		return out;
	}

	std::string makeRushSeq()
	{
		// A normal goal with Dungeon Floors: just its boss.
		if ( g_slot.goal == 0 ) { return "boss,"; }
		if ( g_slot.goal == 1 ) { return "hellboss,"; }
		if ( g_slot.goal == 2 ) { return "sanctum,"; }
		// All Three Bosses with Dungeon Floors: all three, in the game's order. Boss Rush: the ones you picked.
		int m = g_slot.goal == 4 ? 15 : bossRushGoalMask(), killed = g_save.goalBossesKilled;
		std::vector<std::string> bosses;
		if ( (m & 1) && !(killed & 1) ) { bosses.push_back("boss"); }
		if ( (m & 2) && !(killed & 2) ) { bosses.push_back("hellboss"); }
		if ( (m & 12) && (killed & 12) != (m & 12) ) { bosses.push_back("sanctum"); }
		if ( g_slot.goal == 3 && g_slot.boss_rush_order != 0 && bosses.size() > 1 )
		{
			// Random order: the same for every run of this seed, or a new one each run.
			uint32_t seed = (uint32_t)g_slot.boss_rush_seed;
			if ( g_slot.boss_rush_order == 2 ) { seed ^= uniqueGameKey * 2654435761u; }
			std::mt19937 rng(seed ? seed : 1);
			std::shuffle(bosses.begin(), bosses.end(), rng);
		}
		std::string seq;
		for ( size_t i = 0; i < bosses.size(); ++i )
		{
			if ( i > 0 && g_slot.boss_rush_town ) { seq += "hamlet,"; }
			seq += bosses[i] + ",";
		}
		return seq;
	}

	// Called as a floor starts loading: in the rush, which map to load instead ("" = the usual one).
	std::string bossRushMapFor(int level)
	{
		if ( !bossRushOn() || secretlevel ) { return ""; }
		int start = std::max(1, g_slot.dungeon_floors > 0 ? g_slot.dungeon_floors : 10);
		if ( level <= start ) { return ""; }
		if ( g_save.rushRunKey != uniqueGameKey || g_save.rushSeq.empty() )
		{
			g_save.rushRunKey = uniqueGameKey;
			g_save.rushSeq = makeRushSeq();
			g_save.rushPhase = 0;
			g_save.rushLevel = 0;
		}
		auto seq = splitCommas(g_save.rushSeq);
		if ( seq.empty() ) { return ""; }
		// A new floor number moves on to the next stop; the same one (a restart, or continuing a
		// saved game) loads the same stop again.
		if ( level != g_save.rushLevel || g_save.rushPhase == 0 )
		{
			g_save.rushPhase = std::min((int)seq.size(), g_save.rushPhase + 1);
			g_save.rushLevel = level;
		}
		g_rushSaveDirty = true; // may run on the loading thread: the save is written on the next tick
		const std::string& map = seq[g_save.rushPhase - 1];
		printlog("[Archipelago] Boss Rush: stop %d of %d (%s).", g_save.rushPhase, (int)seq.size(), map.c_str());
		return map;
	}

	// All Three Bosses (normal length): one route through all of them. Barony splits after the Ruins:
	// Herx's portal skips Hell, and the Underworld's way into Hell skips Herx. Here Herx's portal leads
	// into Hell, the Devil's portal on to Hamlet and the expansion, and Hell can't be entered from the
	// Underworld until Herx is beaten.
	bool allBossesRoute() { return g_ready && g_slot.goal == 4 && !bossRushOn() && multiplayer == SINGLE; }

	void adjustNextLevelImpl(const std::string& prevMap)
	{
		if ( !allBossesRoute() || secretlevel ) { return; }
		bool fromHerx = prevMap.compare(0, 4, "Boss") == 0;
		bool fromHell = prevMap.compare(0, 4, "Hell") == 0;
		if ( fromHerx && currentlevel > 21 )
		{
			currentlevel = 21;
			g_routeNote = "Through Herx's portal and down into Hell. The Devil waits at the bottom.";
		}
		else if ( currentlevel >= 21 && currentlevel <= 24 && !fromHerx && !fromHell && !(g_save.goalBossesKilled & 1) )
		{
			currentlevel = 19;
			g_routeNote = "Hell won't let you in yet: Baron Herx must fall first. Back to the Ruins.";
		}
		else if ( prevMap.compare(0, 9, "Hell Boss") == 0 && currentlevel >= 25 )
		{
			g_routeNote = "Out of Hell. The twin liches wait at the end of the dungeon.";
		}
	}

	bool bossRushMoreAfterThis()
	{
		if ( !bossRushOn() || g_save.rushRunKey != uniqueGameKey ) { return false; }
		return g_save.rushPhase > 0 && g_save.rushPhase < (int)splitCommas(g_save.rushSeq).size();
	}

	// In the rush's town: the way down to the next boss goes to the middle of town.
	void placeRushTownExit()
	{
		if ( !bossRushOn() || strncmp(map.name, "Hamlet", 6) || !bossRushMoreAfterThis() || !map.entities ) { return; }
		int x, y;
		if ( !findReachableSpot(x, y, true) ) { return; }
		int moved = 0;
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !e || e->behavior != &actLadder ) { continue; }
			e->x = x * 16 + 8;
			e->y = y * 16 + 8;
			++moved;
			break;
		}
		if ( moved )
		{
			say(COLOR_AP, "Boss Rush: rest and shop here. The way to the next boss is in the middle of town.");
		}
	}

	// The tracker's floor map: archipelago/trackermap.txt. Only what the game's own map knows:
	// the layout you've seen (or mapped), items lying where you've seen; Archipelago items and green
	// enemies everywhere; the Floor Key only while the Compass or the key warning points at it.
	bool tileSeen(int x, int y)
	{
		if ( x < 0 || y < 0 || x >= (int)map.width || y >= (int)map.height || x >= MINIMAP_MAX_DIMENSION || y >= MINIMAP_MAX_DIMENSION ) { return false; }
		return minimap[y][x] == 1 || minimap[y][x] == 2;
	}

	std::string cleanField(std::string v)
	{
		for ( char& c : v ) { if ( c == '\t' || c == '\n' || c == '\r' ) { c = ' '; } }
		return v;
	}

	void writeTrackerMap()
	{
		static std::string lastWritten;
		std::ostringstream out;
		bool ingameNow = inGame() && map.tiles && map.width > 0 && map.height > 0;
		out << "ingame=" << (ingameNow ? 1 : 0) << "\n";
		if ( ingameNow )
		{
			int w = std::min<int>(map.width, MINIMAP_MAX_DIMENSION), h = std::min<int>(map.height, MINIMAP_MAX_DIMENSION);
			out << "size=" << w << " " << h << "\nfloorid=" << g_floorLoads << "\n";
			out << "name=" << cleanField(map.name) << "\n";
			for ( int y = 0; y < h; ++y )
			{
				std::string row(w, '.');
				for ( int x = 0; x < w; ++x )
				{
					switch ( minimap[y][x] )
					{
						case 1: row[x] = 'f'; break; // floor you've seen
						case 2: row[x] = 'w'; break; // wall you've seen
						case 3: row[x] = 'g'; break; // mapped floor (Magic Mapping)
						case 4: row[x] = 'h'; break; // mapped wall
						default: break;
					}
				}
				out << "row=" << row << "\n";
			}
			if ( players[clientnum] && players[clientnum]->entity )
			{
				Entity* pl = players[clientnum]->entity;
				out << "player=" << pl->x / 16.0 << "\t" << pl->y / 16.0 << "\t" << pl->yaw << "\n";
			}
			// Always with Floor Key on Tracker Map; else only while the Compass, the Minotaur warning or
			// Key Compass Time points at it.
			bool keyVisible = g_slot.floor_key_on_tracker_map != 0 || g_compassShowing || !g_keyCompass.empty();
			for ( node_t* node = map.entities ? map.entities->first : nullptr; node; node = node->next )
			{
				Entity* e = (Entity*)node->element;
				if ( !e ) { continue; }
				double tx = e->x / 16.0, ty = e->y / 16.0;
				int ix = (int)(e->x / 16), iy = (int)(e->y / 16);
				if ( e->behavior == &actItem )
				{
					if ( e->skill[10] == AP_ITEM_TYPE && (Uint32)e->skill[14] == AP_FLOOR_APPEARANCE )
					{
						out << "ap=" << tx << "\t" << ty << "\tground\n";
						continue;
					}
					if ( isFloorKeyEntity(e) )
					{
						if ( keyVisible ) { out << "key=" << tx << "\t" << ty << "\n"; }
						continue;
					}
					if ( !tileSeen(ix, iy) ) { continue; }
					Item* item = newItemFromEntity(e, true);
					if ( !item ) { continue; }
					std::string path = getItemSpritePath(clientnum, *item);
					std::string name = item->getName();
					free(item);
					out << "item=" << tx << "\t" << ty << "\t" << cleanField(path) << "\t" << cleanField(name) << "\n";
				}
				else if ( e->behavior == &actApGrave )
				{
					if ( g_graveEntities.count(e->getUID()) && tileSeen(ix, iy) ) { out << "grave=" << tx << "\t" << ty << "\n"; }
				}
				else if ( e->behavior == &actGoldBag )
				{
					if ( tileSeen(ix, iy) ) { out << "gold=" << tx << "\t" << ty << "\t" << e->skill[0] << "\n"; }
				}
				else if ( e->behavior == &actChest )
				{
					list_t* inv = chestList(e);
					bool hasAp = false;
					for ( node_t* n = inv ? inv->first : nullptr; n; n = n->next ) { if ( isApItem((Item*)n->element) ) { hasAp = true; break; } }
					if ( hasAp ) { out << "ap=" << tx << "\t" << ty << "\tchest\n"; }
					else if ( tileSeen(ix, iy) ) { out << "chest=" << tx << "\t" << ty << "\n"; }
				}
				else if ( e->behavior == &actLadder || e->behavior == &actPortal )
				{
					if ( !tileSeen(ix, iy) ) { continue; }
					bool secret = e->behavior == &actLadder && e->skill[3] != 0;
					out << (secret ? "secret=" : "stairs=") << tx << "\t" << ty << "\n";
				}
				else if ( e->behavior == &actMonster )
				{
					Stat* st = e->getStats();
					if ( st && st->type == SHOPKEEPER && g_caravanUid && e->getUID() == g_caravanUid && st->HP > 0 )
					{
						out << "caravan=" << tx << "\t" << ty << "\n";
					}
					if ( st && st->type == SHOPKEEPER )
					{
						for ( node_t* n = st->inventory.first; n; n = n->next )
						{
							if ( isApItem((Item*)n->element) ) { out << "ap=" << tx << "\t" << ty << "\tshop\n"; break; }
						}
					}
					else if ( isMarkedEnemy(e) && st && st->HP > 0 )
					{
						out << "foe=" << tx << "\t" << ty << "\n";
					}
					else if ( g_championUid && e->getUID() == g_championUid && st && st->HP > 0 )
					{
						out << "champion=" << tx << "\t" << ty << "\n";
					}
					else if ( g_bountyUid && e->getUID() == g_bountyUid && st && st->HP > 0 )
					{
						out << "bounty=" << tx << "\t" << ty << "\n";
					}
				}
			}
			out << "locked=" << (g_floorNeedsKey && !g_floorKeyFound && g_slot.floor_key_requirement != 2 ? 1 : 0) << "\n";
		}
		std::string text = out.str();
		if ( text == lastWritten ) { return; }
		if ( writeWhole("archipelago/trackermap.txt", text) ) { lastWritten = text; }
	}

	void launchTracker()
	{
#ifdef WINDOWS
		static bool launched = false;
		if ( launched ) { return; }
		launched = true;
		if ( !std::filesystem::exists("BaronyAPTracker.exe") ) { return; }
		STARTUPINFOA si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		char cmd[] = "BaronyAPTracker.exe";
		if ( CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) )
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		}
#endif
	}

	// The tracker window connects us: it writes archipelago/connect.txt (server=host:port, slot=name),
	// we read it, delete it and connect.
	void pollTrackerConnect()
	{
		std::error_code ec;
		if ( !std::filesystem::exists("archipelago/connect.txt", ec) ) { return; }
		std::string server, slot, password;
		{
			std::ifstream in("archipelago/connect.txt");
			std::string line;
			while ( std::getline(in, line) )
			{
				if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
				auto eq = line.find('=');
				if ( eq == std::string::npos ) { continue; }
				std::string key = line.substr(0, eq), value = line.substr(eq + 1);
				if ( key == "server" ) { server = value; }
				else if ( key == "slot" ) { slot = value; }
				else if ( key == "password" ) { password = value; }
			}
		}
		std::filesystem::remove("archipelago/connect.txt", ec);
		if ( server.empty() || slot.empty() ) { return; }
		printlog("[Archipelago] Connecting from the tracker window.");
		connect(server, slot, password);
	}

	// Before we're connected, the tracker only needs to know how the connection is going.
	void writeConnectStatus()
	{
		static std::string lastWritten;
		std::string status = "idle";
		if ( g_started )
		{
			switch ( AP_GetConnectionStatus() )
			{
				case AP_ConnectionStatus::ConnectionRefused: status = "refused"; break;
				case AP_ConnectionStatus::Authenticated: status = "connecting"; break; // waiting for slot data
				default: status = "connecting"; break;
			}
		}
		std::ostringstream out;
		out << "connected=0\nstatus=" << status << "\nserver=" << g_server << "\nslot=" << g_slotName << "\ngame=1\n";
		std::string text = out.str();
		if ( text == lastWritten ) { return; }
		std::filesystem::create_directories("archipelago");
		if ( writeWhole("archipelago/tracker.txt", text) ) { lastWritten = text; }
	}

	// ----------------------------------------------------------- console commands
	ConsoleCommand ccmd_ap_connect("/ap_connect", "connect to Archipelago: /ap_connect host:port slot [password]",
		[](int argc, const char** argv) {
		if ( argc < 3 )
		{
			messagePlayer(clientnum, MESSAGE_MISC, "Usage: /ap_connect host:port slot [password]");
			return;
		}
		connect(argv[1], argv[2], argc >= 4 ? argv[3] : "");
	});

	ConsoleCommand ccmd_ap_disconnect("/ap_disconnect", "disconnect from Archipelago",
		[](int argc, const char** argv) {
		disconnect();
		messagePlayer(clientnum, MESSAGE_MISC, "Archipelago: disconnected.");
	});

	ConsoleCommand ccmd_ap_status("/ap_status", "show the Archipelago connection, keys, lives and kills",
		[](int argc, const char** argv) {
		messagePlayer(clientnum, MESSAGE_MISC, "%s", statusLine().c_str());
	});

	ConsoleCommand ccmd_ap_say("/ap_say", "send a chat message to the Archipelago room",
		[](int argc, const char** argv) {
		std::string text;
		for ( int i = 1; i < argc; ++i ) { if ( i > 1 ) { text += " "; } text += argv[i]; }
		if ( g_ready && !text.empty() ) { AP_Say(text); }
	});

	// Testing helper: fire any trap right now, e.g. /ap_trap minotaur
	ConsoleCommand ccmd_ap_trap("/ap_trap", "test a trap: minotaur, ambush, polymorph, fumble, blind, teleport, hunger, darkness, curse",
		[](int argc, const char** argv) {
		if ( argc < 2 || !inGame() ) { return; }
		static const std::pair<const char*, int64_t> traps[] = {
			{ "minotaur", ap_ids::ITEM_TRAP_MINOTAUR }, { "ambush", ap_ids::ITEM_TRAP_AMBUSH },
			{ "polymorph", ap_ids::ITEM_TRAP_POLYMORPH }, { "fumble", ap_ids::ITEM_TRAP_FUMBLE },
			{ "blind", ap_ids::ITEM_TRAP_BLIND }, { "teleport", ap_ids::ITEM_TRAP_TELEPORT },
			{ "hunger", ap_ids::ITEM_TRAP_HUNGER }, { "darkness", ap_ids::ITEM_TRAP_DARKNESS },
			{ "curse", ap_ids::ITEM_TRAP_CURSE },
		};
		for ( auto& t : traps )
		{
			if ( !strcmp(argv[1], t.first) ) { applyTrap(t.second); writeSave(); return; }
		}
		messagePlayer(clientnum, MESSAGE_MISC, "Unknown trap: %s", argv[1]);
	});
} // anonymous namespace

// =============================================================== hooks

void tick()
{
	if ( !g_triedAutoConnect )
	{
		g_triedAutoConnect = true;
#ifdef WINDOWS
		CreateMutexA(nullptr, FALSE, "BaronyAPGameRunning"); // lets the tracker window know the game is open
#endif
		std::filesystem::create_directories("archipelago");
		std::error_code ec;
		if ( g_apOff ) { printlog("[Archipelago] Playing without Archipelago."); }
		else if ( g_viaLauncher ) { printlog("[Archipelago] Started from the launcher."); }
		else
		{
			std::filesystem::remove("archipelago/connect.txt", ec); // left over from last time
			// With the tracker window, you connect from there; otherwise archipelago.cfg connects on start.
			if ( std::filesystem::exists("BaronyAPTracker.exe", ec) ) { launchTracker(); }
			else { autoConnectFromConfig(); }
		}
	}
	if ( g_apOff ) { return; }
	if ( ticks % (TICKS_PER_SECOND / 2) == 0 ) { pollTrackerConnect(); }
	if ( !g_ready && ticks % TICKS_PER_SECOND == 0 ) { writeConnectStatus(); }
	if ( !g_started ) { return; }
	checkReady();
	if ( !g_ready ) { return; }
	// Playtime: counts only while you're in the dungeon and the game isn't paused (menus don't count).
	{
		static Uint32 playTicks = 0;
		if ( inGame() && !gamePaused && !loading )
		{
			if ( ++playTicks >= (Uint32)TICKS_PER_SECOND )
			{
				playTicks = 0;
				++g_save.playSeconds;
				if ( g_save.playSeconds % 30 == 0 ) { writeSave(); }
			}
		}
	}
	if ( g_resendChecks && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated )
	{
		g_resendChecks = false;
		for ( int64_t loc : g_sent ) { AP_SendItem(loc); }
		if ( g_save.goalSent ) { AP_StoryComplete(); }
		printlog("[Archipelago] Reconnected; %d checks sent again.", (int)g_sent.size());
	}

	// The death screen works even while the connection is down.
	if ( g_deathScreen && inGame() )
	{
		bool pressed = confirmPressed();
		if ( pressed && ticks - g_deathScreenStart >= DEATH_SCREEN_MIN_TICKS )
		{
			restartFloor();
		}
	}

	if ( AP_GetConnectionStatus() == AP_ConnectionStatus::Disconnected )
	{
		if ( g_lostSince == 0 ) { g_lostSince = ticks ? ticks : 1; }
		if ( ticks % TICKS_PER_SECOND == 0 ) { writeTrackerFile(); } // the tracker shows "Reconnecting..."
		return; // APCpp reconnects by itself; items wait in the inbox
	}

	showServerMessages();
	if ( keystatus[SDLK_F1] ) { keystatus[SDLK_F1] = 0; g_logPanel = g_logPanel == 1 ? 0 : 1; }
	if ( keystatus[SDLK_F2] ) { keystatus[SDLK_F2] = 0; g_logPanel = g_logPanel == 2 ? 0 : 2; }
	if ( keystatus[SDLK_F3] ) { keystatus[SDLK_F3] = 0; g_logPanel = g_logPanel == 3 ? 0 : 3; }
	if ( keystatus[SDLK_F4] ) { keystatus[SDLK_F4] = 0; ++g_lootToggles; } // tracker: full or short loot list
	if ( keystatus[SDLK_F5] ) { keystatus[SDLK_F5] = 0; g_hofPanel = !g_hofPanel; g_hofAutoUntil = 0; } // Hall of Fame
	pullDeathSources();
	if ( g_floorReady && inGame() ) { g_floorApUnfound = std::max(0, g_floorApTotal - g_floorApFound); }
	if ( g_rushSaveDirty ) { g_rushSaveDirty = false; writeSave(); }
	if ( ticks % TICKS_PER_SECOND == 0 ) { writeTrackerFile(); writeTrackerMap(); }
	if ( !inGame() )
	{
		// A Death Link that arrives in the menus or between runs is dropped, so it can't
		// kill you the moment your next run starts.
		if ( g_deathLinkIn.exchange(false) )
		{
			AP_DeathLinkClear();
			printlog("[Archipelago] Death Link received outside a run; ignored.");
		}
		return;
	}

	pullInbox();
	if ( g_save.runKey != uniqueGameKey && stats[clientnum]->HP > 0 )
	{
		initRun();
		g_resumePending = g_slot.checkpoint_interval > 0 && g_checkpoint.valid && currentlevel < g_checkpoint.floor;
	}
	applyNewItems();
	updatePendingTraps();
	updateBounty();
	updateChampion();
	updateGrace();
	updateGraves();
	if ( stats[clientnum] && stats[clientnum]->GOLD > g_save.runGoldMax ) { g_save.runGoldMax = stats[clientnum]->GOLD; }
	syncPermanents();
	if ( ticks % TICKS_PER_SECOND == 0 ) { syncSpells(); }
	if ( g_resumePending && !loadnextlevel && players[clientnum]->entity && stats[clientnum]->HP > 0 )
	{
		g_resumePending = false;
		resumeFromCheckpoint(clientnum);
	}
	handleDeathLinkIn();

	// A floor load makes a new player entity, which also catches a same-layout restart.
	Uint32 playerUid = players[clientnum]->entity->getUID();
	bool newFloor = currentlevel != g_lastLevel || (int)secretlevel != g_lastSecret || mapseed != g_lastMapSeed
		|| playerUid != g_lastPlayerUid;
	if ( newFloor )
	{
		g_lastLevel = currentlevel;
		g_lastSecret = secretlevel;
		g_lastMapSeed = mapseed;
		g_lastPlayerUid = playerUid;
		onNewFloor();
	}


	if ( ++g_tickCounter % 10 == 0 )
	{
		sendLevelChecks();
		sendSkillChecks();
		scanInventories();
		g_floorCounts = countFloor();
		g_loot = countLoot();
		updateMinotaurStatus();
		if ( g_compassGivePending && players[clientnum] && players[clientnum]->entity ) { g_compassGivePending = false; giveCompassIfMissing(); }
		updateFloorKey();
		updateKeyCompass(g_minotaurSecs);
	}
}

// Boss Rush with a long dungeon: boss arenas on the way down are skipped (a floor of the area just
// before them is played instead); the bosses come in the rush at the end.
static int skipRushBossLine(int line, const std::vector<std::string>& levelsList)
{
	if ( !bossRushOn() || secretlevel || line < 1 || line >= (int)levelsList.size() ) { return line; }
	std::string text = levelsList[line];
	while ( !text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t') ) { text.pop_back(); }
	bool bossLine = text == "map: boss" || text == "map: hellboss" || text == "map: sanctum";
	if ( !bossLine ) { return line; }
	for ( int i = line - 1; i >= 1; --i )
	{
		if ( levelsList[i].compare(0, 4, "gen:") == 0 ) { return i; }
	}
	return line;
}

int remapShuffledLine(int levelToLoad, const std::vector<std::string>& levelsList);

// Custom Dungeon Length past floor 34: bonus floors drawn from the last areas' generated floors
// (Caves and Citadel), the same for a floor number every time.
static int bonusFloorLine(int level, const std::vector<std::string>& levelsList)
{
	int last = (int)levelsList.size() - 1;
	if ( !bossRushOn() || secretlevel || level < last || level > g_slot.dungeon_floors ) { return -1; }
	std::vector<int> late;
	for ( int i = std::max(1, last - 10); i < last; ++i )
	{
		if ( levelsList[i].compare(0, 4, "gen:") == 0 ) { late.push_back(i); }
	}
	if ( late.empty() ) { return -1; }
	uint32_t h = (uint32_t)g_slot.floor_shuffle_seed * 2654435761u ^ (uint32_t)level * 40503u;
	h ^= h >> 13;
	return late[h % late.size()];
}

int remapLevelLine(int levelToLoad, const std::vector<std::string>& levelsList)
{
	int bonus = bonusFloorLine(levelToLoad, levelsList);
	if ( bonus >= 0 ) { return bonus; }
	int mapped = remapShuffledLine(levelToLoad, levelsList);
	return skipRushBossLine(mapped, levelsList);
}

int remapShuffledLine(int levelToLoad, const std::vector<std::string>& levelsList)
{
	if ( !g_ready || g_slot.floor_shuffle == 0 || secretlevel || multiplayer == CLIENT )
	{
		return levelToLoad;
	}
	if ( g_floorLineMap.empty() || g_floorLineMapSource != levelsList.size() )
	{
		size_t n = levelsList.size();
		g_floorLineMap.resize(n);
		for ( size_t i = 0; i < n; ++i ) { g_floorLineMap[i] = (int)i; }
		g_floorLineMapSource = n;

		auto biomeOf = [&](size_t i) -> std::string {
			const std::string& line = levelsList[i];
			if ( line.compare(0, 4, "gen:") != 0 ) { return ""; }
			std::istringstream ss(line.substr(4));
			std::string biome;
			ss >> biome;
			return biome;
		};
		// Only floors up to the goal take part, so nothing past the goal leaks in.
		size_t last = std::min(n - 1, (size_t)g_slot.goal_floor);
		std::mt19937 rng((uint32_t)g_slot.floor_shuffle_seed);

		if ( g_slot.floor_shuffle == 1 )
		{
			// Sections: runs of 4 identical generated biomes trade places.
			std::vector<size_t> runStarts;
			for ( size_t i = 1; i + 3 <= last; )
			{
				std::string b = biomeOf(i);
				if ( !b.empty() && biomeOf(i + 1) == b && biomeOf(i + 2) == b && biomeOf(i + 3) == b
					&& (i + 4 > last || biomeOf(i + 4) != b) )
				{
					runStarts.push_back(i);
					i += 4;
				}
				else
				{
					++i;
				}
			}
			std::vector<size_t> shuffled = runStarts;
			std::shuffle(shuffled.begin(), shuffled.end(), rng);
			for ( size_t r = 0; r < runStarts.size(); ++r )
			{
				for ( size_t j = 0; j < 4; ++j )
				{
					g_floorLineMap[runStarts[r] + j] = (int)(shuffled[r] + j);
				}
			}
		}
		else
		{
			// Full: every generated floor draws from any generated floor.
			std::vector<size_t> genLines;
			for ( size_t i = 1; i <= last; ++i )
			{
				if ( !biomeOf(i).empty() ) { genLines.push_back(i); }
			}
			std::vector<size_t> shuffled = genLines;
			std::shuffle(shuffled.begin(), shuffled.end(), rng);
			for ( size_t k = 0; k < genLines.size(); ++k )
			{
				g_floorLineMap[genLines[k]] = (int)shuffled[k];
			}
		}
	}
	if ( levelToLoad >= 0 && levelToLoad < (int)g_floorLineMap.size() )
	{
		return g_floorLineMap[levelToLoad];
	}
	return levelToLoad;
}

void onDungeonGenerated()
{
	++g_floorLoads;
	g_generatedFloorKey = true;
	g_generatedLevel = currentlevel;
	g_generatedSecret = secretlevel;
	if ( !g_ready || secretlevel || multiplayer == CLIENT ) { return; }

	bool dirty = false;
	if ( g_save.minotaurQueue > 0 && !minotaurlevel && strncmp(map.filename, "fortress", 8) )
	{
		minotaurlevel = 1;
		--g_save.minotaurQueue;
		dirty = true;
	}
	if ( g_save.darknessQueue > 0 && !darkmap )
	{
		darkmap = true;
		--g_save.darknessQueue;
		messageLocalPlayers(MESSAGE_HINT, Language::get(1108));
		dirty = true;
	}
	if ( dirty ) { writeSave(); }
}

void onMonsterInit(Entity* my, Stat* myStats)
{
	if ( !g_ready || !my || !myStats || multiplayer == CLIENT ) { return; }

	if ( myStats->type == SHOPKEEPER ) { rerollItemList(&myStats->inventory); } // Randomize Loot

	// Shopkeepers stock an Archipelago item while shop checks remain.
	if ( myStats->type == SHOPKEEPER && g_caravanUid && my->getUID() == g_caravanUid ) { stockCaravan(myStats); }
	else if ( myStats->type == SHOPKEEPER && g_slot.shop_checks > 0
		&& uncheckedCount(ap_ids::SHOP_OFFSET, g_slot.shop_checks) > 0 )
	{
		Item* apItem = newItem(AP_ITEM_TYPE, EXCELLENT, 0, 1, AP_SHOP_APPEARANCE, true, &myStats->inventory);
		if ( apItem )
		{
			// The shopkeeper has already sorted his stock into the shop's grid; give the Archipelago
			// item a slot of its own (it used to sit on slot 0,0, hidden under another item).
			const int maxX = Player::ShopGUI_t::MAX_SHOP_X, maxY = Player::ShopGUI_t::MAX_SHOP_Y;
			std::set<int> taken;
			Item* last = nullptr;
			for ( node_t* n = myStats->inventory.first; n; n = n->next )
			{
				Item* it = (Item*)n->element;
				if ( !it || it == apItem ) { continue; }
				if ( it->x >= 0 && it->x < maxX && it->y >= 0 && it->y < maxY )
				{
					taken.insert(it->x + it->y * 100);
					if ( !last || it->y > last->y || (it->y == last->y && it->x > last->x) ) { last = it; }
				}
			}
			bool placed = false;
			for ( int y = 0; y < maxY && !placed; ++y )
			{
				for ( int x = 0; x < maxX && !placed; ++x )
				{
					if ( !taken.count(x + y * 100) ) { apItem->x = x; apItem->y = y; placed = true; }
				}
			}
			if ( !placed && last )
			{
				// A full shop: the item in the last slot (the cheapest) makes room.
				apItem->x = last->x;
				apItem->y = last->y;
				list_RemoveNode(last->node);
			}
		}
	}

	bool isBoss = myStats->type == LICH || myStats->type == DEVIL || myStats->type == LICH_ICE
		|| myStats->type == LICH_FIRE;


	// Enemy / Boss Level Scaling: bring monsters up to (or toward) the strongest player's level.
	int scaling = isBoss ? g_slot.boss_level_scaling : g_slot.enemy_level_scaling;
	if ( scaling != 0 && myStats->type != MINOTAUR && myStats->type != SHOPKEEPER
		&& my->monsterAllySummonRank == 0 && !my->monsterAllyGetPlayerLeader() && myStats->leader_uid == 0 )
	{
		int playerLevel = 1;
		for ( int p : activePlayers() ) { playerLevel = std::max<int>(playerLevel, stats[p]->LVL); }
		int monsterLevel = std::max<int>(1, myStats->LVL);
		if ( playerLevel > monsterLevel || scaling == 2 )
		{
			// Each level of difference changes health and damage by about 8%.
			double factor = std::max(0.5, std::min(3.0, 1.0 + 0.08 * (playerLevel - monsterLevel)));
			myStats->MAXHP = std::max(1, (int)(myStats->MAXHP * factor));
			myStats->HP = myStats->MAXHP;
			if ( myStats->STR > 0 ) { myStats->STR = (Sint32)(myStats->STR * factor); }
			if ( myStats->DEX > 0 ) { myStats->DEX = (Sint32)(myStats->DEX * (1.0 + (factor - 1.0) * 0.5)); }
			if ( myStats->CON > 0 ) { myStats->CON = (Sint32)(myStats->CON * factor); }
			myStats->LVL = std::max(1, playerLevel);
		}
	}

	// A boss keeps the damage from before a lost life.
	if ( isBoss && g_slot.boss_death == 1 && g_savedBossFloor == currentlevel )
	{
		auto it = g_savedBossHP.find(myStats->type);
		if ( it != g_savedBossHP.end() )
		{
			myStats->HP = std::min(myStats->MAXHP, std::max<Sint32>(1, it->second));
		}
	}


	// Difficulty scaling: weaken monsters that normally live deeper than the current floor.
	if ( (g_slot.floor_shuffle != 0 || g_slot.enemy_randomizer != 0) && g_slot.difficulty_scaling && g_slot.difficulty == 0
		&& !secretlevel && !isBoss && myStats->type != MINOTAUR && myStats->type != SHOPKEEPER
		&& my->monsterAllySummonRank == 0 && !my->monsterAllyGetPlayerLeader() )
	{
		int natural = biomeNaturalFloor();
		for ( auto& m : ap_ids::BESTIARY )
		{
			if ( m.monster == myStats->type ) { natural = std::max(natural, m.depth); break; }
		}
		if ( natural > currentlevel )
		{
			double factor = (currentlevel + 1.0) / (natural + 1.0);
			int hpCap = 10 + 8 * std::max(1, currentlevel);
			myStats->MAXHP = std::max(1, std::min((int)(myStats->MAXHP * factor), hpCap));
			myStats->HP = std::min(myStats->HP, myStats->MAXHP);
			if ( myStats->STR > 0 ) { myStats->STR = (Sint32)(myStats->STR * factor); }
			if ( myStats->CON > 0 ) { myStats->CON = (Sint32)(myStats->CON * factor); }
			myStats->LVL = std::max(1, (int)(myStats->LVL * factor));
		}
	}
}

void onMonsterDeath(Entity* my, Stat* myStats)
{
	if ( !g_ready || !my || !myStats || multiplayer == CLIENT ) { return; }
	if ( !g_countedDeaths.insert(my->getUID()).second ) { return; }
	if ( g_countedDeaths.size() > 4096 ) { g_countedDeaths.clear(); }
	if ( my->monsterAllySummonRank != 0 || my->monsterAllyGetPlayerLeader() ) { return; } // your own allies

	int type = myStats->type;
	if ( type == SHOPKEEPER ) { return; }

	++g_save.totalKills;
	++g_save.runKills;
	if ( g_bountyUid && my->getUID() == g_bountyUid ) { claimBounty(my); }
	if ( g_championUid && my->getUID() == g_championUid ) { claimChampion(my); }
	if ( type != MINOTAUR && type != LICH && type != DEVIL && type != LICH_ICE && type != LICH_FIRE )
	{
		++g_floorKills;
	}
	bool wasMarked = g_markedEnemies.erase(my->getUID()) > 0;
	if ( wasMarked && g_slot.enemy_sanity == 1 )
	{
		++g_save.greenKills;
		int per = std::max(1, g_slot.kill_check_interval);
		int toNext = per - (int)(g_save.greenKills % per);
		if ( toNext == per ) { toNext = 0; }
		sendKillChecks();
		if ( toNext > 0 ) { say(COLOR_AP, "Green enemy defeated! %d more for the next Enemy Sanity check.", toNext); }
		else { say(COLOR_AP, "Green enemy defeated! Enemy Sanity check sent."); }
		writeSave();
	}
	if ( wasMarked && g_slot.enemy_sanity == 2 )
	{
		int index = nextUnchecked(ap_ids::KILL_OFFSET, g_slot.kill_checks);
		if ( index > 0 )
		{
			sendLocation(ap_ids::BASE_ID + ap_ids::KILL_OFFSET + index);
			say(COLOR_AP, "Marked enemy defeated! (Enemy Sanity %d)", index);
		}
	}

	if ( g_slot.bestiary_checks != 0 )
	{
		for ( auto& m : ap_ids::BESTIARY )
		{
			if ( m.monster == type && (m.early || g_slot.bestiary_checks == 2) && m.depth < g_slot.goal_floor )
			{
				sendLocation(ap_ids::BASE_ID + ap_ids::BESTIARY_OFFSET + m.index);
			}
		}
	}

	// Goal bosses
	int goalMask = 0, goalBit = 0;
	if ( g_slot.goal == 0 ) { goalMask = 1; if ( type == LICH ) { goalBit = 1; } }
	else if ( g_slot.goal == 1 ) { goalMask = 1; if ( type == DEVIL ) { goalBit = 1; } }
	else if ( g_slot.goal == 3 || g_slot.goal == 4 )
	{
		// Boss Rush: every boss you picked; All Three Bosses: all of them (bits: Herx 1, Devil 2,
		// Ice Lich 4, Fire Lich 8). A boss beaten in any run stays beaten.
		goalMask = g_slot.goal == 4 ? 15 : bossRushGoalMask();
		if ( type == LICH ) { goalBit = 1; }
		else if ( type == DEVIL ) { goalBit = 2; }
		else if ( type == LICH_ICE ) { goalBit = 4; }
		else if ( type == LICH_FIRE ) { goalBit = 8; }
		goalBit &= goalMask;
	}
	else { goalMask = 3; if ( type == LICH_ICE ) { goalBit = 1; } else if ( type == LICH_FIRE ) { goalBit = 2; } }

	if ( goalBit )
	{
		g_save.goalBossesKilled |= goalBit;
		if ( (g_save.goalBossesKilled & goalMask) == goalMask && !g_save.goalSent )
		{
			g_save.goalSent = true;
			if ( g_save.finishSeconds < 0 ) { g_save.finishSeconds = g_save.playSeconds; }
			endRunRecord(RUN_WON);
			AP_StoryComplete();
			say(COLOR_AP, "Goal complete! Your world is done.");
		}
	}
	else if ( g_slot.boss_checks )
	{
		for ( auto& b : ap_ids::BOSSES )
		{
			if ( b.monster == type && b.floor <= g_slot.goal_floor )
			{
				sendLocation(ap_ids::BASE_ID + ap_ids::BOSS_OFFSET + b.index);
			}
		}
	}
	writeSave();
}

bool tryConsumeLife(int player)
{
	if ( !g_ready || multiplayer == CLIENT || client_disconnected[player] ) { return false; }
	Stat* s = stats[player];
	if ( !s ) { return false; }
	if ( g_deathScreen || g_restartPending )
	{
		s->HP = std::max<Sint32>(1, s->HP); // stay "dead" on the death screen until the restart
		return true;
	}
	if ( graceActive() && !g_dyingFromDeathLink )
	{
		s->HP = std::max<Sint32>(1, g_graceHP); // still safe from losing a life just after the last one
		return true;
	}
	if ( g_save.livesLeft <= 0 ) { return false; }

	--g_save.livesLeft;
	++g_save.runLivesLost;
	bool fromDeathLink = g_dyingFromDeathLink;
	g_dyingFromDeathLink = false;
	recordGrave(s);

	// Remember boss health on this floor, for "keep damage".
	g_savedBossHP.clear();
	g_savedBossFloor = currentlevel;
	if ( map.creatures )
	{
		for ( node_t* node = map.creatures->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			Stat* es = e ? e->getStats() : nullptr;
			if ( es && es->HP > 0 && (es->type == LICH || es->type == DEVIL || es->type == LICH_ICE || es->type == LICH_FIRE) )
			{
				g_savedBossHP[es->type] = es->HP;
			}
		}
	}

	if ( !fromDeathLink )
	{
		AP_DeathLinkSend("lost a life in the dungeon");
		recordDeath(g_slotName);
		addLog("You died (" + std::to_string(g_save.livesLeft) + (g_save.livesLeft == 1 ? " life" : " lives") + " left)", COLOR_TRAP);
	}

	if ( g_slot.death_items == 1 ) { countTakenLoot(); }

	// Freeze the player and show the death screen; restartFloor() runs on confirm.
	s->HP = 1;
	if ( Entity* ent = players[player]->entity )
	{
		// Frozen, untouchable and unseen until the restart: no more hits or blood.
		ent->setEffect(EFF_PARALYZED, true, TICKS_PER_SECOND * 3600, false);
		ent->setEffect(EFF_INVISIBLE, true, TICKS_PER_SECOND * 3600, false);
	}
	g_savedGodmode = godmode;
	godmode = true;
	g_confirmWasDown = true; // the button must be let go and pressed again
	g_deathScreen = true;
	g_deathScreenPlayer = player;
	g_deathScreenStart = ticks;
#ifdef USE_FMOD
	playMusic(gameovermusic, false, true, false);
#endif
	printlog("[Archipelago] Life lost. Lives left: %d", g_save.livesLeft);
	writeSave();
	return true;
}

void onPlayerDied(int player)
{
	if ( !g_ready || multiplayer == CLIENT ) { return; }
	bool fromDeathLink = g_dyingFromDeathLink;
	g_dyingFromDeathLink = false;
	if ( player == clientnum && stats[player] ) { recordGrave(stats[player]); }
	endRunRecord(RUN_GAME_OVER);
	if ( !fromDeathLink )
	{
		AP_DeathLinkSend("died in the dungeon");
		recordDeath(g_slotName);
		addLog("You died", COLOR_TRAP);
	}
}

bool canDescend(int player)
{
	if ( !g_ready || multiplayer == CLIENT ) { return true; }
	if ( g_floorNeedsKey && !g_floorKeyFound )
	{
		messagePlayerColor(player, MESSAGE_HINT, COLOR_AP, g_slot.floor_key_requirement == 2
			? "The stairs are locked. Defeat every enemy on this floor first."
			: g_slot.floor_key_requirement == 1 ? "The stairs are locked. Find this floor's Floor Key first."
			: "The stairs are locked. Find this floor's Floor Key, or defeat every enemy here.");
		return false;
	}
	if ( g_slot.champion_locks_stairs && championAlive() )
	{
		messagePlayerColor(player, MESSAGE_HINT, COLOR_TRAP, "The Champion guards the stairs. Defeat it first.");
		return false;
	}
	// Warn once before leaving a floor with Archipelago things still on it; using the stairs again leaves.
	FloorCounts c = countFloor();
	if ( c.total() > 0 && (g_stairsWarnedAt == 0 || ticks - g_stairsWarnedAt > TICKS_PER_SECOND * 15) )
	{
		g_stairsWarnedAt = ticks;
		messagePlayerColor(player, MESSAGE_HINT, COLOR_TRAP, "Wait! %s. Use the stairs again to leave anyway.",
			floorCountsText(c).c_str());
		return false;
	}
	return true;
}

uint32_t lockedMapSeed(uint32_t seed)
{
	if ( !g_ready || g_slot.layout_seed != 0 || multiplayer == CLIENT ) { return seed; }
	Uint32 h = (Uint32)g_slot.floor_shuffle_seed * 2654435761u ^ (Uint32)(currentlevel + 1) * 2246822519u
		^ (secretlevel ? 0x9E3779B9u : 0u);
	h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
	return h ? h : 1;
}

int adjustMinotaurTime(int ticks)
{
	if ( !g_ready ) { return ticks; }
	long long t = ticks;
	t = t * std::max(25, g_slot.minotaur_time) / 100; // Minotaur Time option (percent)
	t = t * currentFloorScale() / 100;                 // Floor Size: more ground, more time
	if ( g_slot.difficulty == 1 ) { t = t * 2 / 3; }
	return (int)t;
}

// ---------------------------------------------------------------- Archipelago item look
namespace
{
	int g_apModelIndex = -1;
	int g_floorKeyModelIndex = -1;
	constexpr const char* AP_MODEL_PATH = "models/items/AP_Item.vox";
	constexpr const char* FLOOR_KEY_MODEL_PATH = "models/items/AP_FloorKey.vox";

	// Adds one .vox model after the game's own; returns its index, or -1.
	int addModel(const char* file)
	{
		if ( !PHYSFS_getRealDir(file) ) { printlog("[Archipelago] %s not found.", file); return -1; }
		char path[128];
		snprintf(path, sizeof(path), "%s", file);
		voxel_t* model = loadVoxel(path);
		if ( !model ) { return -1; }
		voxel_t** grown = (voxel_t**)realloc(models, sizeof(voxel_t*) * (nummodels + 1));
		if ( grown ) { models = grown; }
		polymodel_t* grownPoly = (polymodel_t*)realloc(polymodels, sizeof(polymodel_t) * (nummodels + 1));
		if ( grownPoly ) { polymodels = grownPoly; }
		if ( !grown || !grownPoly ) { return -1; }
		int index = (int)nummodels;
		models[index] = model;
		memset(&polymodels[index], 0, sizeof(polymodel_t));
		nummodels += 1;
		generatePolyModels(index, index + 1, false);
		printlog("[Archipelago] %s loaded as model %d.", file, index);
		return index;
	}
}

bool isMarkedEnemy(const Entity* entity)
{
	if ( !entity || g_markedEnemies.empty() ) { return false; }
	if ( g_markedEnemies.count(entity->getUID()) ) { return true; }
	return g_markedEnemies.count(entity->parent) > 0; // body parts belong to their monster
}

bool isChampionEnemy(const Entity* entity)
{
	if ( !entity || !g_championUid ) { return false; }
	return entity->getUID() == g_championUid || entity->parent == g_championUid;
}

int lanternLightBonus(int player)
{
	// Progressive Lantern: from Lantern Darkness below normal up to one above, in three steps.
	if ( !g_ready || !g_slot.progressive_lantern || player != clientnum ) { return 0; }
	int dark = std::max(1, std::min(3, g_slot.lantern_darkness));
	int have = std::min(3, countReceived(ap_ids::ITEM_LANTERN));
	return -dark + (int)std::lround(have * (dark + 1) / 3.0);
}

bool isBountyEnemy(const Entity* entity)
{
	if ( !entity || !g_bountyUid ) { return false; }
	return entity->getUID() == g_bountyUid || entity->parent == g_bountyUid; // body parts too
}

bool isApItem(const Item* item)
{
	return item && item->type == AP_ITEM_TYPE
		&& (item->appearance == AP_FLOOR_APPEARANCE || item->appearance == AP_SHOP_APPEARANCE
			|| item->appearance == AP_CARAVAN_APPEARANCE || item->appearance == AP_HINT_APPEARANCE);
}

int apItemModel() { return g_apModelIndex; }
int floorKeyModel() { return g_floorKeyModelIndex; }
const char* apItemIconPath() { return "items/images/AP_Item.png"; }
const char* apItemName(const Item* item, bool withArticle)
{
	if ( item && item->appearance == AP_HINT_APPEARANCE ) { return withArticle ? "an Archipelago Hint" : "Archipelago Hint"; }
	return withArticle ? "an Archipelago item" : "Archipelago item";
}

// The main-menu title: Barony's own logo with the O swapped for the Archipelago logo. It's made here
// from the player's own copy of Barony's title.png (the mod only ships the small Archipelago piece),
// and saved next to the mod's other images so the tracker window can show it too.
namespace
{
	bool g_apTitleReady = false;
	const char* const AP_TITLE_PATH = "images/ui/Archipelago/AP_Title.png";

	void makeApTitle()
	{
		const char* titleDir = PHYSFS_getRealDir("images/system/title.png");
		const char* patchDir = PHYSFS_getRealDir("images/ui/Archipelago/AP_TitleO.png");
		if ( !titleDir || !patchDir ) { return; }
		std::string titlePath = std::string(titleDir) + "/images/system/title.png";
		std::string patchPath = std::string(patchDir) + "/images/ui/Archipelago/AP_TitleO.png";
		std::string outPath = std::string(patchDir) + "/" + AP_TITLE_PATH;

		SDL_Surface* rawTitle = IMG_Load(titlePath.c_str());
		SDL_Surface* rawPatch = IMG_Load(patchPath.c_str());
		SDL_Surface* title = rawTitle ? SDL_ConvertSurfaceFormat(rawTitle, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
		SDL_Surface* patch = rawPatch ? SDL_ConvertSurfaceFormat(rawPatch, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
		if ( rawTitle ) { SDL_FreeSurface(rawTitle); }
		if ( rawPatch ) { SDL_FreeSurface(rawPatch); }
		// Only Barony's standard 130x60 title is known; anything else is left alone.
		if ( title && patch && title->w == 130 && title->h == 60 && patch->w == 18 && patch->h == 18 )
		{
			auto at = [](SDL_Surface* sf, int x, int y) -> Uint8* { return (Uint8*)sf->pixels + y * sf->pitch + x * 4; };
			for ( int y = 9; y < 24; ++y )
			{
				for ( int x = 74; x < 87; ++x ) { memset(at(title, x, y), 0, 4); } // the O
			}
			for ( int y = 0; y < 18; ++y )
			{
				for ( int x = 0; x < 18; ++x )
				{
					Uint8* src = at(patch, x, y);
					if ( src[3] == 0 ) { continue; }
					Uint8* dst = at(title, 71 + x, 8 + y);
					bool outline = src[0] == 24 && src[1] == 16 && src[2] == 12;
					if ( outline && dst[3] != 0 ) { continue; } // outlines only go on empty pixels
					memcpy(dst, src, 4);
				}
			}
			g_apTitleReady = IMG_SavePNG(title, outPath.c_str()) == 0;
			printlog("[Archipelago] Main menu title %s.", g_apTitleReady ? "made" : "could not be saved");
		}
		if ( title ) { SDL_FreeSurface(title); }
		if ( patch ) { SDL_FreeSurface(patch); }
	}
}

// Floor Size: bigger generated floors, with more monsters and loot to match.
namespace { int g_floorSizePending = 100; int g_currentFloorScale = 100; }

static int floorSizePercent()
{
	if ( !g_ready || multiplayer == CLIENT ) { return 100; }
	// Floor Size: the sizes ticked in the options (bit 1 Normal, 2 Bigger, 4 Much Bigger). With several,
	// each floor's size comes from the seed and the floor, so it's the same every time you're there.
	static const int SIZES[] = { 100, 120, 140 };
	std::vector<int> pool;
	for ( int b = 0; b < 3; ++b ) { if ( g_slot.floor_size_mask & (1 << b) ) { pool.push_back(SIZES[b]); } }
	if ( pool.empty() ) { return 100; }
	if ( pool.size() == 1 ) { return pool[0]; }
	Uint32 h = (Uint32)g_slot.floor_shuffle_seed * 2654435761u ^ (Uint32)(currentlevel + 1) * 40503u ^ (secretlevel ? 0x9E37u : 0u);
	h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
	return pool[h % pool.size()];
}

void requestFloorSize()
{
	g_floorSizePending = floorSizePercent();
}

int takeFloorSizePercent()
{
	int pct = g_floorSizePending;
	g_floorSizePending = 100;
	g_currentFloorScale = pct; // every floor load passes here: hand-made floors get 100
	return pct;
}

// How much bigger the current floor is across (100 = normal). The Minotaur's timer and the key
// compass stretch by this, since there's that much more ground to cover.
int currentFloorScale()
{
	return std::max(100, g_currentFloorScale);
}

int scaleFloorEntities(int count)
{
	int pct = floorSizePercent();
	if ( pct <= 100 ) { return count; }
	return count * pct * pct / 10000; // the area grows with the square of the size
}

int lockedClass(int player, int currentClass)
{
	// Lock Class: with Skill Checks set to a class, new characters always get that class (looks,
	// race and name are still yours to pick). A DLC class you don't own isn't forced.
	if ( !g_ready || !g_slot.lock_class || g_slot.skill_checks < 2 ) { return currentClass; }
	if ( player != clientnum ) { return currentClass; }
	int chosen = g_slot.skill_checks - 2;
	if ( chosen < 0 || chosen >= ap_ids::NUM_SKILL_CLASSES ) { return currentClass; }
	int pack = ap_ids::CLASS_DLC_PACK[chosen];
	if ( (pack == 1 && !enabledDLCPack1) || (pack == 2 && !enabledDLCPack2) || (pack == 3 && !enabledDLCPack3) )
	{
		return currentClass;
	}
	return chosen;
}

bool isCompassItem(const Item* item) { return isCompassItemPrivate(item); }
bool useCompassItem(Item* item, int player) { return useCompassPrivate(item, player); }

bool isFloorKey(const Item* item)
{
	return item && item->type == FLOOR_KEY_TYPE && item->appearance == FLOOR_KEY_APPEARANCE;
}

const char* titleImagePath()
{
	return g_apTitleReady ? "*images/ui/Archipelago/AP_Title.png" : "*images/system/title.png";
}

// DLC: this build has no Steam support compiled in, so it asks Steam directly, through the
// steam_api64.dll that comes with Barony, which DLC packs you own. Only owned packs are turned on.
namespace
{
	void checkSteamDlc()
	{
#ifdef WINDOWS
		HMODULE steam = LoadLibraryA("steam_api64.dll");
		if ( !steam ) { printlog("[Archipelago] steam_api64.dll not found; DLC stays off."); return; }
		using InitFn = bool (*)();
		using AppsFn = void* (*)();
		using DlcFn = bool (*)(void*, unsigned int);
		auto init = (InitFn)GetProcAddress(steam, "SteamAPI_InitSafe");
		auto apps = (AppsFn)GetProcAddress(steam, "SteamAPI_SteamApps_v008");
		auto installed = (DlcFn)GetProcAddress(steam, "SteamAPI_ISteamApps_BIsDlcInstalled");
		if ( !init || !apps || !installed ) { printlog("[Archipelago] Unknown steam_api64.dll version; DLC stays off."); return; }
		if ( !init() ) { printlog("[Archipelago] Steam isn't running (start Barony from Steam); DLC stays off."); return; }
		void* a = apps();
		if ( !a ) { return; }
		if ( installed(a, 1010820) ) { enabledDLCPack1 = true; }
		if ( installed(a, 1010821) ) { enabledDLCPack2 = true; }
		if ( installed(a, 1010822) ) { enabledDLCPack3 = true; }
		printlog("[Archipelago] Steam DLC: Myths and Outcasts %s, Legends and Pariahs %s, Deserters and Disciples %s.",
			enabledDLCPack1 ? "owned" : "not owned", enabledDLCPack2 ? "owned" : "not owned",
			enabledDLCPack3 ? "owned" : "not owned");
#endif
	}
}

void loadExtraModels()
{
	makeApTitle();
	checkSteamDlc();
	g_apModelIndex = addModel(AP_MODEL_PATH);
	g_floorKeyModelIndex = addModel(FLOOR_KEY_MODEL_PATH);
}

// ---------------------------------------------------------------- Enemy Randomizer
namespace
{
	Uint32 g_enemyTableKey = 0;
	std::mt19937 g_enemySpawnRng;
	Uint32 g_enemyTableLoad = 0;
	std::map<int, int> g_enemyTable; // vanilla type -> replacement, for this floor

	std::vector<int> enemyPool()
	{
		std::vector<int> pool;
		for ( auto& m : ap_ids::BESTIARY )
		{
			if ( g_slot.enemy_pool == 0 && m.depth > currentlevel + 10 ) { continue; }
			pool.push_back(m.monster);
		}
		return pool;
	}
}

// ---------------------------------------------------------------- Archipelago items in containers
namespace
{
	int apItemsLyingAround()
	{
		int n = 0;
		if ( !map.entities ) { return 0; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( e && e->behavior == &actItem && e->skill[10] == AP_ITEM_TYPE
				&& ((Uint32)e->skill[14] == AP_FLOOR_APPEARANCE) ) { ++n; }
		}
		return n;
	}

	bool apItemAvailable()
	{
		return g_ready && multiplayer != CLIENT && g_slot.archipelago_pickups > 0 && currentlevel >= 1
			&& uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups) > apItemsLyingAround();
	}
}

void onChestCreated(Entity* chest)
{
	if ( !chest || chest->behavior != &actChest || !chest->children.first ) { return; }
	rerollItemList((list_t*)chest->children.first->element); // Randomize Loot
	if ( !apItemAvailable() ) { return; }
	if ( g_floorReady ) { g_attemptApMade = 0; g_floorReady = false; } // first chest of a new floor
	bool restart = g_restartPending;
	if ( restart && g_attemptApMade >= g_restartApBudget ) { return; }
	if ( local_rng.rand() % 100 >= 25 ) { return; } // 1 in 4 chests
	list_t* inventory = (list_t*)chest->children.first->element;
	if ( inventory )
	{
		newItem(AP_ITEM_TYPE, EXCELLENT, 0, 1, AP_FLOOR_APPEARANCE, true, inventory);
		++g_attemptApMade;
		if ( !restart ) { ++g_floorApTotal; }
	}
}

void onBreakableDestroyed(Entity* breakable)
{
	if ( !breakable || !breakable->isColliderBreakableContainer() || !apItemAvailable() ) { return; }
	if ( g_floorIsRestart && g_attemptApMade >= g_restartApBudget ) { return; }
	if ( local_rng.rand() % 100 >= 8 ) { return; } // 1 in 12 crates, pots and barrels
	dropItemMonster(newItem(AP_ITEM_TYPE, EXCELLENT, 0, 1, AP_FLOOR_APPEARANCE, true, nullptr), breakable, nullptr);
	noteApItemMade();
}

void noteSecretExit(int secretLevelExit)
{
	g_secretExitThisFloor = secretLevelExit;
}

// Where the Enemy Randomizer works: every floor from floor 1, but never the safe places (the starting
// area, the town, Minetown) or boss arenas; other secret areas only with Enemy Randomizer: Secret Areas.
static bool enemyRandomizerHere()
{
	if ( currentlevel < 1 || isSpecialMap() ) { return false; }
	if ( !strncmp(map.name, "Minetown", 8) ) { return false; }
	if ( secretlevel && !g_slot.enemy_randomizer_secret ) { return false; }
	return true;
}

int randomizeMonster(int monsterType)
{
	if ( !g_ready || g_slot.enemy_randomizer == 0 || multiplayer == CLIENT ) { return monsterType; }
	if ( !enemyRandomizerHere() ) { return monsterType; }
	if ( monsterType == SHOPKEEPER || monsterType == MINOTAUR || monsterType == LICH || monsterType == DEVIL
		|| monsterType == LICH_ICE || monsterType == LICH_FIRE || monsterType == NOTHING ) { return monsterType; }
	auto pool = enemyPool();
	if ( pool.empty() ) { return monsterType; }

	// Locked: the same floor always gets the same enemies, through deaths, restarts and new runs.
	// Re-roll: every time a floor loads (including after a lost life) the enemies are rolled again.
	Uint32 key = (Uint32)g_slot.floor_shuffle_seed ^ (Uint32)(currentlevel * 2654435761u) ^ (secretlevel ? 0x5EC2E7u : 0u);
	if ( g_slot.enemy_seed == 1 )
	{
		key ^= mapseed * 2246822519u ^ (Uint32)(g_floorLoads * 3266489917u);
	}
	if ( key != g_enemyTableKey || g_floorLoads != g_enemyTableLoad )
	{
		// New floor load: start the floor's sequence from the top, so a locked floor repeats exactly.
		g_enemyTableKey = key;
		g_enemyTableLoad = g_floorLoads;
		g_enemyTable.clear();
		g_enemySpawnRng.seed(key);
	}

	if ( g_slot.enemy_randomizer == 2 )
	{
		// Every spawn: each monster is its own roll (from a floor-seeded sequence when locked).
		return pool[g_enemySpawnRng() % pool.size()];
	}

	// Per floor: each kind of monster is swapped for one random kind (it can roll itself).
	auto it = g_enemyTable.find(monsterType);
	if ( it != g_enemyTable.end() ) { return it->second; }
	std::mt19937 rng(key ^ (Uint32)(monsterType * 40503u));
	int replacement = pool[rng() % pool.size()];
	g_enemyTable[monsterType] = replacement;
	return replacement;
}

int randomizePlacedMonster(int monsterType, bool named)
{
	// Hand-made floors: their own spawn lists and monsters placed in the map editor. Named characters
	// are left alone, and so is everywhere enemyRandomizerHere() rules out.
	if ( !g_ready || g_slot.enemy_randomizer == 0 || multiplayer == CLIENT ) { return monsterType; }
	if ( named || !enemyRandomizerHere() ) { return monsterType; }
	bool isEnemy = false;
	for ( auto& m : ap_ids::BESTIARY ) { if ( m.monster == monsterType ) { isEnemy = true; break; } }
	if ( !isEnemy ) { return monsterType; } // bots, decoys and other special things
	return randomizeMonster(monsterType);
}

// ---------------------------------------------------------------- Music Mods
// Music Mods: Workshop or local mods that replace Barony's music (same file names, like
// music/mines01.ogg) are mixed in with the original soundtrack, or replace it. The mods are read
// from your own Steam Workshop folder and the game's mods folder; nothing is downloaded or shipped.
namespace
{
	std::map<std::string, std::vector<std::string>> g_modMusic; // "mines01.ogg" -> files from mods
	bool g_modMusicScanned = false;
#ifdef USE_FMOD
	std::map<std::string, FMOD::Sound*> g_modMusicLoaded;      // file -> loaded stream
	std::map<FMOD::Sound*, FMOD::Sound*> g_musicChoice;        // this floor's pick for each track
	Uint32 g_musicChoiceLoad = 0xFFFFFFFFu;
#endif

	void scanModMusic()
	{
		if ( g_modMusicScanned ) { return; }
		g_modMusicScanned = true;
		namespace fs = std::filesystem;
		std::vector<fs::path> roots = { fs::path("mods"), fs::path("../../workshop/content/371970") };
		std::error_code ec;
		for ( auto& root : roots )
		{
			if ( !fs::is_directory(root, ec) ) { continue; }
			for ( auto& mod : fs::directory_iterator(root, ec) )
			{
				fs::path dir = mod.path() / "music";
				if ( !fs::is_directory(dir, ec) ) { continue; }
				int count = 0;
				for ( auto& f : fs::directory_iterator(dir, ec) )
				{
					if ( !f.is_regular_file(ec) ) { continue; }
					std::string name = f.path().filename().string();
					std::string lower = name;
					std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
					if ( lower.size() < 4 || lower.substr(lower.size() - 4) != ".ogg" ) { continue; }
					g_modMusic[lower].push_back(f.path().string());
					++count;
				}
				if ( count ) { printlog("[Archipelago] Music Mods: %d tracks in %s", count, dir.string().c_str()); }
			}
		}
	}
#ifdef USE_FMOD
	std::string musicSlotName(FMOD::Sound* sound)
	{
		char buf[64];
		auto arr = [&](FMOD::Sound** a, int n, const char* fmt) -> bool {
			if ( !a ) { return false; }
			for ( int i = 0; i < n; ++i ) { if ( a[i] == sound ) { snprintf(buf, sizeof(buf), fmt, i); return true; } }
			return false;
		};
		if ( arr(minesmusic, NUMMINESMUSIC, "mines%02d.ogg") || arr(swampmusic, NUMSWAMPMUSIC, "swamp%02d.ogg")
			|| arr(labyrinthmusic, NUMLABYRINTHMUSIC, "labyrinth%02d.ogg") || arr(ruinsmusic, NUMRUINSMUSIC, "ruins%02d.ogg")
			|| arr(underworldmusic, NUMUNDERWORLDMUSIC, "underworld%02d.ogg") || arr(hellmusic, NUMHELLMUSIC, "hell%02d.ogg")
			|| arr(minotaurmusic, NUMMINOTAURMUSIC, "minotaur%02d.ogg") || arr(cavesmusic, NUMCAVESMUSIC, "caves%02d.ogg")
			|| arr(citadelmusic, NUMCITADELMUSIC, "citadel%02d.ogg") || arr(fortressmusic, NUMFORTRESSMUSIC, "fortress%02d.ogg") )
		{
			return buf;
		}
		if ( intromusic )
		{
			for ( int i = 0; i < NUMINTROMUSIC; ++i )
			{
				if ( intromusic[i] == sound ) { return i == 0 ? std::string("intro.ogg") : "intro" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".ogg"; }
			}
		}
		const std::pair<FMOD::Sound*, const char*> singles[] = {
			{ introductionmusic, "introduction.ogg" }, { intermissionmusic, "intermission.ogg" }, { minetownmusic, "minetown.ogg" },
			{ splashmusic, "splash.ogg" }, { librarymusic, "library.ogg" }, { shopmusic, "shop.ogg" }, { herxmusic, "herxboss.ogg" },
			{ templemusic, "temple.ogg" }, { endgamemusic, "endgame.ogg" }, { escapemusic, "escape.ogg" }, { devilmusic, "devil.ogg" },
			{ sanctummusic, "sanctum.ogg" }, { gnomishminesmusic, "gnomishmines.ogg" }, { greatcastlemusic, "greatcastle.ogg" },
			{ sokobanmusic, "sokoban.ogg" }, { caveslairmusic, "caveslair.ogg" }, { bramscastlemusic, "bramscastle.ogg" },
			{ hamletmusic, "hamlet.ogg" }, { tutorialmusic, "tutorial.ogg" },
		};
		for ( auto& p : singles ) { if ( p.first && p.first == sound ) { return p.second; } }
		return std::string();
	}
#endif
}

void* pickMusicVersion(void* soundPtr)
{
#ifdef USE_FMOD
	FMOD::Sound* sound = (FMOD::Sound*)soundPtr;
	if ( !sound || !g_ready || g_slot.music_mods == 0 || !fmod_system ) { return soundPtr; }
	scanModMusic();
	if ( g_modMusic.empty() ) { return soundPtr; }
	// Our own mod streams pass straight through.
	for ( auto& l : g_modMusicLoaded ) { if ( l.second == sound ) { return soundPtr; } }
	// One pick per track per floor, so the music doesn't flip while you play.
	if ( g_musicChoiceLoad != g_floorLoads ) { g_musicChoice.clear(); g_musicChoiceLoad = g_floorLoads; }
	auto chosen = g_musicChoice.find(sound);
	if ( chosen != g_musicChoice.end() ) { return chosen->second; }

	FMOD::Sound* result = sound;
	// Which track is this? Match the game's own music slots (FMOD's getName can return the song's
	// title tag instead of its file name, so it isn't reliable).
	std::string name = musicSlotName(sound);
	if ( name.empty() )
	{
		char nameBuf[512] = "";
		if ( sound->getName(nameBuf, sizeof(nameBuf)) == FMOD_OK )
		{
			name = std::filesystem::path(nameBuf).filename().string();
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		}
	}
	if ( !name.empty() )
	{
		auto it = g_modMusic.find(name);
		// Mixed: original or one of the mod versions, all equally likely. Mods Only: always a mod version.
		if ( it != g_modMusic.end() && !it->second.empty() )
		{
			int options = (int)it->second.size() + (g_slot.music_mods == 1 ? 1 : 0);
			int pick = local_rng.rand() % options;
			if ( pick < (int)it->second.size() )
			{
				const std::string& file = it->second[pick];
				FMOD::Sound* alt = nullptr;
				auto loaded = g_modMusicLoaded.find(file);
				if ( loaded != g_modMusicLoaded.end() ) { alt = loaded->second; }
				else if ( fmod_system->createStream(file.c_str(), FMOD_2D, nullptr, &alt) == FMOD_OK )
				{
					g_modMusicLoaded[file] = alt;
				}
				if ( alt ) { result = alt; }
			}
			printlog("[Archipelago] Music Mods: %s plays %s.", name.c_str(),
				result == sound ? "the original" : "a mod version");
		}
	}
	g_musicChoice[sound] = result;
	return result;
#else
	return soundPtr;
#endif
}

// ---------------------------------------------------------------- Music Shuffle
namespace
{
	// Floors whose music can be shuffled, and the names whose music they can borrow.
	const char* const BIOME_NAMES[] = {
		"The Mines", "The Swamp", "The Labyrinth", "The Ruins", "Hell", "Caves", "Citadel", "Underworld",
	};
	const char* const SPECIAL_NAMES[] = {
		"Minetown", "The Gnomish Mines", "The Haunted Castle", "Sokoban", "Cockatrice Lair",
		"Bram's Castle", "The Mystic Library", "The Temple", "Mages Guild",
	};
}

MusicOverride::MusicOverride()
{
	if ( !g_ready || g_slot.music_shuffle == 0 ) { return; }
	bool shuffleable = false;
	for ( const char* b : BIOME_NAMES )
	{
		if ( !strncmp(map.name, b, strlen(b)) && strcmp(map.name, "Hell Boss") ) { shuffleable = true; break; }
	}
	if ( !shuffleable ) { return; }
	std::vector<const char*> choices(std::begin(BIOME_NAMES), std::end(BIOME_NAMES));
	if ( g_slot.music_shuffle == 2 )
	{
		choices.insert(choices.end(), std::begin(SPECIAL_NAMES), std::end(SPECIAL_NAMES));
	}
	// Locked: a floor always plays the same music. Random: the game only reads this name when
	// a song starts, so picking fresh each time gives a random song every time one starts.
	Uint32 key = g_slot.music_seed == 1 ? local_rng.getU32()
		: (Uint32)g_slot.floor_shuffle_seed ^ (Uint32)(currentlevel * 2246822519u);
	const char* pick = choices[key % choices.size()];
	memcpy(saved, map.name, sizeof(saved));
	snprintf(map.name, sizeof(map.name), "%s", pick);
	active = true;
}

MusicOverride::~MusicOverride()
{
	if ( active ) { memcpy(map.name, saved, sizeof(saved)); }
}

namespace
{
	const char* HUD_FONT = "fonts/pixel_maz_multiline.ttf#16#2";
	const char* TITLE_FONT = "fonts/pixelmix.ttf#32#2";
	const char* BIG_FONT = "fonts/pixelmix.ttf#16#2";
	const Uint32 OUTLINE = makeColor(0, 0, 0, 255);

	SDL_Rect virtualViewport()
	{
		return SDL_Rect{ 0, 0, Frame::virtualScreenX, Frame::virtualScreenY };
	}

	// Draws text; align 0 = left edge at x, 1 = centered on x, 2 = right edge at x. Returns its width.
	int drawText(const char* str, const char* font, Uint32 color, int x, int y, int align = 0)
	{
		Text* t = Text::get(str, font, color, OUTLINE);
		if ( !t ) { return 0; }
		int w = (int)t->getWidth();
		int dx = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
		t->draw(SDL_Rect{ 0, 0, 0, 0 }, SDL_Rect{ dx, y, 0, 0 }, virtualViewport());
		return w;
	}

	void drawImage(const char* path, int x, int y, int w, int h, Uint32 color = 0xFFFFFFFF)
	{
		if ( Image* img = Image::get(path) )
		{
			img->drawColor(nullptr, SDL_Rect{ x, y, w > 0 ? w : (int)img->getWidth(), h > 0 ? h : (int)img->getHeight() },
				virtualViewport(), color);
		}
	}

	void drawDeathScreen()
	{
		const int W = Frame::virtualScreenX, H = Frame::virtualScreenY;
		Uint32 elapsed = ticks - g_deathScreenStart;
		Uint8 alpha = (Uint8)std::min<Uint32>(170, elapsed * 6);
		drawImage("images/system/white.png", 0, 0, W, H, makeColor(40, 0, 0, alpha));
		drawText("You died!", TITLE_FONT, makeColorRGB(220, 50, 40), W / 2, H / 2 - 70, 1);
		char buf[96];
		snprintf(buf, sizeof(buf), "Life lost. Lives left: %d", g_save.livesLeft);
		drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), W / 2, H / 2 - 18, 1);
		if ( elapsed >= DEATH_SCREEN_MIN_TICKS )
		{
			const char* what = g_slot.floor_restart == 1 ? "to try this floor again" : "to restart the floor";
			std::string glyph = Input::inputs[clientnum].getGlyphPathForBinding("MenuConfirm", ticks % 50 < 25);
			Image* g = glyph.empty() ? nullptr : Image::get(glyph.c_str());
			Text* t = Text::get((std::string("Press ") + " " + std::string(what)).c_str(), BIG_FONT, makeColorRGB(255, 200, 90), OUTLINE);
			int gw = g ? (int)g->getWidth() : 0;
			int tw = t ? (int)t->getWidth() : 0;
			int left = W / 2 - (tw + gw + 8) / 2;
			int y = H / 2 + 24;
			int pw = drawText("Press", BIG_FONT, makeColorRGB(255, 200, 90), left, y);
			if ( g )
			{
				g->draw(nullptr, SDL_Rect{ left + pw + 6, y - ((int)g->getHeight() - 16) / 2, (int)g->getWidth(), (int)g->getHeight() }, virtualViewport());
			}
			drawText(what, BIG_FONT, makeColorRGB(255, 200, 90), left + pw + 6 + gw + 6, y);
		}
	}
}

void drawHud()
{
	if ( !g_started || nohud ) { return; }

	// Top left: connection status.
	bool connected = isConnected();
	bool connecting = !g_ready && AP_GetConnectionStatus() != AP_ConnectionStatus::ConnectionRefused;
	const char* label = connected ? "Archipelago Connected" : connecting ? "Archipelago Connecting..." : "Archipelago Disconnected";
	Uint32 color = connected ? makeColorRGB(120, 230, 120) : connecting ? makeColorRGB(240, 210, 90) : makeColorRGB(240, 90, 80);
	drawImage("*images/ui/Archipelago/AP_Logo.png", 8, 6, 24, 24,
		connected ? 0xFFFFFFFF : makeColor(255, 255, 255, 110));
	drawText(label, BIG_FONT, color, 38, 10);
	if ( g_ready && inGame() )
	{
		// What's left on the floor is in the tracker window (and the stairs warn you), not here.
		int row = 32;
		// The Minotaur: how long until he arrives, or that he's here.
		if ( g_minotaurSecs >= 0 )
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "Minotaur arrives in %d:%02d", g_minotaurSecs / 60, g_minotaurSecs % 60);
			bool urgent = g_minotaurSecs <= 30;
			Uint32 mc = urgent && (ticks / (TICKS_PER_SECOND / 2)) % 2 ? makeColorRGB(255, 70, 60) : makeColorRGB(240, 120, 90);
			drawText(buf, BIG_FONT, mc, 38, row);
			row += 22;
		}
		else if ( g_minotaurSecs == -2 )
		{
			drawText("The Minotaur is hunting you!", BIG_FONT, makeColorRGB(255, 70, 60), 38, row);
			row += 22;
		}
		if ( graceActive() )
		{
			char gb[64];
			snprintf(gb, sizeof(gb), "Safe: %.1f s", (g_graceUntil - ticks) / (double)TICKS_PER_SECOND);
			drawText(gb, BIG_FONT, makeColorRGB(120, 200, 255), 38, row);
			row += 22;
		}
		if ( g_floorNeedsKey && !g_floorKeyFound )
		{
			std::string keyLine;
			std::string left = g_enemiesLeft > 0 ? " (" + std::to_string(g_enemiesLeft) + " left)" : std::string();
			if ( g_slot.floor_key_requirement == 2 )
			{
				keyLine = "Defeat all enemies" + left;
			}
			else
			{
				keyLine = g_slot.floor_key_requirement == 0 ? "Find the Floor Key or defeat all enemies" + left
					: std::string("Find the Floor Key");
				if ( compassLevel() > 0 && g_slot.compass_mode == 0 && ticks >= g_compassPingUntil && ticks < g_compassReadyAt )
				{
					int secs = (int)((g_compassReadyAt - ticks) / TICKS_PER_SECOND) + 1;
					char cb[32];
					snprintf(cb, sizeof(cb), "  [compass %d:%02d]", secs / 60, secs % 60);
					keyLine += cb;
				}
			}
			drawText(keyLine.c_str(), HUD_FONT, makeColorRGB(255, 200, 90), 38, row);
			row += 18;
		}
		// Key hint: only for the first moments of a floor, so the corner stays clean.
		if ( g_logPanel == 0 && ticks - g_floorStartTicks < (Uint32)(TICKS_PER_SECOND * 12) )
		{
			drawText("F1: items   F2: log   F3: deaths   F4: tracker loot list", HUD_FONT, makeColorRGB(140, 140, 140), 38, row);
		}
	}
	if ( (g_hofPanel || (g_hofAutoUntil && ticks < g_hofAutoUntil && currentlevel == 0)) && !g_deathScreen )
	{
		// Hall of Fame: the best of every run on this seed, and the last few runs.
		auto rows = pastRuns();
		int deepest = 0, mostKills = 0, wins = 0;
		long fastest = -1, mostGold = 0;
		int fewestLives = -1;
		for ( auto& r : rows )
		{
			deepest = std::max(deepest, r.deepest);
			mostKills = std::max(mostKills, r.kills);
			mostGold = std::max(mostGold, r.gold);
			if ( r.result == RUN_WON )
			{
				++wins;
				if ( fastest < 0 || r.secs < fastest ) { fastest = r.secs; }
				if ( fewestLives < 0 || r.lives < fewestLives ) { fewestLives = r.lives; }
			}
		}
		auto hms = [](long t) { char b[32]; snprintf(b, sizeof(b), "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60); return std::string(b); };
		const int x = 12, y = 80, w = 460, lineH = 22;
		int shown = (int)std::min<size_t>(rows.size(), 5);
		int h = 214 + lineH * std::max(1, shown);
		drawImage("images/system/white.png", x, y, w, h, makeColor(10, 8, 6, 210));
		drawText(g_hofPanel ? "Hall of Fame (F5 to close)" : "Hall of Fame (F5)", BIG_FONT, makeColorRGB(255, 200, 90), x + 8, y + 6);
		char buf[160];
		Uint32 txt = makeColorRGB(225, 220, 210), dim = makeColorRGB(150, 145, 135), gold = makeColorRGB(255, 200, 90);
		int ly = y + 38;
		snprintf(buf, sizeof(buf), "Runs %d   Wins %d", (int)rows.size(), wins); drawText(buf, HUD_FONT, txt, x + 12, ly); ly += lineH;
		snprintf(buf, sizeof(buf), "Deepest floor  %d", deepest); drawText(buf, HUD_FONT, txt, x + 12, ly); ly += lineH;
		snprintf(buf, sizeof(buf), "Fastest win  %s", fastest >= 0 ? hms(fastest).c_str() : "-"); drawText(buf, HUD_FONT, fastest >= 0 ? gold : dim, x + 12, ly); ly += lineH;
		if ( fewestLives >= 0 ) { snprintf(buf, sizeof(buf), "Fewest lives lost in a win  %d", fewestLives); } else { snprintf(buf, sizeof(buf), "Fewest lives lost in a win  -"); }
		drawText(buf, HUD_FONT, fewestLives >= 0 ? gold : dim, x + 12, ly); ly += lineH;
		snprintf(buf, sizeof(buf), "Most kills in a run  %d   Most gold  %ld", mostKills, mostGold); drawText(buf, HUD_FONT, txt, x + 12, ly); ly += lineH;
		if ( g_slot.bounty_enemies ) { snprintf(buf, sizeof(buf), "Bounties claimed  %ld", g_save.bountyKills); drawText(buf, HUD_FONT, txt, x + 12, ly); }
		ly += lineH + 6;
		drawText("Last runs", HUD_FONT, gold, x + 12, ly); ly += lineH;
		if ( rows.empty() ) { drawText("No runs finished yet.", HUD_FONT, dim, x + 12, ly); }
		static const char* const RESULT[] = { "playing", "game over", "won", "left" };
		for ( int i = 0; i < shown; ++i )
		{
			const RunRow& r = rows[rows.size() - 1 - i];
			snprintf(buf, sizeof(buf), "#%d  floor %d  %d kills  %s  %s", (int)(rows.size() - i), r.deepest, r.kills,
				RESULT[std::max(0, std::min(3, r.result))], hms(r.secs).c_str());
			drawText(buf, HUD_FONT, r.result == RUN_WON ? gold : txt, x + 12, ly);
			ly += lineH;
		}
	}
	else if ( g_logPanel == 3 )
	{
		// Death counter: total, then everyone who has died, most deaths first.
		std::vector<std::pair<std::string, int>> rows(g_save.deaths.begin(), g_save.deaths.end());
		std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second > b.second; });
		const int x = 12, y = 80, w = 360, lineH = 22;
		int h = 70 + lineH * (int)std::max<size_t>(1, rows.size());
		drawImage("images/system/white.png", x, y, w, h, makeColor(10, 8, 6, 200));
		drawText("Deaths (F3 to close)", BIG_FONT, makeColorRGB(255, 200, 90), x + 8, y + 6);
		char buf[64];
		drawImage("*images/ui/Archipelago/AP_Skull.png", x + 10, y + 32, 24, 24);
		snprintf(buf, sizeof(buf), "Total  %d", totalDeaths());
		drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), x + 42, y + 34);
		int ly = y + 64;
		if ( rows.empty() ) { drawText("Nobody has died yet.", HUD_FONT, makeColorRGB(170, 170, 170), x + 12, ly); }
		for ( auto& r : rows )
		{
			drawImage("*images/ui/Archipelago/AP_Skull.png", x + 14, ly, 16, 16);
			Uint32 col = r.first == g_slotName ? makeColorRGB(255, 200, 90) : makeColorRGB(225, 220, 210);
			drawText(r.first.c_str(), HUD_FONT, col, x + 38, ly);
			snprintf(buf, sizeof(buf), "%d", r.second);
			drawText(buf, HUD_FONT, col, x + w - 14, ly, 2);
			ly += lineH;
		}
	}
	else if ( g_logPanel != 0 )
	{
		std::vector<const LogLine*> lines;
		for ( auto it = g_log.rbegin(); it != g_log.rend() && lines.size() < 16; ++it )
		{
			if ( g_logPanel == 2 || it->item ) { lines.push_back(&*it); }
		}
		const int x = 12, y = 80, w = 520, lineH = 18;
		int h = 30 + lineH * (int)std::max<size_t>(1, lines.size());
		drawImage("images/system/white.png", x, y, w, h, makeColor(10, 8, 6, 200));
		drawText(g_logPanel == 1 ? "Archipelago items (F1 to close)" : "Archipelago log (F2 to close)",
			BIG_FONT, makeColorRGB(255, 200, 90), x + 8, y + 6);
		int ly = y + 28;
		if ( lines.empty() ) { drawText("Nothing yet.", HUD_FONT, makeColorRGB(170, 170, 170), x + 8, ly); }
		for ( auto rit = lines.rbegin(); rit != lines.rend(); ++rit, ly += lineH )
		{
			drawText((*rit)->text.c_str(), HUD_FONT, (*rit)->color, x + 8, ly);
		}
	}

	// Top right, under Barony's player bar: lives, then the death counter.
	constexpr int HUD_RIGHT_TOP = 60;
	if ( g_ready && inGame() && (g_slot.lives > 0 || g_save.livesLeft > 0) )
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "x %d", g_save.livesLeft);
		const int right = Frame::virtualScreenX - 12;
		const int top = HUD_RIGHT_TOP; // under Barony's own player bar in the top right
		int w = drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), right, top, 2);
		drawImage("*images/ui/Archipelago/AP_Life.png", right - w - 32, top - 4, 26, 24);
	}
	if ( g_ready && inGame() )
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "x %d", totalDeaths());
		const int right = Frame::virtualScreenX - 12;
		const int top = HUD_RIGHT_TOP + ((g_slot.lives > 0 || g_save.livesLeft > 0) ? 28 : 0);
		int w = drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), right, top, 2);
		drawImage("*images/ui/Archipelago/AP_Skull.png", right - w - 32, top - 3, 24, 24);
	}

	// Compass face under the XP bar, arrow pointing the way to the Floor Key from where you face.
	if ( g_ready && inGame() && g_compassShowing && !g_deathScreen )
	{
		const int size = 72;
		const int x = Frame::virtualScreenX / 2 - size / 2, y = 56;
		drawImage("*images/ui/Archipelago/AP_CompassFace.png", x, y, size, size);
		char arrow[64];
		snprintf(arrow, sizeof(arrow), "*images/ui/Archipelago/AP_CompassArrow%d.png", g_compassArrow);
		drawImage(arrow, x, y, size, size);
		char label[48];
		if ( g_compassTiles <= 1 ) { snprintf(label, sizeof(label), "Floor Key: right here"); }
		else if ( g_compassShowsDistance ) { snprintf(label, sizeof(label), "Floor Key: %d tiles", g_compassTiles); }
		else { snprintf(label, sizeof(label), "Floor Key"); }
		drawText(label, BIG_FONT, makeColorRGB(255, 200, 90), Frame::virtualScreenX / 2, y + size + 4, 1);
	}

	if ( g_deathScreen && inGame() )
	{
		drawDeathScreen();
	}
}

const char* bossRushNextMap()
{
	static std::string chosen;
	chosen = bossRushMapFor(currentlevel);
	return chosen.empty() ? nullptr : chosen.c_str();
}

bool bossRushHideMidPortal()
{
	// The last boss of the rush: only the victory portal, so the run ends there.
	if ( !bossRushOn() || g_save.rushRunKey != uniqueGameKey || g_save.rushPhase <= 0 ) { return false; }
	return g_save.rushPhase >= (int)splitCommas(g_save.rushSeq).size();
}

void adjustNextLevel(const std::string& prevMap)
{
	adjustNextLevelImpl(prevMap);
}

bool bossRushSkipVictory()
{
	// All Three Bosses: Herx's and the Devil's victory portals (classic mode) lead on instead of ending.
	if ( allBossesRoute() && (!strncmp(map.name, "Boss", 4) || !strncmp(map.name, "Hell Boss", 9)) )
	{
		return true;
	}
	// Not the last boss yet: the victory portal takes you on to the next stop instead of ending.
	if ( !bossRushMoreAfterThis() ) { return false; }
	say(COLOR_AP, "Boss Rush: on to the next one!");
	return true;
}

bool spellbookRead(Item* item, int player)
{
	// Returns true when the spellbook mustn't teach its spell (Spell Sanity: not received yet).
	if ( !g_ready || !item || itemCategory(item) != SPELLBOOK || multiplayer == CLIENT ) { return false; }
	int id = getSpellIDFromSpellbook(item->type);
	const ap_ids::SpellEntry* sp = sanitySpell(id);
	if ( !sp ) { return false; }
	if ( g_slot.spellbook_checks && !sp->dlc )
	{
		int64_t loc = ap_ids::BASE_ID + ap_ids::SPELLBOOK_OFFSET + id;
		if ( !isChecked(loc) )
		{
			sendLocation(loc);
			say(COLOR_AP, "Read the %s spellbook: that's a check!", sp->name);
		}
	}
	if ( g_slot.spell_sanity && spellCopies(id) <= 0 && !knowsSpell(player, id) )
	{
		messagePlayerColor(player, MESSAGE_HINT, COLOR_AP,
			"You can't make sense of it yet. The %s spell has to come from the multiworld.", sp->name);
		return true;
	}
	return false;
}

int spellUpgradeBonus(int spellID)
{
	// Spell Upgrades: each extra copy of a spell is +10% power, like a blessed +1 spellbook.
	if ( !g_ready || !g_slot.spell_sanity || !sanitySpell(spellID) ) { return 0; }
	return 10 * std::max(0, spellCopies(spellID) - 1);
}

bool launcherGate()
{
#ifdef WINDOWS
	// barony.exe opens the launcher (the tracker window) first and waits for it: Connect & Play
	// starts the game connected, Play without Archipelago starts it plain, closing the window quits.
	// The game stays this same process the whole time, so Steam sees it running as usual.
	std::string cmdLine = GetCommandLineA();
	if ( cmdLine.find("-ap-off") != std::string::npos ) { g_apOff = true; return true; }
	char exePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::string dir = exePath;
	size_t slash = dir.find_last_of("\\/");
	dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
	std::string tracker = dir + "\\BaronyAPTracker.exe";
	if ( GetFileAttributesA(tracker.c_str()) == INVALID_FILE_ATTRIBUTES ) { return true; }

	HANDLE evConnect = CreateEventA(nullptr, TRUE, FALSE, "BaronyAPLaunchConnect");
	HANDLE evPlain = CreateEventA(nullptr, TRUE, FALSE, "BaronyAPLaunchPlain");
	if ( !evConnect || !evPlain ) { return true; }
	ResetEvent(evConnect);
	ResetEvent(evPlain);
	STARTUPINFOA si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::string cmd = "\"" + tracker + "\" --launcher";
	std::vector<char> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back('\0');
	if ( !CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi) )
	{
		return true; // no launcher; start as before
	}
	CloseHandle(pi.hThread);
	HANDLE waits[] = { evConnect, evPlain, pi.hProcess };
	DWORD r = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
	bool play = true;
	if ( r == WAIT_OBJECT_0 ) { g_viaLauncher = true; }
	else if ( r == WAIT_OBJECT_0 + 1 ) { g_apOff = true; }
	else
	{
		// The window was closed: quit, unless it picked a choice just as it closed.
		if ( WaitForSingleObject(evConnect, 0) == WAIT_OBJECT_0 ) { g_viaLauncher = true; }
		else if ( WaitForSingleObject(evPlain, 0) == WAIT_OBJECT_0 ) { g_apOff = true; }
		else { play = false; }
	}
	CloseHandle(pi.hProcess);
	CloseHandle(evConnect);
	CloseHandle(evPlain);
	return play;
#else
	return true;
#endif
}

bool holidayThemeAllowed()
{
	// Barony's holiday themes (Halloween, Christmas) replace the music and art; off unless
	// archipelago.cfg has holiday_theme=1.
	static int allowed = -1;
	if ( allowed < 0 )
	{
		allowed = 0;
		std::ifstream in("archipelago.cfg");
		std::string line;
		while ( in && std::getline(in, line) )
		{
			if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
			if ( line == "holiday_theme=1" || line == "holiday_theme=true" ) { allowed = 1; }
		}
	}
	return allowed == 1;
}

bool isConnected()
{
	return g_ready && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated;
}

std::string statusLine()
{
	if ( !g_started ) { return "Archipelago: not connected. Use /ap_connect host:port slot [password]."; }
	if ( !g_ready ) { return "Archipelago: connecting to " + g_server + "..."; }
	char buf[256];
	snprintf(buf, sizeof(buf), "Archipelago: %s as %s | lives %d | kills %ld | queued: %d Minotaur, %d darkness",
		isConnected() ? "connected" : "reconnecting", g_slotName.c_str(),
		g_save.livesLeft, g_save.totalKills, g_save.minotaurQueue, g_save.darknessQueue);
	return buf;
}

} // namespace AP
