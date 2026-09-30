#include <resources.h>

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
		float value = 0.f;
		float pauseLeft = 0.f;  // thrown clear, motor off
		bool homing = false;
		float chase = 0.f;      // seconds of chase, for the turn's growth
		float phase = 0.f;      // its own, so a cloud does not pulse as one
	};

	std::vector<Orb> orbs;

	float hold = 0.f;
	float points = 0.f;     // banked by extracting; not a round's, so reset leaves it
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
	hold = 0.f;
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

		if (!o.homing)
		{
			// It waits exactly where it stopped. Nothing reaches out for it;
			// the ship has to come.
			o.velocity = {};
			if (distance <= orbWakeRadius) { o.homing = true; }
			continue;
		}

		if (!playerPresent) { o.homing = false; continue; } // nothing to chase

		if (distance <= orbPickupRadius)
		{
			hold += o.value;
			orbs.erase(orbs.begin() + i);
			i--;
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
	if (hold <= 0.f) { return; }

	// What was carried scatters from the wreck, still ore. The round restarts
	// on death, so this lasts only as long as the wreck is watched -- it was a
	// deposit, until asteroids replaced deposits (A4).
	for (float left = hold; left > 0.001f; left -= spillOrbValue)
	{
		const float angle = randomBetween(0.f, 6.2831853f);
		emit(position, {std::cos(angle), std::sin(angle)}, std::min(spillOrbValue, left));
	}
	hold = 0.f;
}

void extracted()
{
	points += hold;
	hold = 0.f;
}

float held() { return hold; }
float banked() { return points; }

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

void debugUi()
{
	ImGui::Text("Hold %.2f   banked %.2f", hold, points);
	int waiting = 0, chasing = 0;
	for (const Orb &o : orbs) { (o.homing ? chasing : waiting)++; }
	ImGui::Text("%d orbs (%d waiting, %d chasing)", (int)orbs.size(), waiting, chasing);
	if (interruptLeft > 0.f) { ImGui::TextColored({1.f, 0.5f, 0.3f, 1.f}, "Mining interrupted"); }
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear points")) { points = 0.f; }

	ImGui::SliderFloat("Interrupt s", &interruptSeconds, 0.f, 3.f);
	ImGui::SliderFloat("Spill orb value", &spillOrbValue, 0.1f, 5.f);
	ImGui::SliderFloat("Fragment value", &enemyFragmentValue, 0.f, 2.f);
	ImGui::SliderInt("Fragments per kill", &enemyFragmentCount, 0, 8);

	ImGui::SeparatorText("Orbs");
	ImGui::SliderFloat("Ore per orb", &orbValue, 0.05f, 4.f);
	ImGui::SliderFloat("Eject speed", &orbEjectSpeed, 0.f, 3000.f, "%.0f");
	ImGui::SliderFloat("Pause", &orbPauseSeconds, 0.f, 3.f, "%.2f s");
	ImGui::SliderFloat("Wake radius", &orbWakeRadius, 200.f, 8000.f, "%.0f");
	ImGui::SliderFloat("Chase accel", &orbAccel, 200.f, 15000.f, "%.0f");
	ImGui::SliderFloat("Chase top speed", &orbMaxSpeed, 500.f, 12000.f, "%.0f");
	ImGui::SliderFloat("Turn start", &orbTurnStart, 0.5f, 12.f, "%.1f rad/s");
	ImGui::SliderFloat("Turn growth", &orbTurnGrowth, 0.f, 20.f, "%.1f rad/s per s");
	ImGui::SliderFloat("Pickup radius", &orbPickupRadius, 60.f, 2000.f, "%.0f");
}

}
