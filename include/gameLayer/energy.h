#pragma once

// A ship's energy, and the rules that tie its shield and cloak to it (gameplay
// roadmap C1). The shield and cloak modules are how those look; this is when
// they are allowed to be up. Each ship owns one (B1): the player's in the
// session, every enemy's on the Enemy.
//
//   Full      energy full -- and the shield up, if the ship has one.
//   Breaking  a hit landed on the shield: it was blocked, energy dropped to
//             zero, and the shield stays up until that hit's ripple finishes.
//             More hits in that window are blocked too, and ripple, but do not
//             push the break back -- constant fire cannot hold a shield up.
//   Down      energy refills slowly; the shield returns when it is full. A hit
//             here does damage and empties the bar again.
//   Cloaked   only reachable from Full, with a full bar, by a ship that can
//             cloak. Energy zero, shield down, no refill. The ship drifts on
//             the momentum it had and cannot be hit, thrust, or fire; it can
//             still turn. Firing is how it leaves, into Down.
//
// A ship can have energy and no shield (B1): its bar fills and empties by the
// same rules, but nothing blocks -- a hit with the bar full does damage and
// empties it, as it would with the bar down. What a full bar is for, then, is
// the cloak, if the ship has one.

#include <shieldBubble.h>
#include <glm/vec2.hpp>

namespace energy
{
	enum class HitResult
	{
		Blocked,  // the shield took it
		Damaged,  // it reached the hull
		Missed,   // cloaked: it passed through
	};

	enum class State { Full, Breaking, Down, Cloaked };

	struct Energy
	{
		bool hasShield = true;     // a full bar raises a shield
		bool canCloak = true;      // a full bar can be spent on the cloak
		State state = State::Full;
		float amount = 1.f;        // 0..1
		float breakTimer = 0.f;    // seconds left before a Breaking shield drops
		float beamRippleLeft = 0.f;
		shield::Bubble bubble;     // the shield it raises and breaks, as it looks
	};

	// Full, the shield up if it has one, not cloaked. A default Energy is
	// already this, with a shield and a cloak; call it after changing what the
	// ship has.
	void reset(Energy &energy);

	// Advances the break and the refill. Game time.
	void update(Energy &energy, float gameDeltaTime);

	// A shot reached the ship. `offsetFromShip` places the ripple.
	HitResult onHit(Energy &energy, glm::vec2 offsetFromShip);

	// A beam is on the ship this frame (B1). A beam is blocked by a shield and
	// cannot break one -- the player's beam has always played by this (C3b) --
	// so a raised shield holds for as long as the beam lasts, rippling where
	// it burns. With no shield up it reaches the hull, and keeps the bar empty
	// while it does. Cloaked, it passes through.
	HitResult onBeam(Energy &energy, glm::vec2 offsetFromShip, float gameDeltaTime);

	// Gives the ship a shield or takes it away during play (sight roadmap
	// M1). Taking it drops a raised shield -- the break plays -- and keeps the
	// bar's level; a shield mid-break goes down. Giving it starts the bar
	// empty, Down: the shield rises when the bar has refilled, never at once.
	// Cloaked, giving it uncloaks into that.
	void setShield(Energy &energy, bool hasShield);

	// E, for the player. Does nothing unless the ship can cloak and its bar
	// is full.
	void cloak(Energy &energy);

	// Firing. Does nothing if not cloaked.
	void uncloak(Energy &energy);

	bool isCloaked(const Energy &energy);

	// 0..1, for the HUD.
	float level(const Energy &energy);

	// One ship's state -- the player's -- and the shared tuning.
	void debugUi(Energy &energy);
}
