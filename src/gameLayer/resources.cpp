#include <resources.h>
#include <inventory.h>
#include <tuning.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace resources
{

namespace
{
	// A soft radial falloff for the orbs' glow. Built rather than loaded, like
	// the fireball's in effects.cpp: it is one gradient, and a file for it
	// would be a file to keep in step.
	wgpu2d::Texture glow;

	// What the beam knocks loose, and what a dead enemy leaves. An orb is
	// thrown clear, hangs where it stopped, and only comes for the ship once
	// the ship is near enough -- then it chases the way a missile does,
	// turning harder and running faster the longer it has been chasing. Ore
	// left behind stays left behind, which is what makes coming back for it a
	// decision.
	struct Orb
	{
		glm::vec2 position = {};
		glm::vec2 velocity = {};
		float value = 0.f;      // ore, as the rock counts it: how big it looks
		int units = 1;          // what it is in the hold (I1)
		float pauseLeft = 0.f;  // thrown clear, motor off
		bool homing = false;
		bool dumped = false;    // thrown out: waits until the ship has left it once
		float chase = 0.f;      // seconds of chase, for the turn's growth
		float phase = 0.f;      // its own, so a cloud does not pulse as one
	};

	std::vector<Orb> orbs;

	int points = 0;         // banked by extracting; not a round's, so reset leaves it

	// One unit in the hold: an orb as the beam sheds them (asteroids'
	// orbValue). A fragment rounds up to 1, a boss orb is 2, a spilled orb 4.
	const float orePerUnit = 0.25f;
	int unitsOf(float value) { return std::max(1, (int)std::lround(value / orePerUnit)); }
	float interruptLeft = 0.f;

	// Tuning.
	float interruptSeconds = 0.6f;
	float spillOrbValue = 1.f;     // a dead player's hold scatters in orbs of this
	float enemyFragmentValue = 0.15f;
	int enemyFragmentCount = 2;

	// The orbs.
	float orbValue = 0.25f;           // ore per orb: the beam emits one per this much
	float orbEjectSpeed = 900.f;      // thrown clear of the rock
	float orbPauseSeconds = 0.45f;    // hanging still before it looks for the ship
	float orbWakeRadius = 2600.f;     // nearer than this, it comes
	float orbAccel = 3500.f;          // per second squared, once chasing
	float orbMaxSpeed = 4200.f;
	float orbTurnStart = 3.5f;        // radians per second at the start of a chase
	float orbTurnGrowth = 5.f;        // added per second of chase
	float orbPickupRadius = 260.f;
	int orbLimit = 800;               // past this the oldest go, like the wreck field

	float randomBetween(float a, float b)
	{
		return a + (b - a) * ((float)std::rand() / (float)RAND_MAX);
	}

	bool buildGlowTexture()
	{
		const int size = 64;
		std::vector<unsigned char> pixels((size_t)size * size * 4);
		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				const float dx = (x + 0.5f) / size * 2.f - 1.f;
				const float dy = (y + 0.5f) / size * 2.f - 1.f;
				const float r = std::min(1.f, std::sqrt(dx * dx + dy * dy));
				const float a = (1.f - r) * (1.f - r);
				unsigned char *p = pixels.data() + ((size_t)y * size + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(a * 255.f);
			}
		}
		glow.createFromBuffer((const char *)pixels.data(), size, size, false, true);
		return glow.id != 0;
	}

	void emit(glm::vec2 at, glm::vec2 direction, float value)
	{
		if ((int)orbs.size() >= orbLimit) { orbs.erase(orbs.begin()); }
		Orb o;
		o.position = at;
		o.velocity = direction * (orbEjectSpeed * randomBetween(0.7f, 1.3f));
		o.value = value;
		o.units = unitsOf(value);
		o.pauseLeft = orbPauseSeconds * randomBetween(0.8f, 1.4f);
		o.phase = randomBetween(0.f, 6.28f);
		orbs.push_back(o);
	}

}

bool init()
{
	return buildGlowTexture();
}

void cleanup()
{
	glow.cleanup();
}

void reset()
{
	orbs.clear();
	interruptLeft = 0.f;
}

