#pragma once

// The player's half of movement: keys into an intent, the ship's facing, and
// this game's tuning. The integrator is engine/movement and knows nothing about
// keys (roadmap R8).
//
// Facing and aim are separate outputs, because one control scheme separates
// them: turning with A/D while the mouse aims the gun. In the other schemes the
// ship simply faces the mouse and the two are the same vector.

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

	struct Result
	{
		float throttle = 0.f;   // 1 while thrusting, for the plume
		glm::vec2 facing = {};  // where the hull points: the sprite and the plume
		glm::vec2 aim = {};     // where the player's shots go
	};

	// Reads the keys, turns and moves the ship by `gameDeltaTime`.
	// `mouseDirection` is a unit vector from the ship toward the pointer.
	// `facing` is the ship's heading, kept by the caller between frames so
	// TurnWithKeys has something to turn.
	Result update(glm::vec2 &position, glm::vec2 &velocity, glm::vec2 &facing,
		glm::vec2 mouseDirection, float gameDeltaTime);

	void debugUi();
}
