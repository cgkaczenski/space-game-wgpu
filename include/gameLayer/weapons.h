#pragma once

// The player's weapons: four slots, each with its own cooldown, and one fire
// function that turns a trigger into bullets (gameplay roadmap C2).
//
// A weapon is data -- cooldown, damage, size, speed, burst, ammo -- not a
// class. Every shot is an ordinary Bullet carrying its damage, size and style,
// so the collision loop and the drawing need no idea which weapon fired it.
//
//   1  Burst laser   two shots in quick succession, then the cooldown
//   2  Heavy laser   one larger shot, three times a burst shot's damage
//   3  Missile       5 rounds. Flies straight for now; homing is C3
//   4  Laser         a held beam. Selectable, but the beam itself is C3
//
// Cooldowns tick for every slot at once, so switching does not reset them.
// Selecting is by keys 1-4 or the wheel (wrapping); an empty weapon can be
// selected but not fired.

#include <bullet.h>
#include <glm/vec2.hpp>
#include <vector>

namespace weapons
{
	constexpr int slotCount = 4;

	// What the HUD needs to draw one slot.
	struct SlotView
	{
		BulletStyle style = BulletStyle::Standard;
		float ready = 1.f;   // 0 just fired .. 1 ready
		int ammo = -1;       // -1: unlimited
		int maxAmmo = -1;
		bool selected = false;
		bool usable = true;  // has ammo, and fires in this build
	};

	// A new round: ammo refilled, cooldowns cleared. The selected slot is a
	// choice, not something that happened, so it survives.
	void reset();

	// Keys 1-4 and the wheel. The wheel only switches without Shift; with
	// Shift it zooms (zoomControl).
	void handleInput();

	// Advances cooldowns and any burst in progress, fires the selected weapon
	// if the trigger is held and it is ready, and appends the shots to `out`.
	// Returns how many shots left the ship this frame. Game time.
	int update(float gameDeltaTime, bool triggerHeld, glm::vec2 origin, glm::vec2 aim,
		std::vector<Bullet> &out);

	SlotView slot(int index);

	void debugUi();
}
