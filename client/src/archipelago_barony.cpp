/*-------------------------------------------------------------------------------

	Barony Archipelago client. See archipelago_barony.hpp for the hooks and
	PATCHES.md for where each one is called from.

	Connection:
	  - archipelago.cfg next to barony.exe (server=, slot=, password=) connects on start
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
		int stair_keys = 0;           // old seeds only
		int floor_keys = 0;           // a key on every floor opens its stairs
		int key_compass_time = 30;
		int enemy_randomizer_secret = 0; // Enemy Randomizer also in secret areas (never Minetown)
		int skill_checks = 0;         // 0 off, 1 any class, 2.. a class (Barony class index + 2)
		int skill_check_max_rank = 3; // 1 Basic .. 5 Legendary
		int lock_class = 0;
		int floor_size = 0;           // 0 normal, 1 bigger, 2 much bigger
		int music_mods = 0;           // 0 off, 1 mixed with the original, 2 mods only
		int extra_monsters = 0;       // percent more monsters per floor           // new characters always get the Skill Checks class    // Minotaur floors: seconds before he arrives that the key compass turns on
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
		int trap_countdown_mask = 0;  // bit n: trap n (Minotaur = 0) counts down before going off
		int trap_countdown = 10;      // seconds
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
	bool g_inboxClearRequested = false;
	std::set<int64_t> g_serverChecked;         // locations the server says are checked
	SlotOptions g_slotIncoming;
	bool g_slotIncomingReady = false;
	std::atomic<bool> g_deathLinkIn{ false };

	// --------------------------------------------------------------- game-thread state
	bool g_started = false;                    // AP_Start has been called
	bool g_ready = false;                      // authenticated, slot data + save loaded
	bool g_triedAutoConnect = false;
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

	int g_secretExitThisFloor = 0; // set by generateDungeon, used once in onNewFloor
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
	void launchTracker();
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
			"Marked enemy", "Stair Key", "Extra Life", "Lives:" };
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
			auto eq = line.rfind('=');
			if ( eq == std::string::npos ) { continue; }
			std::string key = line.substr(0, eq);
			if ( key.compare(0, 6, "death:") == 0 )
			{
				g_save.deaths[key.substr(6)] = atoi(line.c_str() + eq + 1);
				continue;
			}
			long long value = atoll(line.c_str() + eq + 1);
			if ( key == "processedItems" ) { g_save.processedItems = (int)value; }
			else if ( key == "totalKills" ) { g_save.totalKills = (long)value; }
			else if ( key == "greenKills" ) { g_save.greenKills = (long)value; }
			else if ( key == "runKey" ) { g_save.runKey = (Uint32)value; }
			else if ( key == "livesLeft" ) { g_save.livesLeft = (int)value; }
			else if ( key == "minotaurQueue" ) { g_save.minotaurQueue = (int)value; }
			else if ( key == "darknessQueue" ) { g_save.darknessQueue = (int)value; }
			else if ( key == "goalBossesKilled" ) { g_save.goalBossesKilled = (int)value; }
			else if ( key == "goalSent" ) { g_save.goalSent = value != 0; }
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
		std::vector<std::string> sources;
		{
			std::lock_guard<std::mutex> lock(g_netMutex);
			sources.swap(g_deathSources);
		}
		for ( auto& src : sources ) { recordDeath(src); }
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

	void disconnect()
	{
		if ( g_started )
		{
			AP_Shutdown();
		}
		g_started = false;
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
		disconnect();
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
		AP_SetDeathLinkRecvCallback([](std::string source, std::string) {
			{
				std::lock_guard<std::mutex> lock(g_netMutex);
				g_deathSources.push_back(source);
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
		registerSlotInt("stair_keys", &SlotOptions::stair_keys);
		registerSlotInt("floor_keys", &SlotOptions::floor_keys);
		registerSlotInt("key_compass_time", &SlotOptions::key_compass_time);
		registerSlotInt("skill_checks", &SlotOptions::skill_checks);
		registerSlotInt("enemy_randomizer_secret", &SlotOptions::enemy_randomizer_secret);
		registerSlotInt("skill_check_max_rank", &SlotOptions::skill_check_max_rank);
		registerSlotInt("lock_class", &SlotOptions::lock_class);
		registerSlotInt("floor_size", &SlotOptions::floor_size);
		registerSlotInt("music_mods", &SlotOptions::music_mods);
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
		registerSlotInt("trap_countdown_mask", &SlotOptions::trap_countdown_mask);
		registerSlotInt("trap_countdown", &SlotOptions::trap_countdown);
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
			g_slot.goal == 0 ? "defeat Baron Herx" : g_slot.goal == 1 ? "defeat the Devil" : "defeat the twin liches");
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
			case ITEM_XP_BOOST: return "XP Boost";
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

	// Traps wait here until they can go off: after their countdown, and only while a player is
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
		int bit = (int)(item - ap_ids::ITEM_TRAP_MINOTAUR);
		bool countdown = bit >= 0 && bit < 31 && (g_slot.trap_countdown_mask & (1 << bit));
		if ( countdown )
		{
			int secs = std::max(1, g_slot.trap_countdown);
			say(COLOR_TRAP, "%s incoming in %d seconds!", trapName(item), secs);
			g_pendingTraps.push_back(PendingTrap{ item, ticks + (Uint32)(secs * TICKS_PER_SECOND), secs <= 3 });
		}
		else
		{
			g_pendingTraps.push_back(PendingTrap{ item, 0, true });
		}
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
		warnSkillClass();
		g_save.runKey = uniqueGameKey;
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
			else if ( g_received[i] != ap_ids::ITEM_STAIR_KEY )
			{
				applyNewItem(g_received[i]);
			}
			else
			{
				say(COLOR_AP, "Received a Progressive Stair Key (%d).", (int)std::count(g_received.begin(), g_received.begin() + i + 1, g_received[i]));
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
				if ( !floor && !shop ) { continue; }
				if ( itemIsEquipped(item, p) ) { continue; }

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
	bool findReachableSpot(int& outX, int& outY)
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
				if ( !walkable(nx, ny) ) { continue; }
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
	void updateKeyCompass(int minotaurSecs)
	{
		g_keyCompass.clear();
		// Key Compass Time: how many seconds before he arrives it turns on (at least 30), but never in
		// the first 30 seconds of his timer.
		// On a bigger floor the seconds stretch with it, so 30 feels like 30 on a normal floor.
		int scale = currentFloorScale();
		int when = std::max(30, g_slot.key_compass_time) * scale / 100;
		int least = 30 * scale / 100;
		bool on = minotaurSecs == -2 || (minotaurSecs >= 0 && minotaurSecs <= when && g_minotaurElapsed >= 30);
		if ( minotaurSecs >= 0 && minotaurSecs <= least ) { on = true; }
		if ( !g_floorNeedsKey || g_floorKeyFound || !on ) { return; }
		Entity* pl = players[clientnum] ? players[clientnum]->entity : nullptr;
		if ( !pl || !map.entities ) { return; }
		for ( node_t* node = map.entities->first; node; node = node->next )
		{
			Entity* e = (Entity*)node->element;
			if ( !isFloorKeyEntity(e) ) { continue; }
			double dx = (e->x - pl->x) / 16.0, dy = (e->y - pl->y) / 16.0; // tiles; +y is south on the map
			int tiles = (int)std::lround(std::sqrt(dx * dx + dy * dy));
			if ( tiles <= 1 ) { g_keyCompass = "right here"; return; }
			static const char* const DIRS[] = { "E", "SE", "S", "SW", "W", "NW", "N", "NE" };
			double ang = std::atan2(dy, dx); // 0 = east, clockwise because +y is south
			int octant = ((int)std::lround(ang / (PI / 4)) + 8) % 8;
			char buf[48];
			snprintf(buf, sizeof(buf), "%d tiles %s", tiles, DIRS[octant]);
			g_keyCompass = buf;
			return;
		}
	}

	// Picked up? Then the stairs open. Lost (a monster took it, it fell somewhere)? Put out a new one.
	void updateFloorKey()
	{
		if ( !g_floorNeedsKey || g_floorKeyFound ) { return; }
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
		spawnFloorKey(); // gone from the floor without being found
	}

	void spawnFloorPickups()
	{
		if ( g_slot.archipelago_pickups <= 0 || currentlevel < 1 ) { return; } // none on the starting floor
		// Every dungeon floor gets them, generated or hand-made; not boss arenas or the town.
		static const char* const SKIP[] = { "Boss", "Hell Boss", "Sanctum", "Hamlet", "Tutorial" };
		for ( const char* name : SKIP )
		{
			if ( !strncmp(map.name, name, strlen(name)) ) { return; }
		}
		int toPlace = std::min(g_slot.pickups_per_floor,
			uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups) - apItemsLyingAround());
		if ( g_floorIsRestart ) { toPlace = std::min(toPlace, g_restartApBudget - g_attemptApMade); }
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
		int extra = std::max(1, existing) * g_slot.extra_monsters / 100;
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
			g_floorApTotal = g_attemptApMade; // chests already made theirs during generation
			g_floorApFound = 0;
		}
		spawnExtraMonsters(); // before the kill removal, so extra monsters you killed stay dead too
		if ( restarted ) { removeKilledEnemies(); }
		else { g_floorKills = 0; }
		if ( restarted && g_slot.death_items == 1 ) { removeTakenLoot(); }
		else if ( !restarted ) { g_floorItemsTaken = 0; g_floorChestsLooted = 0; }
		rememberFloorLoot();
		g_markedEnemies.clear();
		g_markedThisFloor = 0;
		g_stairsWarnedAt = 0;
		g_floorStartTicks = ticks;
		sendFloorChecks();
		sendSpecialAreaCheck();
		spawnFloorPickups();
		giveSecretAreaTools();
		if ( !restarted ) { g_floorKeyFound = false; }
		g_floorNeedsKey = floorUsesKey();
		if ( g_floorNeedsKey && !g_floorKeyFound )
		{
			spawnFloorKey();
			if ( !g_floorKeyFound ) { say(COLOR_AP, "Find this floor's Floor Key to open the stairs down."); }
		}
		pickMarkedEnemies();
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
			applyDeathPenalty(stats[p]);
			say(COLOR_TRAP, "Death Link! Someone else died... you lose a life (%d left).", g_save.livesLeft);
			printlog("[Archipelago] Death Link: lost a life. Lives left: %d", g_save.livesLeft);
			writeSave();
			return;
		}
		say(COLOR_TRAP, "Death Link! Someone else died...");
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
			if ( it && it->type == AP_ITEM_TYPE && (it->appearance == AP_FLOOR_APPEARANCE || it->appearance == AP_SHOP_APPEARANCE) )
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

	FloorCounts g_floorCounts;
	int g_logPanel = 0; // 0 hidden, 1 items only (F1), 2 everything (F2), 3 death counter (F3)

	// The tracker window (BaronyAPTracker.exe) reads this file.
	void writeTrackerFile()
	{
		// Built in memory and written only when something changed, so the disk is left alone.
		static std::string lastWritten;
		std::ostringstream out;
		out << "connected=" << (AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated ? 1 : 0) << "\n";
		out << "slot=" << g_slotName << "\n";
		out << "ingame=" << (inGame() ? 1 : 0) << "\n";
		out << "floor=" << currentlevel << "\n";
		out << "ground=" << g_floorCounts.ground << "\nchests=" << g_floorCounts.chests << "\nshop=" << g_floorCounts.shop
			<< "\nmarked=" << g_floorCounts.marked << "\n";
		out << "lives=" << g_save.livesLeft << "\nkeys=" << countReceived(ap_ids::ITEM_STAIR_KEY) << "\n";
		out << "pickups_left=" << uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups) << "\n";
		out << "deaths_total=" << totalDeaths() << "\n";
		out << "minotaur=" << g_minotaurSecs << "\n";
		out << "floorkey=" << (!g_floorNeedsKey ? 2 : g_floorKeyFound ? 1 : 0) << "\n";
		out << "keycompass=" << g_keyCompass << "\n";
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
		for ( auto& d : g_save.deaths ) { out << "death=" << d.first << "\t" << d.second << "\n"; }
		size_t start = g_log.size() > 40 ? g_log.size() - 40 : 0;
		for ( size_t i = start; i < g_log.size(); ++i )
		{
			out << (g_log[i].item ? "item=" : "log=") << g_log[i].text << "\n";
		}
		std::string text = out.str();
		if ( text == lastWritten ) { return; }
		std::ofstream file("archipelago/tracker.txt", std::ios::trunc | std::ios::binary);
		if ( !file ) { return; }
		file << text;
		lastWritten = text;
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
	if ( !g_triedAutoConnect ) { autoConnectFromConfig(); }
	if ( !g_started ) { return; }
	checkReady();
	if ( !g_ready ) { return; }

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
		return; // APCpp reconnects by itself; items wait in the inbox
	}

	showServerMessages();
	if ( keystatus[SDLK_F1] ) { keystatus[SDLK_F1] = 0; g_logPanel = g_logPanel == 1 ? 0 : 1; }
	if ( keystatus[SDLK_F2] ) { keystatus[SDLK_F2] = 0; g_logPanel = g_logPanel == 2 ? 0 : 2; }
	if ( keystatus[SDLK_F3] ) { keystatus[SDLK_F3] = 0; g_logPanel = g_logPanel == 3 ? 0 : 3; }
	if ( keystatus[SDLK_F4] ) { keystatus[SDLK_F4] = 0; ++g_lootToggles; } // tracker: full or short loot list
	pullDeathSources();
	if ( ticks % TICKS_PER_SECOND == 0 ) { writeTrackerFile(); }
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
	syncPermanents();
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
		updateFloorKey();
		updateKeyCompass(g_minotaurSecs);
	}
}

int remapLevelLine(int levelToLoad, const std::vector<std::string>& levelsList)
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

	// Shopkeepers stock an Archipelago item while shop checks remain.
	if ( myStats->type == SHOPKEEPER && g_slot.shop_checks > 0
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
	else { goalMask = 3; if ( type == LICH_ICE ) { goalBit = 1; } else if ( type == LICH_FIRE ) { goalBit = 2; } }

	if ( goalBit )
	{
		g_save.goalBossesKilled |= goalBit;
		if ( (g_save.goalBossesKilled & goalMask) == goalMask && !g_save.goalSent )
		{
			g_save.goalSent = true;
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
	if ( g_save.livesLeft <= 0 ) { return false; }

	--g_save.livesLeft;
	bool fromDeathLink = g_dyingFromDeathLink;
	g_dyingFromDeathLink = false;

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
	if ( !fromDeathLink )
	{
		AP_DeathLinkSend("died in the dungeon");
		recordDeath(g_slotName);
	}
}

bool canDescend(int player)
{
	if ( !g_ready || multiplayer == CLIENT ) { return true; }
	if ( g_floorNeedsKey && !g_floorKeyFound )
	{
		messagePlayerColor(player, MESSAGE_HINT, COLOR_AP, "The stairs are locked. Find this floor's Floor Key first.");
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
	if ( !g_slot.stair_keys ) { return true; }
	int nextFloor = currentlevel + 1;
	int section = (nextFloor - 1) / ap_ids::FLOORS_PER_SECTION + 1;
	int needed = section - 1;
	int have = countReceived(ap_ids::ITEM_STAIR_KEY);
	if ( have >= needed ) { return true; }
	messagePlayerColor(player, MESSAGE_HINT, COLOR_AP,
		"The stairs are sealed. You need %d Progressive Stair Key%s to go deeper (you have %d).",
		needed, needed == 1 ? "" : "s", have);
	return false;
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
	constexpr const char* AP_MODEL_PATH = "models/items/AP_Item.vox";
}

bool isMarkedEnemy(const Entity* entity)
{
	if ( !entity || g_markedEnemies.empty() ) { return false; }
	if ( g_markedEnemies.count(entity->getUID()) ) { return true; }
	return g_markedEnemies.count(entity->parent) > 0; // body parts belong to their monster
}

bool isApItem(const Item* item)
{
	return item && item->type == AP_ITEM_TYPE
		&& (item->appearance == AP_FLOOR_APPEARANCE || item->appearance == AP_SHOP_APPEARANCE);
}

int apItemModel() { return g_apModelIndex; }
const char* apItemIconPath() { return "items/images/AP_Item.png"; }
const char* apItemName(bool withArticle) { return withArticle ? "an Archipelago item" : "Archipelago item"; }

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
	switch ( g_slot.floor_size ) { case 1: return 120; case 2: return 140; default: return 100; }
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
	if ( !PHYSFS_getRealDir(AP_MODEL_PATH) )
	{
		printlog("[Archipelago] %s not found; the Archipelago item will look like a luckstone.", AP_MODEL_PATH);
		return;
	}
	char path[128];
	snprintf(path, sizeof(path), "%s", AP_MODEL_PATH);
	voxel_t* model = loadVoxel(path);
	if ( !model ) { return; }
	voxel_t** grown = (voxel_t**)realloc(models, sizeof(voxel_t*) * (nummodels + 1));
	polymodel_t* grownPoly = (polymodel_t*)realloc(polymodels, sizeof(polymodel_t) * (nummodels + 1));
	if ( grown ) { models = grown; }
	if ( grownPoly ) { polymodels = grownPoly; }
	if ( !grown || !grownPoly ) { return; }
	int index = (int)nummodels;
	models[index] = model;
	memset(&polymodels[index], 0, sizeof(polymodel_t));
	nummodels += 1;
	generatePolyModels(index, index + 1, false);
	g_apModelIndex = index;
	printlog("[Archipelago] Archipelago item model loaded as model %d.", index);
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
	if ( !chest || chest->behavior != &actChest || !chest->children.first || !apItemAvailable() ) { return; }
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
	char nameBuf[512] = "";
	if ( sound->getName(nameBuf, sizeof(nameBuf)) == FMOD_OK )
	{
		std::string name = std::filesystem::path(nameBuf).filename().string();
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
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
		if ( g_floorNeedsKey && !g_floorKeyFound )
		{
			std::string keyLine = g_keyCompass.empty() ? std::string("Find the Floor Key") : "Floor Key: " + g_keyCompass;
			drawText(keyLine.c_str(), g_keyCompass.empty() ? HUD_FONT : BIG_FONT, makeColorRGB(255, 200, 90), 38, row);
			row += g_keyCompass.empty() ? 18 : 22;
		}
		// Key hint: only for the first moments of a floor, so the corner stays clean.
		if ( g_logPanel == 0 && ticks - g_floorStartTicks < (Uint32)(TICKS_PER_SECOND * 12) )
		{
			drawText("F1: items   F2: log   F3: deaths   F4: tracker loot list", HUD_FONT, makeColorRGB(140, 140, 140), 38, row);
		}
	}
	if ( g_logPanel == 3 )
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

	// Top right: lives.
	if ( g_ready && inGame() && (g_slot.lives > 0 || g_save.livesLeft > 0) )
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "x %d", g_save.livesLeft);
		const int right = Frame::virtualScreenX - 12;
		int w = drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), right, 10, 2);
		drawImage("*images/ui/Archipelago/AP_Life.png", right - w - 32, 6, 26, 24);
	}
	if ( g_ready && inGame() )
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "x %d", totalDeaths());
		const int right = Frame::virtualScreenX - 12;
		const int top = (g_slot.lives > 0 || g_save.livesLeft > 0) ? 38 : 10;
		int w = drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), right, top, 2);
		drawImage("*images/ui/Archipelago/AP_Skull.png", right - w - 32, top - 3, 24, 24);
	}

	if ( g_deathScreen && inGame() )
	{
		drawDeathScreen();
	}
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
	snprintf(buf, sizeof(buf), "Archipelago: %s as %s | stair keys %d | lives %d | kills %ld | queued: %d Minotaur, %d darkness",
		isConnected() ? "connected" : "reconnecting", g_slotName.c_str(), countReceived(ap_ids::ITEM_STAIR_KEY),
		g_save.livesLeft, g_save.totalKills, g_save.minotaurQueue, g_save.darknessQueue);
	return buf;
}

} // namespace AP
