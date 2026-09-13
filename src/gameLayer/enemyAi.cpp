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
}

//returns true if should shoot bullet
bool update(Enemy &enemy, float deltaTime, glm::vec2 playerPos, float speedMultiplier)
{
	// One clock for the whole behaviour. The cooldown and the turn used to run
	// on real time while the movement ran on game time, so at low game speed
	// enemies crawled but still turned and fired at full rate.
	const float gameDeltaTime = deltaTime * speedMultiplier;

	glm::vec2 directionToPlayer = playerPos - enemy.position;
	if (glm::length(directionToPlayer) == 0) { directionToPlayer = {1,0}; }
	else { directionToPlayer = glm::normalize(directionToPlayer); }

	glm::vec2 newDirection = {};

	bool shoot = (glm::length(directionToPlayer + enemy.viewDirection) >= enemy.fireRange);

	if (shoot)
	{
		if (enemy.firedTime <= 0.f)
		{
			//we can shoot
			enemy.firedTime = enemy.fireTimeReset;
		}
		else
		{
			shoot = 0;
		}
	}

	enemy.firedTime -= gameDeltaTime;
	if (enemy.firedTime < 0) { enemy.firedTime = 0.f; }


	if (glm::length(directionToPlayer + enemy.viewDirection) <= 0.2)
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

	return shoot;
}

Enemy spawnNear(glm::vec2 playerPos)
{
	glm::uvec2 shipTypes[] = {{0,0}, {0,1}, {2,0}, {3, 1}};

	Enemy e;
	e.position = playerPos;

	glm::vec2 offset(2000, 0);
	offset = glm::vec2(  glm::vec4(offset,0,1) * glm::rotate(glm::mat4(1.f), glm::radians((float)(rand()%360)), glm::vec3(0,0, 1))  );

	e.position += offset;

	// The two ranges below were integer division and a bitwise AND: every
	// cooldown came out exactly 0.1 or 1.1 s, and turn speeds clumped on the
	// handful of values `rand() & 1000` can produce. Both are even spreads now.
	e.speed = 800 + rand() % 1000;
	e.turnSpeed = 2.2f + (rand() % 1000) / 500.f;  // 2.2 .. 4.2
	e.type = shipTypes[rand() % 4];
	e.fireRange = 1.5 + (rand() % 1000) / 2000.f;
	e.fireTimeReset = 0.1f + (rand() % 1000) / 500.f; // 0.1 .. 2.1 s
	e.bulletSpeed = rand() % 3000 + 1000;

	return e;
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
