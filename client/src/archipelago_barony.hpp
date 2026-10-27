/*-------------------------------------------------------------------------------

	Barony Archipelago client.

	All of the Archipelago logic lives in archipelago_barony.cpp. The rest of Barony
	only calls the small hooks below, each from one place (see PATCHES.md for where).

	Everything here runs on the host (multiplayer != CLIENT). Network callbacks from
	APCpp arrive on its own thread and are queued; the game applies them in AP::tick().

-------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <cstdint>
#include <vector>

class Entity;
class Stat;
class Item;

namespace AP
{
	// game.cpp, top of gameLogic(): once per game tick.
	void tick();

	// files.cpp, physfsLoadMapFile(): which line of levels.txt to load for this floor.
	// Returns levelToLoad unchanged unless floor shuffle is on.
	int remapLevelLine(int levelToLoad, const std::vector<std::string>& levelsList);

	// maps.cpp, generateDungeon(): after the Minotaur and dark-floor decisions.
	void onDungeonGenerated();

	// actmonster.cpp: right after a monster's stats are initialized.
	void onMonsterInit(Entity* my, Stat* myStats);

	// actmonster.cpp: when a monster's HP first reaches 0.
	void onMonsterDeath(Entity* my, Stat* myStats);

	// actplayer.cpp: a player's HP is 0. Returns true if a life was spent and the
	// floor is restarting, in which case the normal death must be skipped.
	bool tryConsumeLife(int player);

	// actplayer.cpp: a player really died (no life left). Sends Death Link.
	void onPlayerDied(int player);

	// actladder.cpp: may the party take the stairs down to the next floor?
	// Shows a message and returns false when a Stair Key is missing.
	bool canDescend(int player);

	// game.cpp, when the next floor's map seed is picked: Layout Seed (same map every time).
	uint32_t lockedMapSeed(uint32_t seed);

	// maps.cpp, generateDungeon(): this floor has a secret-area entrance (0 = none). The player
	// gets a pickaxe and a levitation potion if they have nothing to dig or float with.
	void noteSecretExit(int secretLevelExit);

	// monster_minotaur.cpp, getMinotaurTimeToArrive(): hard mode brings him sooner.
	int adjustMinotaurTime(int ticks);

	// game.cpp, after the HUD and main menu are drawn: connection status, lives, death screen.
	void drawHud();

	// opengl.cpp: Enemy Sanity marked enemies are drawn with a green glow.
	bool isBountyEnemy(const Entity* entity); // Bounty Enemies glow gold
	bool isMarkedEnemy(const Entity* entity);

	// The Archipelago item (floor pickups and shop items) and its own logo model and icon.
	bool isApItem(const Item* item);
	bool isFloorKey(const Item* item);
	bool isCompassItem(const Item* item); // the Compass (a marked mirror)
	// items.cpp, useItem(): using the Compass. Returns true when it handled the item.
	bool useCompassItem(Item* item, int player);
	// charclass.cpp, initClass(): Lock Class (the class chosen for Skill Checks).
	int lockedClass(int player, int currentClass);
	// maps.cpp / files.cpp: Floor Size. requestFloorSize() before a generated floor's base map loads;
	// loadMap takes the percent once; scaleFloorEntities() raises the monster/item/decoration count.
	void requestFloorSize();
	int takeFloorSizePercent();
	int scaleFloorEntities(int count);
	int currentFloorScale();           // 100 normal, 120/140 on a bigger generated floor  // this floor's Floor Key (a marked skeleton key)
	int apItemModel();                 // model index, or -1 if the model didn't load
	int floorKeyModel();               // the Floor Key's own model, or -1
	const char* apItemIconPath();
	const char* apItemName(bool withArticle);
	// init.cpp, right after the game's models load: adds the Archipelago model.
	void loadExtraModels();
	// ui/MainMenu.cpp: the title image (Barony's logo with the Archipelago logo for the O).
	const char* titleImagePath();

	// actchest.cpp / actgeneral.cpp: Archipelago items in chests and breakable containers.
	void onChestCreated(Entity* chest);
	void onBreakableDestroyed(Entity* breakable);

	// maps.cpp, monsterCurve(): Enemy Randomizer. Returns the monster type to spawn instead.
	int randomizeMonster(int monsterType);
	// maps.cpp, map monsters: the same for hand-made floors (named characters and story floors excluded).
	int randomizePlacedMonster(int monsterType, bool named);

	// sound_game.cpp, handleLevelMusic(): Music Shuffle. While one of these exists, the
	// floor's name is swapped for the biome whose music should play.
	struct MusicOverride
	{
		MusicOverride();
		~MusicOverride();
	private:
		char saved[32];
		bool active = false;
	};

	// engine/audio/music.cpp, playMusic(): Music Mods. Takes and returns an FMOD::Sound*: the track to
	// play, which may be a mod's version of the same track.
	void* pickMusicVersion(void* sound);
	const char* bossRushNextMap();  // Boss Rush: the map to load in place of this floor, or nullptr
	bool bossRushSkipVictory();
	bool bossRushHideMidPortal();   // Boss Rush: the last boss's arena only has the victory portal     // Boss Rush: true = this victory portal goes on to the next boss
	bool spellbookRead(Item* item, int player); // Spell Sanity / Spellbook Checks; true = don't teach
	int spellUpgradeBonus(int spellID);         // Spell Upgrades: extra spell power in percent
	bool launcherGate();        // start of main(): the launcher window first; false = quit
	bool holidayThemeAllowed(); // Barony's Halloween/Christmas themes, off unless archipelago.cfg says holiday_theme=1

	// Status for the console and the UI.
	bool isConnected();
	std::string statusLine();
}
