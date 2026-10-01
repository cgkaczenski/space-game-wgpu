#define GLM_ENABLE_EXPERIMENTAL
#include <enemyAi.h>

#include <engine/steering.h>
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

	// How each class flies (gameplay roadmap P1). Every enemy is on Momentum,
	// as the player is -- thrust, a little drag, a top speed -- and each class
	// has its own feel. Rolled at spawn, so a change applies to new spawns
	// (Reset game respawns a level's).
	struct Flight
	{
		float mass;                  // for blows and bumps: the player's is 1
		float thrust;                // units per second squared at full intent
		float drag;                  // per second: coasting speed halves in ln 2 / drag
		float speedMin, speedMax;    // the top speed, rolled in this range
		float turnMin, turnMax;      // radians per second, rolled in this range
	};
	// Light and twitchy: 1.5 times the player's 6000 thrust, the old Instant
	// speeds as its cap.
	Flight rusherFlight = {0.8f, 9000.f, 0.6f, 800.f, 1800.f, 2.2f, 4.2f};
	// Heavy: 0.6 times the player's thrust, and slow to turn -- it used to
	// turn at 3.5 .. 4.5.
	Flight sniperFlight = {1.4f, 3600.f, 0.6f, 1200.f, 1600.f, 1.8f, 2.4f};

	// Stunned, a ship tumbles: no thrust, no cap -- the blow is far past any
	// top speed -- and this drag, the old knockback's fade, so the blow is
	// mostly spent in half a second.
	float stunDrag = 4.f;

	// How each class fights, flying on engine/steering (P1, step 2). Both
	// circle the player facing it, holding a range: a rusher close and fast,
	// a sniper far and slow. Off the ring they arrive onto it, braking in
	// time; far off they head for where the player is going.
	struct Tactics
	{
		float range;                 // the radius it circles at
		float orbitSpeed;            // how fast it goes round, units per second
		float lead;                  // the most it leads the player by, seconds
	};
	Tactics rusherTactics = {550.f, 700.f, 1.f};
	Tactics sniperTactics = {1900.f, 350.f, 1.f};
	steering::Params steer;          // the controller's response and braking share

	void rollFlight(Enemy &e, const Flight &f)
	{
		e.body.mass = f.mass;
		e.body.move = movement::momentum(f.thrust, f.drag);
		e.body.move.maxSpeed = randomBetween(f.speedMin, f.speedMax);
		e.body.turnRate = randomBetween(f.turnMin, f.turnMax);
	}

	bool canSee(const Enemy &enemy, glm::vec2 playerPos, bool playerHidden)
	{
		if (playerHidden) { return false; }
		const glm::vec2 toPlayer = playerPos - enemy.body.position;
		const float distance = glm::length(toPlayer);
		if (distance <= hearingRadius) { return true; }
		if (distance > enemy.sightRange) { return false; }
		const float cosine = glm::dot(toPlayer / distance, enemy.body.facing);
		return cosine >= std::cos(enemy.sightHalfAngle);
	}

	// Each policy below only decides an intent -- where to face, where to
	// thrust. `update` steps the body with it, the same step as the player's.

	// Lost the player: fly to where it was last seen, then turn slowly to scan
	// for it. The sweep is visible, because the cone turns with the nose.
	movement::Intent search(Enemy &enemy, float dt)
	{
		movement::Intent intent;
		const glm::vec2 toLast = enemy.lastKnown - enemy.body.position;
		const float distance = glm::length(toLast);
		if (distance > arriveDistance)
		{
			intent.face = toLast;
			intent.thrust = steering::arrive(enemy.body, enemy.lastKnown, {}, steer);
			return intent;
		}

		// There: hold the spot -- arriving brakes to it, rather than coasting
		// past -- and turn slowly to scan.
		const float sweep = (enemy.id % 2u) ? scanRate : -scanRate;
		intent.face = rotated(enemy.body.facing, sweep * dt);
		intent.thrust = steering::arrive(enemy.body, enemy.lastKnown, {}, steer);
		enemy.searchLeft -= dt;
		if (enemy.searchLeft <= 0.f) { enemy.awareness = Enemy::Awareness::Unaware; }
		return intent;
	}

	// Nothing to go on: drift in slow curves at part thrust, changing the curve
	// every couple of seconds. Patrols replace this once levels give places.
	movement::Intent wander(Enemy &enemy, float dt)
	{
		enemy.wanderTimer -= dt;
		if (enemy.wanderTimer <= 0.f)
		{
			enemy.wanderTurn = randomBetween(-0.8f, 0.8f);
			enemy.wanderTimer = randomBetween(1.5f, 3.f);
		}
		// At part of its top speed along its nose: a velocity to hold, not a
		// thrust -- part thrust against a little drag still reaches the cap.
		movement::Intent intent;
		intent.face = rotated(enemy.body.facing, enemy.wanderTurn * dt);
		intent.thrust = steering::matchVelocity(enemy.body,
			intent.face * (wanderSpeedFraction * movement::topSpeed(enemy.body.move)), steer);
		return intent;
	}

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
		return glm::length(directionToPlayer + enemy.body.facing) >= enemy.fireRange;
	}

	// Close in and circle tight, fast, facing the player -- guns on it all
	// the way round. It used to fly where it looked, straight at the player,
	// which with momentum overshoots: it passed through and had to come
	// round again. Which way round is the id's.
	movement::Intent closeIn(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity)
	{
		movement::Intent intent;
		intent.face = towardPlayer(enemy.body.position, playerPos, nullptr);
		intent.thrust = steering::orbit(enemy.body, playerPos, playerVelocity,
			rusherTactics.range, rusherTactics.orbitSpeed, enemy.id % 2u == 0u, rusherTactics.lead, steer);
		return intent;
	}

	// Hang back and circle wide, slowly, facing the player: back off when
	// too close, close in when too far, round in between -- all one orbit,
	// braking onto the ring instead of swinging through it. Which way round is
	// the sheet row's, as before (type.y).
	movement::Intent keepDistance(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity)
	{
		movement::Intent intent;
		intent.face = towardPlayer(enemy.body.position, playerPos, nullptr);
		intent.thrust = steering::orbit(enemy.body, playerPos, playerVelocity,
			sniperTactics.range, sniperTactics.orbitSpeed, enemy.type.y % 2u == 0u, sniperTactics.lead, steer);
		return intent;
	}

	void rollLoadout(Enemy &e)
	{
		if (e.behaviour == Enemy::Behaviour::KeepDistance)
		{
			// Column 2 of the sheet, so they read as a different ship. Two rows
			// so they do not all orbit the same way (keepDistance uses type.y).
			e.type = (rand() % 2) ? glm::uvec2{2, 0} : glm::uvec2{2, 1};
			rollFlight(e, sniperFlight);
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
		rollFlight(e, rusherFlight);
		e.fireRange = 1.5f + (rand() % 1000) / 2000.f;
		e.fireTimeReset = 0.1f + (rand() % 1000) / 500.f; // 0.1 .. 2.1 s
		e.bulletSpeed = 1000 + rand() % 1600;            // 1000 .. 2600
	}
}

