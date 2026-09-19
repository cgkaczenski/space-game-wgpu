#define GLM_ENABLE_EXPERIMENTAL
#include <enemyAi.h>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace enemyAi
{

namespace
{
	bool spawningEnabled = false;
	bool conesVisible = true;

	constexpr size_t maxEnemies = 15;

	// Sight and search (gameplay roadmap C5).
	float hearingRadius = 400.f;        // noticed at any angle this close
	float searchSeconds = 4.f;          // scanning at the last known position
	float scanRate = 1.2f;              // radians per second, sweeping while scanning
	float arriveDistance = 200.f;       // close enough to the last known position
	float wanderSpeedFraction = 0.45f;  // of its top speed, while unaware

	float randomBetween(float a, float b)
	{
		return a + (b - a) * (rand() / (float)RAND_MAX);
	}

	glm::vec2 rotated(glm::vec2 v, float radians)
	{
		const float c = std::cos(radians), s = std::sin(radians);
		return {v.x * c - v.y * s, v.x * s + v.y * c};
	}

	// Turns the enemy's facing toward `direction` (unit) by at most rate * dt.
	void turnToward(Enemy &enemy, glm::vec2 direction, float rate, float dt)
	{
		const glm::vec2 v = enemy.viewDirection;
		const float angle = std::atan2(v.x * direction.y - v.y * direction.x, glm::dot(v, direction));
		enemy.viewDirection = glm::normalize(rotated(v, std::clamp(angle, -rate * dt, rate * dt)));
	}

	bool canSee(const Enemy &enemy, glm::vec2 playerPos, bool playerHidden)
	{
		if (playerHidden) { return false; }
		const glm::vec2 toPlayer = playerPos - enemy.position;
		const float distance = glm::length(toPlayer);
		if (distance <= hearingRadius) { return true; }
		if (distance > enemy.sightRange) { return false; }
		const float cosine = glm::dot(toPlayer / distance, enemy.viewDirection);
		return cosine >= std::cos(enemy.sightHalfAngle);
	}

	// Lost the player: fly to where it was last seen, then turn slowly to scan
	// for it. The sweep is visible, because the cone turns with the nose.
	void search(Enemy &enemy, float dt)
	{
		const glm::vec2 toLast = enemy.lastKnown - enemy.position;
		const float distance = glm::length(toLast);
		if (distance > arriveDistance)
		{
			turnToward(enemy, toLast / distance, enemy.turnSpeed, dt);
			movement::integrate(enemy.position, enemy.velocity, enemy.viewDirection, enemy.move, dt);
			return;
		}

		movement::integrate(enemy.position, enemy.velocity, {}, enemy.move, dt);
		const float sweep = (enemy.id % 2u) ? scanRate : -scanRate;
		enemy.viewDirection = glm::normalize(rotated(enemy.viewDirection, sweep * dt));
		enemy.searchLeft -= dt;
		if (enemy.searchLeft <= 0.f) { enemy.awareness = Enemy::Awareness::Unaware; }
	}

	// Nothing to go on: drift in slow curves at part speed, changing the curve
	// every couple of seconds. Patrols replace this once levels give places.
	void wander(Enemy &enemy, float dt)
	{
		enemy.wanderTimer -= dt;
		if (enemy.wanderTimer <= 0.f)
		{
			enemy.wanderTurn = randomBetween(-0.8f, 0.8f);
			enemy.wanderTimer = randomBetween(1.5f, 3.f);
		}
		enemy.viewDirection = glm::normalize(rotated(enemy.viewDirection, enemy.wanderTurn * dt));
		movement::integrate(enemy.position, enemy.velocity,
			enemy.viewDirection * wanderSpeedFraction, enemy.move, dt);
	}

	// Sniper's hang-back ring. Spawn offset is 2000, so they appear already
	// near this range and hold it rather than charging in.
	constexpr float preferredDistance = 1900.f;
	constexpr float rangeSlack = 300.f;

	glm::vec2 towardPlayer(glm::vec2 from, glm::vec2 playerPos, float *distance)
	{
		glm::vec2 toPlayer = playerPos - from;
		const float length = glm::length(toPlayer);
		if (distance) { *distance = length; }
		if (length == 0.f) { return {1.f, 0.f}; }
		return toPlayer / length;
	}

	// Shared gun: aligned and the cooldown is cold. Starts the cooldown on
	// the shot so both policies do not each copy the same four lines.
	bool tickGun(Enemy &enemy, float gameDeltaTime, bool aligned)
	{
		bool shoot = aligned;
		if (shoot)
		{
			if (enemy.firedTime <= 0.f)
			{
				enemy.firedTime = enemy.fireTimeReset;
			}
			else
			{
				shoot = false;
			}
		}

		enemy.firedTime -= gameDeltaTime;
		if (enemy.firedTime < 0.f) { enemy.firedTime = 0.f; }
		return shoot;
	}

	bool alignedTo(const Enemy &enemy, glm::vec2 directionToPlayer)
	{
		return glm::length(directionToPlayer + enemy.viewDirection) >= enemy.fireRange;
	}

	// Fly where you look, and look at the player. The old single behaviour.
	bool closeIn(Enemy &enemy, float gameDeltaTime, glm::vec2 playerPos)
	{
		const glm::vec2 directionToPlayer = towardPlayer(enemy.position, playerPos, nullptr);

		glm::vec2 newDirection = {};
		if (glm::length(directionToPlayer + enemy.viewDirection) <= 0.2f)
		{
			if (rand() % 2)
			{
				newDirection = glm::vec2(directionToPlayer.y, -directionToPlayer.x);
			}
			else
			{
				newDirection = glm::vec2(-directionToPlayer.y, directionToPlayer.x);
			}
		}
		else
		{
			newDirection =
				gameDeltaTime * enemy.turnSpeed * directionToPlayer + enemy.viewDirection;
		}

		enemy.viewDirection = glm::normalize(newDirection);

		// Wants to go where it looks. This used to multiply the speed by the
		// length of newDirection -- within a few percent of 1, so a per-frame
		// wobble rather than a feature -- and is plain full intent now.
		movement::integrate(enemy.position, enemy.velocity, enemy.viewDirection,
			enemy.move, gameDeltaTime);

		return tickGun(enemy, gameDeltaTime, alignedTo(enemy, directionToPlayer));
	}

	// Face the player, move independently: retreat when too close, close the
	// gap when too far, orbit in between. That split is the whole policy --
	// CloseIn cannot hang back because it only has one vector.
	bool keepDistance(Enemy &enemy, float gameDeltaTime, glm::vec2 playerPos)
	{
		float distance = 0.f;
		const glm::vec2 directionToPlayer = towardPlayer(enemy.position, playerPos, &distance);

		glm::vec2 facing =
			gameDeltaTime * enemy.turnSpeed * directionToPlayer + enemy.viewDirection;
		const float facingLength = glm::length(facing);
		if (facingLength > 0.f)
		{
			enemy.viewDirection = facing / facingLength;
		}

		glm::vec2 orbit(-directionToPlayer.y, directionToPlayer.x);
		if (enemy.type.y % 2u == 0u) { orbit = -orbit; }

		glm::vec2 move = orbit;
		if (distance < preferredDistance - rangeSlack)
		{
			move = glm::normalize(-directionToPlayer + orbit * 0.6f);
		}
		else if (distance > preferredDistance + rangeSlack)
		{
			move = directionToPlayer;
		}

		movement::integrate(enemy.position, enemy.velocity, move, enemy.move, gameDeltaTime);

		return tickGun(enemy, gameDeltaTime, alignedTo(enemy, directionToPlayer));
	}

	void rollLoadout(Enemy &e)
	{
		if (e.behaviour == Enemy::Behaviour::KeepDistance)
		{
			// Column 2 of the sheet, so they read as a different ship. Two rows
			// so they do not all orbit the same way (keepDistance uses type.y).
			e.type = (rand() % 2) ? glm::uvec2{2, 0} : glm::uvec2{2, 1};
			e.move = movement::instant(1200 + rand() % 400); // 1200 .. 1600
			e.turnSpeed = 3.5f + (rand() % 1000) / 1000.f; // 3.5 .. 4.5
			e.fireRange = 1.7f + (rand() % 1000) / 5000.f; // 1.7 .. 1.9
			e.fireTimeReset = 0.8f + (rand() % 1000) / 1000.f; // 0.8 .. 1.8 s
			e.bulletSpeed = 2800 + rand() % 1200;          // 2800 .. 4000
			// Further and narrower than a rusher: it spots the player first,
			// and keeps its distance while it does.
			e.sightRange = 3500.f;
			e.sightHalfAngle = 0.524f;                    // 30 degrees either side
			return;
		}

		e.type = (rand() % 2) ? glm::uvec2{0, 0} : glm::uvec2{0, 1};
		e.move = movement::instant(800 + rand() % 1000);  // 800 .. 1800
		e.turnSpeed = 2.2f + (rand() % 1000) / 500.f;   // 2.2 .. 4.2
		e.fireRange = 1.5f + (rand() % 1000) / 2000.f;
		e.fireTimeReset = 0.1f + (rand() % 1000) / 500.f; // 0.1 .. 2.1 s
		e.bulletSpeed = 1000 + rand() % 1600;            // 1000 .. 2600
	}
}

void stun(Enemy &enemy, glm::vec2 push, float seconds)
{
	enemy.stunned = seconds;
	enemy.knockback = push;
	// A fast tumble either way, so two rammed ships do not turn in step.
	const float turns = 6.f + (rand() % 1000) / 200.f; // 6 .. 11 rad/s
	enemy.spinRate = (rand() % 2) ? turns : -turns;
}

void alert(Enemy &enemy, glm::vec2 playerPos)
{
	enemy.awareness = Enemy::Awareness::Engaged;
	enemy.lastKnown = playerPos;
	const glm::vec2 toPlayer = playerPos - enemy.position;
	const float distance = glm::length(toPlayer);
	if (distance > 0.001f) { enemy.viewDirection = toPlayer / distance; }
}

bool showCones() { return conesVisible; }

bool update(Enemy &enemy, float gameDeltaTime, glm::vec2 playerPos, bool playerHidden)
{
	if (enemy.stunned > 0.f)
	{
		enemy.stunned -= gameDeltaTime;
		enemy.position += enemy.knockback * gameDeltaTime;
		enemy.knockback *= std::exp(-4.f * gameDeltaTime);

		const float a = enemy.spinRate * gameDeltaTime;
		const glm::vec2 v = enemy.viewDirection;
		enemy.viewDirection = {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)};
		return false;
	}

	if (canSee(enemy, playerPos, playerHidden))
	{
		enemy.awareness = Enemy::Awareness::Engaged;
		enemy.lastKnown = playerPos;
	}
	else if (enemy.awareness == Enemy::Awareness::Engaged)
	{
		// Just lost sight -- out of the cone, or cloaked in front of it. Go to
		// where the player was.
		enemy.awareness = Enemy::Awareness::Searching;
		enemy.searchLeft = searchSeconds;
	}

	switch (enemy.awareness)
	{
	case Enemy::Awareness::Searching:
		search(enemy, gameDeltaTime);
		return false;

	case Enemy::Awareness::Unaware:
		wander(enemy, gameDeltaTime);
		return false;

	case Enemy::Awareness::Engaged:
		break;
	}

	switch (enemy.behaviour)
	{
	case Enemy::Behaviour::CloseIn:
		return closeIn(enemy, gameDeltaTime, playerPos);
	case Enemy::Behaviour::KeepDistance:
		return keepDistance(enemy, gameDeltaTime, playerPos);
	}
	return false;
}

