/*-------------------------------------------------------------------------------

	Barony Archipelago client.

	All of the Archipelago logic lives in archipelago_barony.cpp. The rest of Barony
	only calls the small hooks below, each from one place (see PATCHES.md for where).

	Everything here runs on the host (multiplayer != CLIENT). Network callbacks from
	APCpp arrive on its own thread and are queued; the game applies them in AP::tick().

-------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>

class Entity;
class Stat;

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

	// monster_minotaur.cpp, getMinotaurTimeToArrive(): hard mode brings him sooner.
	int adjustMinotaurTime(int ticks);

	// game.cpp, after the HUD and main menu are drawn: connection status, lives, death screen.
	void drawHud();

	// Status for the console and the UI.
	bool isConnected();
	std::string statusLine();
}
