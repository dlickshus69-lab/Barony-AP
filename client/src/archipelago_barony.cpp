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
		int bestiary_checks = 0;      // 0 off, 1 early, 2 all
		int archipelago_pickups = 0;
		int pickups_per_floor = 2;
		int shop_checks = 0;
		int boss_checks = 0;
		int special_area_checks = 0;
		int stair_keys = 0;
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
		int players = 1;
	};

	// --------------------------------------------------------------- saved progress
	struct SaveData
	{
		int processedItems = 0;       // received items already used (one-shot items)
		long totalKills = 0;
		Uint32 runKey = 0;            // uniqueGameKey of the run permanent items were applied to
		int livesLeft = 0;
		int minotaurQueue = 0;
		int darknessQueue = 0;
		int goalBossesKilled = 0;     // bit per goal boss
		bool goalSent = false;
		int permApplied[8] = {};      // permanent items already given to the current run's character
	};

	const int64_t PERMANENT_ITEMS[8] = {
		ap_ids::ITEM_PERM_STR, ap_ids::ITEM_PERM_DEX, ap_ids::ITEM_PERM_CON, ap_ids::ITEM_PERM_INT,
		ap_ids::ITEM_PERM_PER, ap_ids::ITEM_PERM_CHR, ap_ids::ITEM_MAX_HP_UP, ap_ids::ITEM_START_GOLD,
	};

	// --------------------------------------------------------------- shared with the network thread
	std::mutex g_netMutex;
	std::vector<int64_t> g_inbox;              // received items not yet handled
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
	Uint32 g_floorLoads = 0;                    // counts floor generations, for re-rolled enemies

	// Death screen: a life was lost, waiting for the player to press confirm.
	bool g_deathScreen = false;
	int g_deathScreenPlayer = 0;
	Uint32 g_deathScreenStart = 0;
	constexpr Uint32 DEATH_SCREEN_MIN_TICKS = TICKS_PER_SECOND * 2;
	Uint32 g_lastPlayerUid = 0;
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
	void say(Uint32 color, const char* fmt, ...)
	{
		char buf[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		messagePlayerColor(clientnum, MESSAGE_HINT, color, "%s", buf);
		printlog("[Archipelago] %s", buf);
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
		out << "runKey=" << g_save.runKey << "\n";
		out << "livesLeft=" << g_save.livesLeft << "\n";
		out << "minotaurQueue=" << g_save.minotaurQueue << "\n";
		out << "darknessQueue=" << g_save.darknessQueue << "\n";
		out << "goalBossesKilled=" << g_save.goalBossesKilled << "\n";
		out << "goalSent=" << (g_save.goalSent ? 1 : 0) << "\n";
		for ( int i = 0; i < 8; ++i ) { out << "perm" << i << "=" << g_save.permApplied[i] << "\n"; }
	}

	void readSave()
	{
		g_save = SaveData();
		std::ifstream in(g_savePath);
		std::string line;
		while ( std::getline(in, line) )
		{
			auto eq = line.find('=');
			if ( eq == std::string::npos ) { continue; }
			std::string key = line.substr(0, eq);
			long long value = atoll(line.c_str() + eq + 1);
			if ( key == "processedItems" ) { g_save.processedItems = (int)value; }
			else if ( key == "totalKills" ) { g_save.totalKills = (long)value; }
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
		AP_SetDeathLinkRecvCallback([]() { g_deathLinkIn = true; });

		registerSlotInt("goal", &SlotOptions::goal);
		registerSlotInt("goal_floor", &SlotOptions::goal_floor);
		registerSlotInt("floor_check_interval", &SlotOptions::floor_check_interval);
		registerSlotInt("level_checks", &SlotOptions::level_checks);
		registerSlotInt("kill_checks", &SlotOptions::kill_checks);
		registerSlotInt("kill_check_interval", &SlotOptions::kill_check_interval);
		registerSlotInt("bestiary_checks", &SlotOptions::bestiary_checks);
		registerSlotInt("archipelago_pickups", &SlotOptions::archipelago_pickups);
		registerSlotInt("pickups_per_floor", &SlotOptions::pickups_per_floor);
		registerSlotInt("shop_checks", &SlotOptions::shop_checks);
		registerSlotInt("boss_checks", &SlotOptions::boss_checks);
		registerSlotInt("special_area_checks", &SlotOptions::special_area_checks);
		registerSlotInt("stair_keys", &SlotOptions::stair_keys);
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
		g_ready = true;
		g_lastCharLevelSent = 1;
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

	void applyTrap(int64_t item)
	{
		using namespace ap_ids;
		if ( item == ITEM_TRAP_MINOTAUR ) { triggerMinotaur(); return; }
		if ( item == ITEM_TRAP_DARKNESS )
		{
			++g_save.darknessQueue;
			say(COLOR_TRAP, "Darkness Trap! The next floor will be dark.");
			return;
		}

		int p = randomActivePlayer();
		if ( p < 0 ) { return; }
		Entity* ent = players[p]->entity;
		Stat* s = stats[p];

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
				spellEffectPolymorph(ent, ent, true, TICKS_PER_SECOND * 30);
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
				ent->setEffect(EFF_BLIND, true, TICKS_PER_SECOND * 15, true);
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

	void sendKillChecks()
	{
		int interval = std::max(1, g_slot.kill_check_interval);
		for ( int k = 1; k <= g_slot.kill_checks; ++k )
		{
			if ( g_save.totalKills >= (long)k * interval )
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

	void spawnFloorPickups()
	{
		if ( g_slot.archipelago_pickups <= 0 || !currentFloorIsNormalGenerated() ) { return; }
		int toPlace = std::min(g_slot.pickups_per_floor, uncheckedCount(ap_ids::PICKUP_OFFSET, g_slot.archipelago_pickups));
		for ( int i = 0; i < toPlace; ++i )
		{
			int x, y;
			if ( !findFloorSpot(x, y) ) { break; }
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
		}
		if ( toPlace > 0 )
		{
			say(COLOR_AP, "%d Archipelago item%s on this floor.", toPlace, toPlace == 1 ? " lies" : "s lie");
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
		}
	}

	// Put back exactly what the player carried when the floor began: used-up potions and
	// broken or dropped gear come back, and anything found on this floor is gone.
	void restoreInventory(int p)
	{
		if ( !g_hasFloorSnapshot[p] || !stats[p] ) { return; }
		Stat* s = stats[p];
		for ( int slot = 0; slot < NUM_EQUIP_SLOTS; ++slot ) { s->*EQUIP_SLOTS[slot] = nullptr; }
		if ( auto ui = inputs.getUIInteraction(p) )
		{
			ui->selectedItem = nullptr;
			ui->selectedItemFromChest = 0;
		}
		list_FreeAll(&s->inventory);
		for ( const ItemSnapshot& snap : g_floorStartInventory[p] )
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

	void restartFloor()
	{
		g_deathScreen = false;
		int player = g_deathScreenPlayer;
		Stat* s = stats[player];
		if ( !s ) { return; }

		// Back to the inventory from the start of the floor.
		restoreInventory(player);

		// Death penalty. Never costs a level.
		if ( g_slot.death_penalty >= 1 ) { s->GOLD -= s->GOLD / 4; }
		if ( g_slot.death_penalty >= 2 ) { s->EXP /= 2; }

		for ( int c = 0; c < NUMEFFECTS; ++c )
		{
			s->clearEffect(c);
			s->EFFECTS_TIMERS[c] = 0;
		}
		s->HP = s->MAXHP;
		s->MP = s->MAXMP;
		s->HUNGER = std::max(s->HUNGER, 800);

		// Reload this floor: a new layout, or the same one again.
		g_restartPending = true;
		skipLevelsOnLoad = -1;
		forceMapSeed = (g_slot.floor_restart == 1 && mapseed > 0) ? mapseed : ((local_rng.rand() & 0x7FFFFFFF) | 1);
		loadnextlevel = true;
		say(COLOR_TRAP, "Life lost. Lives left: %d", g_save.livesLeft);
		writeSave();
	}

	void onNewFloor()
	{
		sendFloorChecks();
		sendSpecialAreaCheck();
		spawnFloorPickups();
		g_restartPending = false;
		snapshotInventories();
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
		if ( !inGame() || stats[p]->HP <= 0 ) { return; }
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
			}
			AP_ClearLatestMessage();
			++shown;
		}
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
	if ( g_deathScreen && inGame() && ticks - g_deathScreenStart >= DEATH_SCREEN_MIN_TICKS
		&& Input::inputs[clientnum].consumeBinaryToggle("MenuConfirm") )
	{
		restartFloor();
	}

	if ( AP_GetConnectionStatus() == AP_ConnectionStatus::Disconnected )
	{
		return; // APCpp reconnects by itself; items wait in the inbox
	}

	showServerMessages();
	if ( !inGame() ) { return; }

	pullInbox();
	if ( g_save.runKey != uniqueGameKey && stats[clientnum]->HP > 0 )
	{
		initRun();
	}
	applyNewItems();
	syncPermanents();
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
		scanInventories();
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
		newItem(AP_ITEM_TYPE, EXCELLENT, 0, 1, AP_SHOP_APPEARANCE, true, &myStats->inventory);
	}

	bool isBoss = myStats->type == LICH || myStats->type == DEVIL || myStats->type == LICH_ICE
		|| myStats->type == LICH_FIRE;

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
	sendKillChecks();

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
	}

	// Freeze the player and show the death screen; restartFloor() runs on confirm.
	s->HP = 1;
	if ( Entity* ent = players[player]->entity )
	{
		ent->setEffect(EFF_PARALYZED, true, TICKS_PER_SECOND * 3600, false);
	}
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
	}
}

bool canDescend(int player)
{
	if ( !g_ready || !g_slot.stair_keys || multiplayer == CLIENT ) { return true; }
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

int adjustMinotaurTime(int ticks)
{
	if ( g_ready && g_slot.difficulty == 1 )
	{
		return ticks * 2 / 3;
	}
	return ticks;
}

// ---------------------------------------------------------------- Archipelago item look
namespace
{
	int g_apModelIndex = -1;
	constexpr const char* AP_MODEL_PATH = "models/items/AP_Item.vox";
}

bool isApItem(const Item* item)
{
	return item && item->type == AP_ITEM_TYPE
		&& (item->appearance == AP_FLOOR_APPEARANCE || item->appearance == AP_SHOP_APPEARANCE);
}

int apItemModel() { return g_apModelIndex; }
const char* apItemIconPath() { return "items/images/AP_Item.png"; }
const char* apItemName(bool withArticle) { return withArticle ? "an Archipelago item" : "Archipelago item"; }

void loadExtraModels()
{
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

int randomizeMonster(int monsterType)
{
	if ( !g_ready || g_slot.enemy_randomizer == 0 || multiplayer == CLIENT ) { return monsterType; }
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
	Uint32 key = (Uint32)g_slot.floor_shuffle_seed ^ (Uint32)(currentlevel * 2246822519u);
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
	const char* label = connected ? "Archipelago: Connected" : connecting ? "Archipelago: Connecting..." : "Archipelago: Disconnected";
	Uint32 color = connected ? makeColorRGB(120, 230, 120) : connecting ? makeColorRGB(240, 210, 90) : makeColorRGB(240, 90, 80);
	drawImage(connected ? "*images/ui/HUD/Ping_Green.png" : connecting ? "*images/ui/HUD/Ping_Yellow.png" : "*images/ui/HUD/Ping_Red.png",
		10, 10, 18, 18);
	drawText(label, HUD_FONT, color, 34, 10);

	// Top right: lives.
	if ( g_ready && inGame() && (g_slot.lives > 0 || g_save.livesLeft > 0) )
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "x %d", g_save.livesLeft);
		const int right = Frame::virtualScreenX - 12;
		int w = drawText(buf, BIG_FONT, makeColorRGB(255, 255, 255), right, 10, 2);
		drawImage("*images/ui/Archipelago/AP_Life.png", right - w - 32, 6, 26, 24);
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
	snprintf(buf, sizeof(buf), "Archipelago: %s as %s | keys %d | lives %d | kills %ld | queued: %d Minotaur, %d darkness",
		isConnected() ? "connected" : "reconnecting", g_slotName.c_str(), countReceived(ap_ids::ITEM_STAIR_KEY),
		g_save.livesLeft, g_save.totalKills, g_save.minotaurQueue, g_save.darknessQueue);
	return buf;
}

} // namespace AP