void stun(Enemy &enemy, glm::vec2 impulse, float seconds)
{
	enemy.stunned = seconds;
	movement::push(enemy.body, impulse);
	// A fast tumble either way, so two rammed ships do not turn in step.
	const float turns = 6.f + (rand() % 1000) / 200.f; // 6 .. 11 rad/s
	enemy.spinRate = (rand() % 2) ? turns : -turns;
}

void alert(Enemy &enemy, glm::vec2 playerPos)
{
	enemy.awareness = Enemy::Awareness::Engaged;
	enemy.lastKnown = playerPos;
	const glm::vec2 toPlayer = playerPos - enemy.body.position;
	const float distance = glm::length(toPlayer);
	if (distance > 0.001f) { enemy.body.facing = toPlayer / distance; }
}

bool showCones() { return conesVisible; }

bool update(Enemy &enemy, float gameDeltaTime, glm::vec2 playerPos, glm::vec2 playerVelocity,
	bool playerHidden, const glm::vec2 *comeBackTo)
{
	if (enemy.stunned > 0.f)
	{
		// Disabled: tumbling on the blow -- no thrust, no cap, the tumble's
		// own quick drag. Integrated directly rather than stepped: the body
		// keeps its own options for when it recovers, and the spin is not a
		// turn toward anything.
		enemy.stunned -= gameDeltaTime;
		enemy.body.facing = glm::normalize(rotated(enemy.body.facing, enemy.spinRate * gameDeltaTime));
		enemy.body.thrust = {};
		movement::integrate(enemy.body.position, enemy.body.velocity, {},
			movement::momentum(0.f, stunDrag), gameDeltaTime);
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

	movement::Intent intent;
	bool fighting = false;
	if (comeBackTo && enemy.awareness != Enemy::Awareness::Engaged)
	{
		// Flying back into the closing circle, straight there at full thrust,
		// still looking.
		enemy.awareness = Enemy::Awareness::Unaware;
		intent.face = *comeBackTo - enemy.body.position;
		intent.forward = 1.f;
	}
	else
	{
		switch (enemy.awareness)
		{
		case Enemy::Awareness::Searching: intent = search(enemy, gameDeltaTime); break;
		case Enemy::Awareness::Unaware: intent = wander(enemy, gameDeltaTime); break;
		case Enemy::Awareness::Engaged:
			fighting = true;
			intent = enemy.behaviour == Enemy::Behaviour::KeepDistance
				? keepDistance(enemy, playerPos, playerVelocity)
				: closeIn(enemy, playerPos, playerVelocity);
			break;
		}
	}

	// One step for every ship, the player's included.
	movement::step(enemy.body, intent, gameDeltaTime);

	// Only a fighting enemy fires, once it has turned far enough to be
	// aligned -- judged on the facing it has after this step's turn.
	if (!fighting) { return false; }
	return tickGun(enemy, gameDeltaTime, alignedTo(enemy, towardPlayer(enemy.body.position, playerPos, nullptr)));
}

Enemy spawnAt(glm::vec2 position, glm::vec2 facing, Enemy::Behaviour behaviour)
{
	static unsigned int nextId = 1; // 0 means "no enemy"

	Enemy e;
	e.id = nextId++;
	e.behaviour = behaviour;
	e.body.position = position;
	e.body.facing = facing;
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
	ImGui::SliderFloat("Wander thrust", &wanderSpeedFraction, 0.f, 1.f, "%.2f");

	// P1: how each class flies. Rolled at spawn.
	auto flightUi = [](const char *name, Flight &f)
	{
		if (!ImGui::TreeNode(name)) { return; }
		ImGui::TextDisabled("New spawns; Reset game respawns a level's");
		ImGui::SliderFloat("Mass", &f.mass, 0.1f, 5.f, "%.2f (the player is 1)");
		ImGui::SliderFloat("Thrust", &f.thrust, 500.f, 30000.f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Falloff", &f.drag, 0.f, 3.f, "%.2f");
		ImGui::DragFloatRange2("Top speed", &f.speedMin, &f.speedMax, 10.f, 100.f, 6000.f, "%.0f");
		ImGui::DragFloatRange2("Turn rate", &f.turnMin, &f.turnMax, 0.05f, 0.1f, 15.f, "%.1f rad/s");
		ImGui::TreePop();
	};
	flightUi("Rusher flight", rusherFlight);
	flightUi("Sniper flight", sniperFlight);

	// P1 step 2: how they fight with it. Live, for every enemy.
	auto tacticsUi = [](const char *name, Tactics &t)
	{
		if (!ImGui::TreeNode(name)) { return; }
		ImGui::SliderFloat("Range", &t.range, 100.f, 4000.f, "%.0f");
		ImGui::SliderFloat("Orbit speed", &t.orbitSpeed, 0.f, 2000.f, "%.0f");
		ImGui::SliderFloat("Lead", &t.lead, 0.f, 3.f, "%.2f s");
		ImGui::TreePop();
	};
	tacticsUi("Rusher tactics", rusherTactics);
	tacticsUi("Sniper tactics", sniperTactics);
	ImGui::SliderFloat("Stun drag", &stunDrag, 0.5f, 10.f, "%.1f /s (tumbling after a ram)");
	if (ImGui::TreeNode("Steering"))
	{
		ImGui::SliderFloat("Response", &steer.responseTime, 0.02f, 2.f, "%.2f s");
		ImGui::SliderFloat("Brake share", &steer.brakeShare, 0.1f, 1.f, "%.2f of full thrust");
		ImGui::TreePop();
	}
}

}
