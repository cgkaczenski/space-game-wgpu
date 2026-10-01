#pragma once

// The player's half of movement: keys into an intent, and this game's tuning
// for the player's ship. The body and its step are engine/movement, which knows
// nothing about keys (roadmap R8, P1).
//
// Facing and aim are separate, because one control scheme separates them:
// turning with A/D while the mouse aims the gun. In the other schemes the ship
// simply faces the mouse and the two are the same vector.

#include <engine/movement.h>
#include <glm/vec2.hpp>

namespace playerMove
{
	enum class Controls
	{
		// W thrusts toward the mouse, S away from it. The ship faces the mouse.
		// A and D do nothing.
		MouseThrust,

		// A and D turn the ship, W and S thrust along where it faces. The
		// mouse aims the gun only, so a shot can leave at any angle to the hull.
		TurnWithKeys,

		// WASD are screen directions. The ship faces the mouse.
		ScreenDirections,
	};

	// Reads the keys into an intent for the player's body and steps it by
	// `gameDeltaTime` (gameplay roadmap P1: every ship is a movement::Body,
	// moved by movement::step). `mouseDirection` is a unit vector from the
	// ship toward the pointer. Sets the body's movement options -- the
	// player's tuning lives here -- and its turn rate: 0, the hull goes where
	// it is told at once. Returns where the player's shots go: the mouse, not
	// necessarily the hull.
	//
	// `drifting` is the cloak: no thrust, and the ship carries its velocity
	// with no falloff at all -- the path is something the player watches, not
	// something they control. The hull still turns as the control scheme says,
	// so the player can line up a shot. The chosen falloff applies again the
	// moment drifting ends.
	glm::vec2 update(movement::Body &ship, glm::vec2 mouseDirection, float gameDeltaTime, bool drifting);

	void debugUi();
}
