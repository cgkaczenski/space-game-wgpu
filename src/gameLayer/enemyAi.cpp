#define GLM_ENABLE_EXPERIMENTAL
#include <enemyAi.h>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <cstdlib>

namespace enemyAi
{

namespace
{
	bool spawningEnabled = false;

	constexpr size_t maxEnemies = 15;

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

		float length = glm::length(newDirection);
		enemy.viewDirection = glm::normalize(newDirection);

		length = glm::clamp(length, 0.1f, 3.f);
		enemy.position += enemy.viewDirection * gameDeltaTime * enemy.speed * length;

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

		enemy.position += move * gameDeltaTime * enemy.speed;

		return tickGun(enemy, gameDeltaTime, alignedTo(enemy, directionToPlayer));
	}

	void rollLoadout(Enemy &e)
	{
		if (e.behaviour == Enemy::Behaviour::KeepDistance)
		{
			// Column 2 of the sheet, so they read as a different ship. Two rows
			// so they do not all orbit the same way (keepDistance uses type.y).
			e.type = (rand() % 2) ? glm::uvec2{2, 0} : glm::uvec2{2, 1};
			e.speed = 1200 + rand() % 400;                 // 1200 .. 1600
			e.turnSpeed = 3.5f + (rand() % 1000) / 1000.f; // 3.5 .. 4.5
			e.fireRange = 1.7f + (rand() % 1000) / 5000.f; // 1.7 .. 1.9
			e.fireTimeReset = 0.8f + (rand() % 1000) / 1000.f; // 0.8 .. 1.8 s
			e.bulletSpeed = 2800 + rand() % 1200;          // 2800 .. 4000
			return;
		}

		e.type = (rand() % 2) ? glm::uvec2{0, 0} : glm::uvec2{0, 1};
		e.speed = 800 + rand() % 1000;
		e.turnSpeed = 2.2f + (rand() % 1000) / 500.f;   // 2.2 .. 4.2
		e.fireRange = 1.5f + (rand() % 1000) / 2000.f;
		e.fireTimeReset = 0.1f + (rand() % 1000) / 500.f; // 0.1 .. 2.1 s
		e.bulletSpeed = 1000 + rand() % 1600;            // 1000 .. 2600
	}
}

bool update(Enemy &enemy, float deltaTime, glm::vec2 playerPos, float speedMultiplier)
{
	// One clock for the whole behaviour. The cooldown and the turn used to run
	// on real time while the movement ran on game time, so at low game speed
	// enemies crawled but still turned and fired at full rate.
	const float gameDeltaTime = deltaTime * speedMultiplier;

	switch (enemy.behaviour)
	{
	case Enemy::Behaviour::CloseIn:
		return closeIn(enemy, gameDeltaTime, playerPos);
	case Enemy::Behaviour::KeepDistance:
		return keepDistance(enemy, gameDeltaTime, playerPos);
	}
	return false;
}

Enemy spawnNear(glm::vec2 playerPos, Enemy::Behaviour behaviour)
{
	Enemy e;
	e.behaviour = behaviour;
	e.position = playerPos;

	glm::vec2 offset(2000, 0);
	offset = glm::vec2(glm::vec4(offset, 0, 1) * glm::rotate(glm::mat4(1.f),
		glm::radians((float)(rand() % 360)), glm::vec3(0, 0, 1)));

	e.position += offset;
	rollLoadout(e);
	return e;
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
}

}
