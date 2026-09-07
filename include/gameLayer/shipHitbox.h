#pragma once

// What a ship's hitbox is in this game. The overlap maths is general and lives
// in engine/collisionSystem.h; that a ship is a square sprite whose circle is
// half its visual size is not -- it is this game's art, and it used to sit at
// the bottom of the general header.

#include <engine/collisionSystem.h>

namespace game
{
	// Sprite is visualSize x visualSize; radius is half of that so the circle
	// fits the ship square. Player, enemies, and ship-ship all use this.
	inline float shipHitboxRadius(float visualSize)
	{
		return visualSize * 0.5f;
	}

	inline collision::Circle shipHitbox(glm::vec2 center, float visualSize)
	{
		return {center, shipHitboxRadius(visualSize)};
	}
}
