#pragma once

// What an enemy is: plain data. What it does is enemyAi, and how it looks is
// renderSpaceShip in shipSprite.h (roadmap R9). Nothing here includes the
// renderer, so a behaviour can be read, swapped or tested without it.

#include <glm/vec2.hpp>
#include <engine/movement.h>
#include <shipHitbox.h>

constexpr float enemyShipSize = 250.f;

struct Enemy
{
	glm::uvec2 type = {}; //used to index into the texture atlas
	glm::vec2 position = {};

	glm::vec2 viewDirection = {1,0};

	// Which function enemyAi calls. Same data, same draw; the policy is not a
	// subclass. CloseIn flies at the player. KeepDistance faces them and holds
	// a range -- movement and facing are not the same vector.
	enum class Behaviour { CloseIn, KeepDistance };
	Behaviour behaviour = Behaviour::CloseIn;

	// Movement: the same integrator and options as the player. Enemies roll
	// Instant, which is how they have always moved; a Momentum enemy is a
	// loadout change, not new code.
	glm::vec2 velocity = {};
	movement::Options move = movement::instant(1500.f);
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
