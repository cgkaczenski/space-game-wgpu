#pragma once

// The interior is slow (sight roadmap W2): inside a field's paint, every ship
// -- the player and each enemy alike -- flies slower, so a chase into a field
// slows both sides and the fighting there is close. The open, lanes and
// clearings included, is normal.
//
// This is the policy; the mechanism is engine/movement's Medium, which scales
// a body's own top speed, so flight mode's boost and an enemy's rolled speed
// still hold, multiplied. A fast ship either carries its speed a little way
// in, losing the excess over a moment (the default), or snaps to the limit.
//
// Not the ram's surge, which sets its speed outright and bursts through, nor
// a cloaked drift, which has no top speed to scale. Shots keep their speed.

#include <engine/movement.h>
#include <glm/vec2.hpp>

namespace interior
{
	// What a ship at `position` is flying through this frame.
	movement::Medium at(glm::vec2 position);

	void debugUi();
}
