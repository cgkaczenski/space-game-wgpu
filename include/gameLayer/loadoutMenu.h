#pragma once

// The live loadout menu (inventory roadmap I2): the four weapon slots and
// the hold's 5 x 4 grid, opened with the Loadout action (I) in the middle of
// a round, and the round does not stop for it.
//
// While it is open the ship's controls are off and it coasts; the world runs
// on, and a hit lands as it would. The world is greyed a little so it reads
// as "not flying"; the HUD stays.
//
//   Drag        press on a weapon or a stack of ore and move it: its icon
//               follows the pointer, and over the hold its shape shows where
//               it would go, green where it fits, red where not. It is held
//               by its middle. The HUD's weapon row works as the panel's
//               slots do while the menu is open: drag from it, drop on it.
//   Turn        Menu rotate (R, or the right button) while dragging; the line
//               under the grid says so. About to be dropped, "LET GO TO DROP"
//               goes along under the item.
//   Let go      on the hold: moved there. On a slot: equipped, and what was
//               there goes to the hold where the dragged one was, or wherever
//               it fits -- refused if nowhere. Slot to slot: swapped. A weapon
//               put in a slot starts on its cooldown.
//   Hotbar      with the menu closed, the HUD's weapon row can still be
//               rearranged: drag one slot onto another and they swap. Let go
//               anywhere else and it goes back; nothing is dropped. A press
//               that starts on a slot does not fire.
//   Drop        let go outside the panel: thrown out behind the ship. Ore
//               goes as orbs, which wait until the ship has left them once;
//               a weapon is gone, until salvage (I3) can leave it in space.
//
// Escape or the Loadout key closes it. It closes by itself when the round
// stops being played: dying, leaving, a restart.

#include <render/wgpu2d.h>

namespace loadoutMenu
{
	struct Frame
	{
		int width = 0;            // framebuffer pixels
		int height = 0;
		bool live = false;        // the round is being played: not paused, dying or leaving
		bool paused = false;
		glm::vec2 ship = {};      // where a jettison is thrown from
		glm::vec2 facing = {1.f, 0.f};
	};

	// Once a frame, before the ship's controls are read: opens and closes it,
	// and while it is open, drags and drops. `escape` is this frame's Escape:
	// true is returned in `escapeTaken` when it closed the menu, so it does
	// not also pause the game.
	void update(const Frame &frame, bool escape, bool &escapeTaken);

	bool isOpen();
	void close();

	// The mouse is the menu's: it is open, or a weapon is being dragged along
	// the HUD's row. The trigger waits.
	bool claimsMouse();

	// 0 .. 1, eased: how far the world is into the menu's grey.
	float look();

	// Screen space, after the HUD.
	void draw(wgpu2d::Renderer2D &renderer, int width, int height);

	void debugUi();
}
