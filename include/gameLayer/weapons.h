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
//   3  Missile       5 rounds, homing on the enemy nearest the mouse (C3a)
//   4  Laser         a held beam. Selectable, but the beam itself is C3
//
// Cooldowns tick for every slot at once, so switching does not reset them.
// Selecting is by keys 1-4 or the wheel (wrapping); an empty weapon can be
// selected but not fired.

#include <bullet.h>
#include <enemy.h>
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

	// Where a shot starts and what it can see. A missile needs more than a
	// bolt: the ship's velocity to launch alongside it, the mouse in the world
	// to pick a target, and the enemies to pick from.
	struct FireContext
	{
		glm::vec2 origin = {};
		glm::vec2 aim = {};           // unit, toward the mouse
		glm::vec2 shipVelocity = {};
		float shipSize = 0.f;
		glm::vec2 mouseWorld = {};
		const std::vector<Enemy> *enemies = nullptr;
	};

	// Advances cooldowns and any burst in progress, fires the selected weapon
	// if the trigger is held and it is ready, and appends the shots to `out`.
	// Returns how many shots left the ship this frame. Game time.
	int update(float gameDeltaTime, bool triggerHeld, const FireContext &context,
		std::vector<Bullet> &out);

	// Steers every missile in `bullets` (gameplay roadmap C3). Call before the
	// bullets move. A missile faces where it was aimed from the start and is
	// first pushed sideways from the wing with its motor off, keeping pace with
	// the ship. Then the motor lights from zero, the slide fades, and it
	// accelerates hard toward its target with a turn rate that keeps growing --
	// which is what makes it unable to miss: however it is moving, it
	// eventually turns tighter than any path that keeps missing. If its target
	// is gone it stops homing and flies on straight.
	void steerMissiles(std::vector<Bullet> &bullets, const std::vector<Enemy> &enemies,
		float gameDeltaTime);

	// How lit a missile's exhaust is: 0 during the launch push, then rising
	// with its speed, so the pick-up is visible.
	float missileThrottle(const Bullet &bullet);

	SlotView slot(int index);

	void debugUi();
}
