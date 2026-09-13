#pragma once

// What a bullet is. How it looks is bulletLook (glow and sprite, roadmap R9).
//
// `update` stays a member, unlike the enemy's: a bullet has no behaviour to
// swap, only `position += velocity`, and that is R8's integrator. If flight
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

	void update(float deltaTime, float speedMultiplier = 1.f)
	{
		position += fireDirection * deltaTime * speed * speedMultiplier;
	}

	collision::Circle getHitbox() const
	{
		return {position + fireDirection * bulletHitboxForwardOffset, bulletHitboxRadius};
	}

	bool isEnemy = 0;
	float speed = 3000;
};
