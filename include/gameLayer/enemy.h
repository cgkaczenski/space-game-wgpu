#pragma once

// What an enemy is: plain data. What it does is enemyAi, and how it looks is
// renderSpaceShip in shipSprite.h (roadmap R9). Nothing here includes the
// renderer, so a behaviour can be read, swapped or tested without it.

#include <glm/vec2.hpp>
#include <shipHitbox.h>

constexpr float enemyShipSize = 250.f;

struct Enemy
{
	glm::uvec2 type = {}; //used to index into the texture atlas
	glm::vec2 position = {};

	glm::vec2 viewDirection = {1,0};

	// Movement. R8 decides the shape of these.
	float speed = 1500.f;
	float turnSpeed = 3.f;

	// The gun, parked here because there is no Weapon yet. bulletSpeed is the
	// weapon's -- flight uses Bullet::speed, and this is copied onto it at fire.
	float firedTime = 1.f;
	float fireTimeReset = 0.2;
	float fireRange = 1.5;
	float bulletSpeed = 2000;

	float life = 1.f;

	collision::Circle getHitbox() const
	{
		return game::shipHitbox(position, enemyShipSize);
	}
};
