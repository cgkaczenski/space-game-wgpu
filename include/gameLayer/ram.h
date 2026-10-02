#pragma once

// The ram (gameplay roadmap C4b). Space winds up for a moment -- the ship
// dips back as a prow of shield brightens in front of it, turning with the
// mouse -- then surges the way it is facing, past its normal top speed.
// Anything the prow strikes takes damage, is knocked aside spinning and is
// disabled for a while; the game freezes for an instant on the hit, the world
// shakes, and the rammer takes nothing. An enemy can ram too (B1): struck by
// one, the player is hit, knocked aside and briefly stunned.
//
// This holds the timing and the rules' numbers. The game does the collision,
// because it owns the enemies; the shield draws the prow; effects draws the
// afterimages and streaks.

#include <shipId.h>
#include <glm/vec2.hpp>
#include <vector>

namespace ram
{
	// One ship's ram, as it stands (gameplay roadmap B1): every ship that can
	// ram owns one -- the player's in the session, an enemy's on the Enemy.
	// The numbers -- speed, damage, stun -- are shared, here.
	struct Ram
	{
		float windupLeft = 0.f;
		float activeLeft = 0.f;
		float cooldownLeft = 0.f;
		float lean = 0.f;                // 0 .. 1: the camera's lean, eased
		glm::vec2 heading = {1.f, 0.f};
		std::vector<ShipId> struck;      // this ram's: each ship is struck once
		// This ram's number, new each time one starts, across every ship: a
		// rock remembers the number that struck it, so each ram strikes a rock
		// once however many ships are ramming.
		unsigned serial = 0;
	};

	// Starts a ram along `direction` (unit) if it is off cooldown, and returns
	// whether it did. The heading then follows the aim until the surge, so the
	// lunge goes where the ship is pointing when it leaves, not where it was
	// pointing when the ram began.
	bool tryStart(Ram &ram, glm::vec2 direction);

	// Ends a ram at once -- its ship was stunned mid-lunge. The cooldown runs on.
	void stop(Ram &ram);

	// Game time. `aim` is unit: during the wind-up the heading tracks it;
	// during the surge it is ignored.
	void update(Ram &ram, float gameDeltaTime, glm::vec2 aim);

	bool windingUp(const Ram &ram);   // the dip before the surge
	bool active(const Ram &ram);      // the surge itself: the only time it strikes
	bool barrierUp(const Ram &ram);   // either: the prow is out and blocks shots from ahead

	// 0 .. 1: how lit the prow is. It brightens through the wind-up and is
	// full for the surge.
	float barrierLevel(const Ram &ram);
	glm::vec2 direction(const Ram &ram);

	// Where the camera should lean: ahead along the ram while it is on, easing
	// back after. Added to the camera like the shake.
	glm::vec2 cameraLean(const Ram &ram);

	// True the first time a given ship is struck in this ram, false after:
	// one ram hits each ship once, and can hit several.
	bool firstHit(Ram &ram, ShipId ship);

	// 0 just used .. 1 ready, for the HUD.
	float ready(const Ram &ram);

	// The shared numbers.
	float surgeSpeed();
	float windupBackSpeed();
	float hitDamage();
	float stunSeconds();        // a struck enemy is disabled this long
	float playerStunSeconds();  // and the player, struck by an enemy's ram, this long
	float knockbackSpeed();
	float hitStopSeconds();

	// The shared numbers, and one ram's state -- the player's.
	void debugUi(Ram &ram);
}