void update(float dt, glm::vec2 playerPos, bool playerPresent)
{
	interruptLeft = std::max(0.f, interruptLeft - dt);

	for (int i = 0; i < (int)orbs.size(); i++)
	{
		Orb &o = orbs[i];

		// Thrown clear, motor off: it slides out of the rock and stops.
		if (o.pauseLeft > 0.f)
		{
			o.pauseLeft -= dt;
			o.position += o.velocity * dt;
			o.velocity *= std::exp(-6.f * dt);
			continue;
		}

		const float distance = playerPresent ? glm::distance(o.position, playerPos) : 1e30f;
		const bool room = inventory::oreRoom() > 0;

		// Thrown out of the hold: it waits for the ship to leave it first.
		if (o.dumped && distance > orbWakeRadius) { o.dumped = false; }

		if (!o.homing)
		{
			// It waits exactly where it stopped. Nothing reaches out for it;
			// the ship has to come -- with room in the hold (I1).
			o.velocity = {};
			if (distance <= orbWakeRadius && room && !o.dumped) { o.homing = true; }
			continue;
		}

		if (!playerPresent || !room) { o.homing = false; continue; } // nothing to chase, or nowhere to go

		if (distance <= orbPickupRadius)
		{
			// As much as fits. What does not stays an orb, waiting.
			o.units -= inventory::addOre(o.units);
			if (o.units <= 0)
			{
				orbs.erase(orbs.begin() + i);
				i--;
			}
			else { o.homing = false; }
			continue;
		}

		// The missile's chase (C3a), smaller: it turns harder the longer it
		// has been after the ship, and keeps gaining speed, so an orb that
		// starts off badly aimed still curves in and arrives quickly.
		o.chase += dt;
		const glm::vec2 wanted = (playerPos - o.position) / std::max(distance, 0.001f);

		float speed = glm::length(o.velocity);
		glm::vec2 heading = speed > 1.f ? o.velocity / speed : wanted;
		speed = std::min(orbMaxSpeed, std::max(speed, 120.f) + orbAccel * dt);

		const float turn = (orbTurnStart + orbTurnGrowth * o.chase) * dt;
		const float toWanted = std::atan2(
			heading.x * wanted.y - heading.y * wanted.x, glm::dot(heading, wanted));
		const float turned = std::clamp(toWanted, -turn, turn);
		const float c = std::cos(turned), s = std::sin(turned);
		heading = {heading.x * c - heading.y * s, heading.x * s + heading.y * c};

		o.velocity = heading * speed;
		o.position += o.velocity * dt;
	}
}

void interrupt() { interruptLeft = interruptSeconds; }
bool interrupted() { return interruptLeft > 0.f; }

void emitOrb(glm::vec2 at, glm::vec2 direction, float value) { emit(at, direction, value); }

void enemyDropped(glm::vec2 position)
{
	for (int i = 0; i < enemyFragmentCount; i++)
	{
		const float angle = randomBetween(0.f, 6.2831853f);
		emit(position, {std::cos(angle), std::sin(angle)}, enemyFragmentValue);
	}
}

void playerDropped(glm::vec2 position)
{
	// What was carried scatters from the wreck, still ore. The round restarts
	// on death, so this lasts only as long as the wreck is watched -- it was a
	// deposit, until asteroids replaced deposits (A4).
	const int spillUnits = unitsOf(spillOrbValue);
	for (int left = inventory::takeOre(); left > 0; left -= spillUnits)
	{
		const float angle = randomBetween(0.f, 6.2831853f);
		emit(position, {std::cos(angle), std::sin(angle)}, std::min(spillUnits, left) * orePerUnit);
	}
}

void jettison(glm::vec2 at, glm::vec2 direction, int units)
{
	const int perOrb = unitsOf(spillOrbValue);
	for (int left = units; left > 0; left -= perOrb)
	{
		// Fanned a little about the throw, so a stack does not land as one dot.
		const float turn = randomBetween(-0.5f, 0.5f);
		const glm::vec2 d = {direction.x * std::cos(turn) - direction.y * std::sin(turn),
			direction.x * std::sin(turn) + direction.y * std::cos(turn)};
		emit(at, d, std::min(perOrb, left) * orePerUnit);
		orbs.back().dumped = true;
	}
}

