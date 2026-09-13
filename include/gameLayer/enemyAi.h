#pragma once

// What an enemy does: two policies over the same data, plus where new enemies
// come from (roadmap R9).
//
// CloseIn flies along its facing, at the player. KeepDistance aims at the
// player and moves separately, so it can back off and still shoot. Both are
// functions over Enemy -- a member cannot be swapped per enemy, and neither
// carries a renderer dependency. This lives in gameLayer, not engine: the
// steering is generic, but chasing, hanging back, and the fire rule are this
// game's. When R8 pulls the integration step out, the generic half has
// somewhere to go.

#include <enemy.h>
#include <vector>

namespace enemyAi
{
	// Steers, moves, and runs the fire cooldown for one enemy, using
	// `enemy.behaviour` to pick the policy. True when it fires this frame; the
	// caller creates the bullet. Every part of it runs on game time:
	// `deltaTime` is the real frame time and `speedMultiplier` the game speed.
	bool update(Enemy &enemy, float deltaTime, glm::vec2 playerPos, float speedMultiplier);

	// A new enemy on a ring around the player. The one-argument form picks a
	// behaviour at random and rolls that policy's loadout.
	Enemy spawnNear(glm::vec2 playerPos);
	Enemy spawnNear(glm::vec2 playerPos, Enemy::Behaviour behaviour);

	// Counts `timerSeconds` down by `gameDeltaTime` and spawns a wave when it
	// runs out, if spawning is on and there is room. The timer is the caller's
	// because restart resets it by resetting the gameplay data, and what
	// restart means is R10's question, not this one's.
	void updateSpawning(std::vector<Enemy> &enemies, float &timerSeconds,
		glm::vec2 playerPos, float gameDeltaTime);

	void debugUi();
}
