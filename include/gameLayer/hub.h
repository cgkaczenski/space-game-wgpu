#pragma once

// The hub, between missions (inventory roadmap I4): where an extraction ends,
// and where the pause menu's QUIT TO HUB goes. The game launches straight
// into a level as before; dying restarts in place as before.
//
//   Tally     what the run that just ended banked -- or that it banked
//             nothing, left from the menu.
//   Stash     every weapon owned, as tiles that scroll with the wheel.
//   Loadout   the four slots and the 5 x 4 hold: drag from the stash what to
//             take -- spares in the hold too, at the cost of room for ore.
//             What is taken can be lost; what stays is safe.
//   Shop      a rolled selection, like crate contents, restocked after each
//             mission and priced by kind and modifiers. Click to buy, into
//             the stash. Drag a weapon onto the shop to sell it for half.
//   Levels    which level LAUNCH starts.
//
// Behind it, the starfield drifting. The game's own clock does not run.

#include <render/wgpu2d.h>
#include <string>
#include <vector>

namespace hub
{
	struct Tally
	{
		bool extracted = false;   // false: left from the menu, nothing banked
		int ore = 0;              // banked as points
		int weapons = 0;          // banked into the stash, or lost
	};

	// Opens it. `restock` rolls the shop anew (after a mission).
	void open(const Tally &tally, const std::vector<std::string> &levels, const std::string &current, bool restock);
	bool isOpen();
	void close();

	enum class Action { None, Launch, Quit };

	// Once a frame while open, after the starfield: draws the hub in screen
	// space and reads the mouse. On Launch, `level` is the file chosen.
	Action update(wgpu2d::Renderer2D &renderer, int width, int height, std::string &level);

	void debugUi();
}
