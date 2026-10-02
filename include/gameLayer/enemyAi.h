#pragma once

// What an enemy does: two policies over the same data, plus where new enemies
// come from (roadmap R9).
//
// CloseIn flies along its facing, at the player. KeepDistance aims at the
// player and moves separately, so it can back off and still shoot. Both are
// functions over Enemy -- a member cannot be swapped per enemy, and neither
// carries a renderer dependency. This lives in gameLayer, not engine: the
// steering is generic, but chasing, hanging back, and the fire rule are this
// game's. The generic half, turning an intent into motion, is
// engine/movement: each policy only decides an intent -- where to face, where
// to thrust -- and the same movement::step as the player's moves its body
// (R8, P1).

#include <enemy.h>
#include <vector>

namespace enemyAi
{
	// What the AI decided this frame about the gun. The gun itself is the
	// enemy's loadout, fired by the caller through weapons::update -- the same
	// function as the player's (gameplay roadmap B1). Only a fighting enemy's
	// gun runs at all, cooldown included, as it always has: an enemy that
	// engages after a while away from the fight does not have a shot waiting.
	struct Orders
	{
		bool fighting = false;   // engaged: run the gun this frame
		bool trigger = false;    // and lined up: pull it
	};

	// Steers and moves one enemy, using `enemy.behaviour` to pick the policy,
	// and decides the trigger. Every part of it runs on `gameDeltaTime`,
	// already scaled (see gameClock.h).
	//
	// It only fights what it can see (gameplay roadmap C5): the player within
	// its cone, or within earshot at any angle, and never while `playerHidden`
	// -- cloaked. Seeing engages it; losing sight sends it to search where it
	// last saw the player; a search that finds nothing leaves it unaware, and
	// an unaware enemy wanders. Only an engaged enemy fires.
	//
	// `comeBackTo`, when given, is somewhere it should be heading instead --
	// back into the closing circle (gameplay roadmap L4). An enemy that is not
	// fighting flies straight there at full speed, still looking; one that is
	// engaged keeps fighting wherever the fight goes. A search is dropped, not
	// resumed, so it does not fly back out to where the player was.
	//
	// `playerVelocity` is for leading: with momentum, a policy flies to where
	// the player is going, not where they are (P1, engine/steering).
	Orders update(Enemy &enemy, float gameDeltaTime, glm::vec2 playerPos, glm::vec2 playerVelocity,
		bool playerHidden, const glm::vec2 *comeBackTo = nullptr);

	// Something of the player's hit it: it engages at once, turned toward
	// where the player is, whatever it could see.
	void alert(Enemy &enemy, glm::vec2 playerPos);

	// The debug toggle for drawing sight cones. On by default.
	bool showCones();

	// Rammed: struck by `impulse` (movement::push -- a lighter enemy is
	// thrown further), spinning, and disabled for `seconds`. While disabled
	// it tumbles: no thrust, no speed cap, and a quick drag that brings the
	// blow down in a fraction of a second, so it is thrown far but not
	// forever. It neither steers nor fires, and recovers facing wherever it
	// ended up.
	void stun(Enemy &enemy, glm::vec2 impulse, float seconds);

	// A new enemy on a ring around the player. The one-argument form picks a
	// behaviour at random and rolls that policy's loadout.
	Enemy spawnNear(glm::vec2 playerPos);

	// A new enemy exactly here, facing `facing` (unit): a level's placement.
	// Unaware, with the behaviour's loadout rolled like any other.
	// `weapon` is a slot of weapons::shipWeapon -- a placement's choice -- or
	// -1 to roll one at random (B1). `shield` is the placement's choice too;
	// Random rolls it at the Shield chance. The cloak and the ram are always
	// rolled, for now.
	Enemy spawnAt(glm::vec2 position, glm::vec2 facing, Enemy::Behaviour behaviour, int weapon = -1,
		AbilityChoice shield = AbilityChoice::Random);
	Enemy spawnNear(glm::vec2 playerPos, Enemy::Behaviour behaviour);

	// Counts `timerSeconds` down by `gameDeltaTime` and spawns a wave when it
	// runs out, if spawning is on and there is room. The timer is the caller's
	// because restart resets it by resetting the gameplay data, and what
	// restart means is R10's question, not this one's.
	void updateSpawning(std::vector<Enemy> &enemies, float &timerSeconds,
		glm::vec2 playerPos, float gameDeltaTime);

	// One class's tuning, together -- how it flies and how it fights, shared
	// by every enemy of the class. In the Enemies section, and beside a
	// selected enemy in the level editor.
	void classUi(Enemy::Behaviour behaviour);

	void debugUi();
}
