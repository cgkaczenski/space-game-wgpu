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
	float stun = 2.f;            // seconds an enemy is disabled
	// The player, struck by an enemy's ram (B1), is disabled only briefly:
	// two seconds out of control is a long time to lose to one hit.
	float playerStun = 0.5f;
	float knockback = 2200.f;    // on top of the surge speed, fading
	float hitStop = 0.06f;       // seconds the game freezes on each strike
	float leanDistance = 220.f;  // world units the camera leans ahead
	float leanRate = 10.f;       // per second, easing in and out

	unsigned lastSerial = 0;     // rams started, by every ship
}

bool tryStart(Ram &r, glm::vec2 direction)
{
	if (barrierUp(r) || r.cooldownLeft > 0.f) { return false; }
	r.heading = direction;
	r.windupLeft = windup;
	r.activeLeft = windup > 0.f ? 0.f : duration;
	r.cooldownLeft = cooldown;
	r.struck.clear();
	r.serial = ++lastSerial;
	return true;
}

void stop(Ram &r)
{
	r.windupLeft = 0.f;
	r.activeLeft = 0.f;
}

void update(Ram &r, float gameDeltaTime, glm::vec2 aim)
{
	r.cooldownLeft = std::max(0.f, r.cooldownLeft - gameDeltaTime);

	if (r.windupLeft > 0.f)
	{
		r.heading = aim; // still lining up: the dip and the prow follow the aim
		r.windupLeft -= gameDeltaTime;
		if (r.windupLeft <= 0.f)
		{
			// Whatever of this frame was left after the wind-up is surge.
			r.activeLeft = duration + r.windupLeft;
			r.windupLeft = 0.f;
		}
	}
	else
	{
		r.activeLeft = std::max(0.f, r.activeLeft - gameDeltaTime);
	}

	const float target = barrierUp(r) ? 1.f : 0.f;
	r.lean += (target - r.lean) * (1.f - std::exp(-leanRate * gameDeltaTime));
}

bool windingUp(const Ram &r) { return r.windupLeft > 0.f; }
bool active(const Ram &r) { return r.activeLeft > 0.f; }
bool barrierUp(const Ram &r) { return windingUp(r) || active(r); }

float barrierLevel(const Ram &r)
{
	if (windingUp(r)) { return windup > 0.f ? 1.f - r.windupLeft / windup : 1.f; }
	return active(r) ? 1.f : 0.f;
}

glm::vec2 direction(const Ram &r) { return r.heading; }
float surgeSpeed() { return speed; }
float windupBackSpeed() { return windupBack; }
float hitDamage() { return damage; }
float stunSeconds() { return stun; }
float playerStunSeconds() { return playerStun; }
float knockbackSpeed() { return knockback; }
float hitStopSeconds() { return hitStop; }
glm::vec2 cameraLean(const Ram &r) { return r.heading * (leanDistance * r.lean); }

bool firstHit(Ram &r, ShipId ship)
{
	if (std::find(r.struck.begin(), r.struck.end(), ship) != r.struck.end()) { return false; }
	r.struck.push_back(ship);
	return true;
}

float ready(const Ram &r)
{
	return cooldown > 0.f ? 1.f - r.cooldownLeft / cooldown : 1.f;
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
	{"playerStun", playerStun},
	{"knockback", knockback},
	{"hitStop", hitStop},
	{"leanDistance", leanDistance},
});

void debugUi(Ram &r)
{
	ImGui::Text(windingUp(r) ? "winding up" : active(r) ? "ramming"
		: (r.cooldownLeft > 0.f ? "cooling down" : "ready (Space)"));
	tune::SliderFloat("Wind-up", &windup, 0.f, 0.5f, "%.2f s");
	tune::SliderFloat("Surge time", &duration, 0.1f, 2.f, "%.2f s");
	tune::SliderFloat("Surge speed", &speed, 1000.f, 12000.f, "%.0f");
	if (tune::SliderFloat("Cooldown", &cooldown, 0.f, 20.f, "%.1f s"))
	{
		// The leftover wait was started from the old value; keep it from
		// outlasting a shorter setting, and a 0 cooldown is ready now.
		r.cooldownLeft = std::min(r.cooldownLeft, cooldown);
	}
	tune::SliderFloat("Damage", &damage, 0.f, 2.f, "%.2f");
	tune::SliderFloat("Stun", &stun, 0.f, 6.f, "%.1f s (an enemy)");
	tune::SliderFloat("Player stun", &playerStun, 0.f, 3.f, "%.2f s (rammed by an enemy)");
	tune::SliderFloat("Knockback", &knockback, 0.f, 8000.f, "%.0f");
	tune::SliderFloat("Hit-stop", &hitStop, 0.f, 0.3f, "%.3f s");
	tune::SliderFloat("Camera lean", &leanDistance, 0.f, 800.f, "%.0f");
}

}
