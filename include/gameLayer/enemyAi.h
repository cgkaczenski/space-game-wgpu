#pragma once

// What an enemy does: chase the player, fire when facing it, and where new
// enemies come from (roadmap R9).
//
// Separate from Enemy so a behaviour is a function over data rather than a
// member of it -- a member cannot be swapped per enemy, and this one carries no
// renderer dependency to drag along. It lives in gameLayer, not engine: the
// steering is generic, but chasing the player, the random side-step and the
// fire rule are this game's. When R8 pulls the integration step out, the
// generic half has somewhere to go.

#include <enemy.h>
#include <vector>

namespace enemyAi
{
	// Steers, moves, and runs the fire cooldown for one enemy. True when it
	// fires this frame; the caller creates the bullet. Every part of it runs on
	// game time: `deltaTime` is the real frame time and `speedMultiplier` the
	// game speed, and all three -- turning, moving, cooldown -- use the product.
	bool update(Enemy &enemy, float deltaTime, glm::vec2 playerPos, float speedMultiplier);

	// A new enemy on a ring around the player, with its tuning rolled.
	Enemy spawnNear(glm::vec2 playerPos);

	// Counts `timerSeconds` down by `gameDeltaTime` and spawns a wave when it
	// runs out, if spawning is on and there is room. The timer is the caller's
	// because restart resets it by resetting the gameplay data, and what
	// restart means is R10's question, not this one's.
	void updateSpawning(std::vector<Enemy> &enemies, float &timerSeconds,
		glm::vec2 playerPos, float gameDeltaTime);

	void debugUi();
}
