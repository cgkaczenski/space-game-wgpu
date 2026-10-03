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
#include <shipId.h>

constexpr float bulletHitboxRadius = 20.f;
// Trail sprites sit ahead of `position`; keep radius and shift the circle to the nose.
constexpr float bulletHitboxForwardOffset = 100.f;

// Which weapon's look a shot has. Data, so the drawing can pick art and colour
// without knowing about weapons. Laser is here for the HUD icon and for C3's
// beam; no Bullet carries it yet.
enum class BulletStyle : unsigned char
{
	Standard,
	Heavy,
	Missile,
	Laser,
};

// How a shot flies. The switch R9 anticipated: a motion kind on the data, not a
// class per bullet. Straight is `update` alone; a missile is also steered each
// frame by weapons::steerMissiles before it moves (gameplay roadmap C3).
enum class BulletMotion : unsigned char
{
	Straight,
	Missile,
};

struct Bullet
{
	glm::vec2 position = {};
	glm::vec2 fireDirection = {};

	BulletMotion motion = BulletMotion::Straight;

	// Missile state. `age` is seconds since launch. `target` is the ship it
	// homes on -- an enemy, or the player (B1) -- noShip for none: never had
	// one, or lost it (died, or cloaked). `aimDirection` is where to head with
	// no target: where its shooter aimed, or, once a target is lost, the
	// heading it had then, so it flies on straight. `topSpeed` is its motor's.
	float age = 0.f;
	ShipId target = noShip;
	glm::vec2 aimDirection = {};
	float topSpeed = 0.f;

	// Velocity that is not along `fireDirection`. Player shots carry the
	// ship's velocity here, so muzzle speed is on top of how the gun is
	// already moving -- a forward shot cannot be caught by raising top speed.
	// A missile adds a push out from the wing as well, and that whole drift
	// fades once its motor lights.
	glm::vec2 drift = {};

	// What the weapon that fired it gave it (gameplay roadmap C2).
	float damage = 0.1f;
	float size = 1.f;    // scales the sprite, the glow and the hitbox
	BulletStyle style = BulletStyle::Standard;

	// Game time, already scaled by the game speed (see gameClock.h).
	void update(float gameDeltaTime)
	{
		position += (fireDirection * speed + drift) * gameDeltaTime;
	}

	collision::Circle getHitbox() const
	{
		return {position + fireDirection * (bulletHitboxForwardOffset * size),
			bulletHitboxRadius * size};
	}

	// Who fired it (B1): the player, or an enemy's id. What it can hit follows
	// from that -- an enemy's shots hit the player, the player's hit enemies.
	ShipId shooter = playerShip;
	bool fromEnemy() const { return shooter != playerShip; }

	// Its weapon's modifiers (B2): what a hit that reaches the hull does
	// besides the damage.
	bool stun = false;
	bool lockdown = false;

	float speed = 3000;
};
