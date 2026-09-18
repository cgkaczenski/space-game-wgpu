#pragma once

// The ram (gameplay roadmap C4b). Space winds up for a moment -- the ship
// dips back as a prow of shield brightens in front of it -- then surges
// toward the mouse, past its normal top speed. Anything the prow strikes
// takes damage, is knocked aside spinning and is disabled for a while; the
// game freezes for an instant on the hit, the world shakes, and the player
// takes nothing. Outside a ram, ships pass through each other as always.
//
// This holds the timing and the rules' numbers. The game does the collision,
// because it owns the enemies; the shield draws the prow; effects draws the
// afterimages and streaks.

#include <glm/vec2.hpp>

namespace ram
{
	// A new round: ready, not ramming.
	void reset();

	// Space. Starts a ram along `direction` (unit) if it is off cooldown, and
	// returns whether it did.
	bool tryStart(glm::vec2 direction);

	// Game time.
	void update(float gameDeltaTime);

	bool windingUp();   // the dip before the surge
	bool active();      // the surge itself: the only time it strikes
	bool barrierUp();   // either: the prow is out and blocks shots from ahead

	// 0 .. 1: how lit the prow is. It brightens through the wind-up and is
	// full for the surge.
	float barrierLevel();

	glm::vec2 direction();
	float surgeSpeed();
	float windupBackSpeed();
	float hitDamage();
	float stunSeconds();
	float knockbackSpeed();
	float hitStopSeconds();

	// Where the camera should lean: ahead along the ram while it is on, easing
	// back after. Added to the camera like the shake.
	glm::vec2 cameraLean();

	// True the first time a given enemy is struck in this ram, false after:
	// one ram hits each enemy once, and can hit several.
	bool firstHit(unsigned int enemyId);

	// 0 just used .. 1 ready, for the HUD.
	float ready();

	void debugUi();
}