Enemy spawnAt(glm::vec2 position, glm::vec2 facing, Enemy::Behaviour behaviour)
{
	static unsigned int nextId = 1; // 0 means "no enemy"

	Enemy e;
	e.id = nextId++;
	e.behaviour = behaviour;
	e.position = position;
	e.viewDirection = facing;
	rollLoadout(e);
	return e;
}

Enemy spawnNear(glm::vec2 playerPos, Enemy::Behaviour behaviour)
{
	glm::vec2 offset(2000, 0);
	offset = glm::vec2(glm::vec4(offset, 0, 1) * glm::rotate(glm::mat4(1.f),
		glm::radians((float)(rand() % 360)), glm::vec3(0, 0, 1)));

	// Outward: a default +X facing would look at the player from the west
	// half of the ring and engage on spawn, skipping unaware wander (C5).
	return spawnAt(playerPos + offset, glm::normalize(offset), behaviour);
}

Enemy spawnNear(glm::vec2 playerPos)
{
	const auto behaviour = (rand() % 2)
		? Enemy::Behaviour::CloseIn
		: Enemy::Behaviour::KeepDistance;
	return spawnNear(playerPos, behaviour);
}

void updateSpawning(std::vector<Enemy> &enemies, float &timerSeconds,
	glm::vec2 playerPos, float gameDeltaTime)
{
	if (!spawningEnabled || enemies.size() >= maxEnemies) { return; }

	timerSeconds -= gameDeltaTime;

	if (timerSeconds < 0)
	{
		timerSeconds = rand() % 6 + 1;

		enemies.push_back(spawnNear(playerPos));
		if (rand() % 3 == 0)
		{
			enemies.push_back(spawnNear(playerPos));
			enemies.push_back(spawnNear(playerPos));
		}
	}
}

void debugUi()
{
	ImGui::Checkbox("Spawn waves", &spawningEnabled);
	ImGui::SameLine();
	ImGui::Checkbox("Vision cones", &conesVisible);
	ImGui::SliderFloat("Hearing", &hearingRadius, 0.f, 1500.f, "%.0f");
	ImGui::SliderFloat("Search time", &searchSeconds, 0.5f, 15.f, "%.1f s");
	ImGui::SliderFloat("Scan speed", &scanRate, 0.2f, 5.f, "%.1f rad/s");
	ImGui::SliderFloat("Wander speed", &wanderSpeedFraction, 0.f, 1.f, "%.2f");
}

}
