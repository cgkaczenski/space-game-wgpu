#include <ram.h>
#include <tuning.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ram
{

namespace
{
	float windup = 0.1f;         // seconds of the dip before the surge
	float windupBack = 500.f;    // how fast the ship eases back during it
	float duration = 0.8f;       // seconds of surge
	float speed = 6000.f;        // against a normal top speed of 2000
	float cooldown = 8.f;        // seconds, from pressing Space
	float damage = 0.4f;         // an enemy has 1 life
	float stun = 2.f;            // seconds disabled
	float knockback = 2200.f;    // on top of the surge speed, fading
	float hitStop = 0.06f;       // seconds the game freezes on each strike
	float leanDistance = 220.f;  // world units the camera leans ahead
	float leanRate = 10.f;       // per second, easing in and out

	float windupLeft = 0.f;
	float activeLeft = 0.f;
	float cooldownLeft = 0.f;
	float lean = 0.f;            // 0 .. 1
	glm::vec2 heading = {1.f, 0.f};
	std::vector<unsigned int> struck;
}

void reset()
{
	windupLeft = 0.f;
	activeLeft = 0.f;
	cooldownLeft = 0.f;
	lean = 0.f;
	struck.clear();
}

bool tryStart(glm::vec2 direction)
{
	if (barrierUp() || cooldownLeft > 0.f) { return false; }
	heading = direction;
	windupLeft = windup;
	activeLeft = windup > 0.f ? 0.f : duration;
	cooldownLeft = cooldown;
	struck.clear();
	return true;
}

void update(float gameDeltaTime, glm::vec2 aim)
{
	cooldownLeft = std::max(0.f, cooldownLeft - gameDeltaTime);

	if (windupLeft > 0.f)
	{
		heading = aim; // still lining up: the dip and the prow follow the mouse
		windupLeft -= gameDeltaTime;
		if (windupLeft <= 0.f)
		{
			// Whatever of this frame was left after the wind-up is surge.
			activeLeft = duration + windupLeft;
			windupLeft = 0.f;
		}
	}
	else
	{
		activeLeft = std::max(0.f, activeLeft - gameDeltaTime);
	}

	const float target = barrierUp() ? 1.f : 0.f;
	lean += (target - lean) * (1.f - std::exp(-leanRate * gameDeltaTime));
}

bool windingUp() { return windupLeft > 0.f; }
bool active() { return activeLeft > 0.f; }
bool barrierUp() { return windingUp() || active(); }

float barrierLevel()
{
	if (windingUp()) { return windup > 0.f ? 1.f - windupLeft / windup : 1.f; }
	return active() ? 1.f : 0.f;
}

glm::vec2 direction() { return heading; }
float surgeSpeed() { return speed; }
float windupBackSpeed() { return windupBack; }
float hitDamage() { return damage; }
float stunSeconds() { return stun; }
float knockbackSpeed() { return knockback; }
float hitStopSeconds() { return hitStop; }
glm::vec2 cameraLean() { return heading * (leanDistance * lean); }

bool firstHit(unsigned int enemyId)
{
	if (std::find(struck.begin(), struck.end(), enemyId) != struck.end()) { return false; }
	struck.push_back(enemyId);
	return true;
}

float ready()
{
	return cooldown > 0.f ? 1.f - cooldownLeft / cooldown : 1.f;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("ram", {
	{"windup", windup},
	{"duration", duration},
	{"speed", speed},
	{"cooldown", cooldown},
	{"damage", damage},
	{"stun", stun},
	{"knockback", knockback},
	{"hitStop", hitStop},
	{"leanDistance", leanDistance},
});

void debugUi()
{
	ImGui::Text(windingUp() ? "winding up" : active() ? "ramming"
		: (cooldownLeft > 0.f ? "cooling down" : "ready (Space)"));
	tune::SliderFloat("Wind-up", &windup, 0.f, 0.5f, "%.2f s");
	tune::SliderFloat("Surge time", &duration, 0.1f, 2.f, "%.2f s");
	tune::SliderFloat("Surge speed", &speed, 1000.f, 12000.f, "%.0f");
	if (tune::SliderFloat("Cooldown", &cooldown, 0.f, 20.f, "%.1f s"))
	{
		// The leftover wait was started from the old value; keep it from
		// outlasting a shorter setting, and a 0 cooldown is ready now.
		cooldownLeft = std::min(cooldownLeft, cooldown);
	}
	tune::SliderFloat("Damage", &damage, 0.f, 2.f, "%.2f");
	tune::SliderFloat("Stun", &stun, 0.f, 6.f, "%.1f s");
	tune::SliderFloat("Knockback", &knockback, 0.f, 8000.f, "%.0f");
	tune::SliderFloat("Hit-stop", &hitStop, 0.f, 0.3f, "%.3f s");
	tune::SliderFloat("Camera lean", &leanDistance, 0.f, 800.f, "%.0f");
}

}
