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
	// Stable for the enemy's life, never reused. The enemy list is erased from
	// as enemies die or leave, so an index is not an identity; a missile keeps
	// its target by this.
	unsigned int id = 0;

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

	// Disabled after being rammed (gameplay roadmap C4b): no steering, no
	// firing, just carried along by the blow and spinning, until `stunned`
	// runs out. See enemyAi::stun.
	float stunned = 0.f;          // seconds left
	glm::vec2 knockback = {};     // world units per second, fading
	float spinRate = 0.f;         // radians per second

	// What it knows (gameplay roadmap C5). It sees along a cone from its nose,
	// and hears anything very close. Engaged is the old behaviour -- it knows
	// where the player is. Searching flies to where it last saw the player and
	// scans. Unaware wanders.
	enum class Awareness { Unaware, Engaged, Searching };
	Awareness awareness = Awareness::Unaware;
	glm::vec2 lastKnown = {};     // where it last saw the player
	float searchLeft = 0.f;       // seconds of scanning left at lastKnown
	float wanderTurn = 0.f;       // radians per second, while unaware
	float wanderTimer = 0.f;      // until the next change of wander turn
	float sightRange = 2500.f;
	float sightHalfAngle = 0.785f; // radians: 45 degrees either side

	collision::Circle getHitbox() const
	{
		return game::shipHitbox(position, enemyShipSize);
	}
};
