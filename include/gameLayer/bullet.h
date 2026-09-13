#pragma once

// What a bullet is. How it looks is bulletLook (glow and sprite, roadmap R9).
//
// `update` stays a member, unlike the enemy's: a bullet has no behaviour to
// swap. It does not go through engine/movement either -- it flies at constant
// velocity with no intent, and an integrator adds nothing to that. If flight
// ever varies (homing, wave), it becomes a switch over a motion kind here
// rather than a class per bullet.

#include <glm/vec2.hpp>
#include <engine/collisionSystem.h>

constexpr float bulletHitboxRadius = 20.f;
// Trail sprites sit ahead of `position`; keep radius and shift the circle to the nose.
constexpr float bulletHitboxForwardOffset = 100.f;

struct Bullet
{
	glm::vec2 position = {};
	glm::vec2 fireDirection = {};

	// Game time, already scaled by the game speed (see gameClock.h).
	void update(float gameDeltaTime)
	{
		position += fireDirection * gameDeltaTime * speed;
	}

	collision::Circle getHitbox() const
	{
		return {position + fireDirection * bulletHitboxForwardOffset, bulletHitboxRadius};
	}

	bool isEnemy = 0;
	float speed = 3000;
};
