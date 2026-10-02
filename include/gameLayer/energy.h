#pragma once

// The player's energy, and the rules that tie the shield and the cloak to it
// (gameplay roadmap C1). The shield and cloak modules are how those look; this
// is when they are allowed to be up.
//
//   Shielded  energy full, shield up.
//   Breaking  a hit landed on the shield: it was blocked, energy dropped to
//             zero, and the shield stays up until that hit's ripple finishes.
//             More hits in that window are blocked too, and ripple, but do not
//             push the break back -- constant fire cannot hold a shield up.
//   Down      energy refills slowly; the shield returns when it is full. A hit
//             here does damage and empties the bar again.
//   Cloaked   only reachable from Shielded, with a full bar. Energy zero,
//             shield down, no refill. The ship drifts on the momentum it had
//             and cannot be hit, thrust, or fire; it can still turn. Firing is
//             how the player leaves it, into Down.

#include <glm/vec2.hpp>

namespace energy
{
	enum class HitResult
	{
		Blocked,  // the shield took it
		Damaged,  // it reached the hull
		Missed,   // cloaked: it passed through
	};

	// A new round: full energy, shield up, not cloaked.
	void reset();

	// Advances the break and the refill. Game time.
	void update(float gameDeltaTime);

	// An enemy shot reached the ship. `offsetFromShip` places the ripple.
	HitResult onHit(glm::vec2 offsetFromShip);

	// An enemy's beam is on the ship this frame (B1). A beam is blocked by a
	// shield and cannot break one -- the rule the player's beam already plays
	// by against shields (C3b) -- so a raised shield holds for as long as the
	// beam lasts, rippling where it burns. With the shield down it reaches
	// the hull, and keeps the bar empty while it does. Cloaked, it passes
	// through.
	HitResult onBeam(glm::vec2 offsetFromShip, float gameDeltaTime);

	// E. Does nothing unless energy is full and the shield is up.
	void cloak();

	// Firing. Does nothing if not cloaked.
	void uncloak();

	bool isCloaked();

	// 0..1, for the HUD.
	float level();

	void debugUi();
}