void extracted()
{
	points += inventory::takeOre();
}

int held() { return inventory::ore(); }
int banked() { return points; }

bool spend(int n)
{
	if (n < 0 || n > points) { return false; }
	points -= n;
	return true;
}

void earn(int n) { points += std::max(n, 0); }

void drawGlow(wgpu2d::Renderer2D &renderer, float time)
{
	for (const Orb &o : orbs)
	{
		// Waiting, it breathes slowly; chasing, it runs bright -- the speed
		// reads from the path it takes.
		const float breathe = 0.75f + 0.25f * std::sin(time * 3.f + o.phase);
		const float strength = (o.homing ? 1.1f : 0.7f) * breathe;
		const float size = 200.f + 120.f * std::min(1.f, o.value / std::max(orbValue, 0.01f));
		renderer.renderRectangle({o.position - glm::vec2(size * 0.5f), glm::vec2(size)},
			glow, glm::vec4(1.f, 0.85f, 0.35f, 1.f) * strength);
		// A small white heart, so an orb reads as a thing and not a smudge.
		renderer.renderRectangle({o.position - glm::vec2(size * 0.16f), glm::vec2(size * 0.32f)},
			glow, glm::vec4(1.f, 1.f, 0.9f, 1.f) * strength);
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("resources", {
	{"interruptSeconds", interruptSeconds},
	{"spillOrbValue", spillOrbValue},
	{"enemyFragmentValue", enemyFragmentValue},
	{"enemyFragmentCount", enemyFragmentCount},
	{"orbValue", orbValue},
	{"orbEjectSpeed", orbEjectSpeed},
	{"orbPauseSeconds", orbPauseSeconds},
	{"orbWakeRadius", orbWakeRadius},
	{"orbAccel", orbAccel},
	{"orbMaxSpeed", orbMaxSpeed},
	{"orbTurnStart", orbTurnStart},
	{"orbTurnGrowth", orbTurnGrowth},
	{"orbPickupRadius", orbPickupRadius},
});

void debugUi()
{
	ImGui::Text("Hold %d   banked %d (orbs; the hold is under Inventory)", inventory::ore(), points);
	int waiting = 0, chasing = 0;
	for (const Orb &o : orbs) { (o.homing ? chasing : waiting)++; }
	ImGui::Text("%d orbs (%d waiting, %d chasing)", (int)orbs.size(), waiting, chasing);
	if (interruptLeft > 0.f) { ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "Mining interrupted"); }
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear points")) { points = 0; }

	tune::SliderFloat("Interrupt s", &interruptSeconds, 0.f, 3.f);
	tune::SliderFloat("Spill orb value", &spillOrbValue, 0.1f, 5.f);
	tune::SliderFloat("Fragment value", &enemyFragmentValue, 0.f, 2.f);
	tune::SliderInt("Fragments per kill", &enemyFragmentCount, 0, 8);

	ImGui::SeparatorText("Orbs");
	tune::SliderFloat("Ore per orb", &orbValue, 0.05f, 4.f);
	tune::SliderFloat("Eject speed", &orbEjectSpeed, 0.f, 3000.f, "%.0f");
	tune::SliderFloat("Pause", &orbPauseSeconds, 0.f, 3.f, "%.2f s");
	tune::SliderFloat("Wake radius", &orbWakeRadius, 200.f, 8000.f, "%.0f");
	tune::SliderFloat("Chase accel", &orbAccel, 200.f, 15000.f, "%.0f");
	tune::SliderFloat("Chase top speed", &orbMaxSpeed, 500.f, 12000.f, "%.0f");
	tune::SliderFloat("Turn start", &orbTurnStart, 0.5f, 12.f, "%.1f rad/s");
	tune::SliderFloat("Turn growth", &orbTurnGrowth, 0.f, 20.f, "%.1f rad/s per s");
	tune::SliderFloat("Pickup radius", &orbPickupRadius, 60.f, 2000.f, "%.0f");
}

}
