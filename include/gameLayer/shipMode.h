#pragma once

// Fight and flight (sight roadmap M1). Tab switches the player's ship between
// two modes:
//
//   Fight   shield up, normal speed. Every weapon passes through rocks and
//           stops only at a core: the beam burns no rock.
//   Flight  faster, with no shield. Guns and missiles work as in a fight;
//           the beam stops at the first rock on its line and mines it.
//
// Switching to flight drops the shield -- its break plays -- and keeps the
// bar where it was, so a full bar can still cloak. Switching to fight raises
// nothing at once: the bar starts empty and the shield returns when it has
// refilled, as after a break, so Tab is never a shield thrown up before a hit.
//
// Each ship has a mode, as each has its energy: the player's in the session,
// every enemy's on its Enemy. Enemies stay in fight mode for now.

#include <energy.h>
#include <engine/movement.h>

namespace shipMode
{
	enum class Mode { Fight, Flight };

	// Switches `mode` to the other one and applies it to the ship's energy.
	void toggle(Mode &mode, energy::Energy &energy);

	// The beam stops at the first rock and mines it, rather than passing
	// every rock but a core.
	bool beamMines(Mode mode);

	// `options` with this mode's speed: flight multiplies the top speed and
	// the acceleration (Flight section).
	movement::Options movementFor(Mode mode, movement::Options options);

	void debugUi(Mode &mode, energy::Energy &energy);
}
