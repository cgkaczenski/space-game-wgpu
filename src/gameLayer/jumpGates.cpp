#include <jumpGates.h>
#include <tuning.h>

#include <gate.h>
#include <level.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace jumpGates
{

namespace
{
	enum class Transit { Instant, Short };

	Transit transit = Transit::Short;
	float transitSeconds = 0.6f;    // Short: the whole of it; the move comes half way
	float instantFlashSeconds = 0.2f;
	float restSeconds = 3.f;        // a pair, after a jump, before it can be used again
	float graceSeconds = 1.5f;      // coming out: not hit, not firing
	float followSeconds = 6.f;      // chasers reaching the gate in this long come through
	float gateRadius = 600.f;
	float exitClear = 400.f;        // past the far gate's rim the ship comes out
	float spinRate = 1.2f;          // radians per second
	glm::vec3 colour = {0.75f, 0.45f, 1.f};
	float swirl = 0.5f;             // of the extraction gate's swirl

	std::vector<level::JumpPair> pairs;
	std::vector<float> restLeft;    // per pair
	float spin = 0.f;

	// The player's jump under way.
	float transitLeft = 0.f;
	bool moved = false;
	glm::vec2 exitAt = {};
	float graceLeft = 0.f;
	float flashLeft = 0.f;          // Instant's flash

	// Where the player last went in, for chasers. Opened when the player is
	// moved, not when the jump starts, so a chaser always comes out after it,
	// into rocks the player's arrival has already made. Each jump has its own
	// number, and a chaser follows each jump once.
	glm::vec2 followFrom = {}, followTo = {};
	float followLeft = 0.f;
	unsigned jumpSerial = 0;
	glm::vec2 pendingFrom = {}, pendingTo = {}; // the jump under way, until the move

	void openFollow()
	{
		followFrom = pendingFrom;
		followTo = pendingTo;
		followLeft = followSeconds;
		jumpSerial++;
	}

	glm::vec2 gateAt(int g) { return g % 2 == 0 ? pairs[(size_t)(g / 2)].a : pairs[(size_t)(g / 2)].b; }
	glm::vec2 otherEnd(int g) { return gateAt(g ^ 1); }

	// Out of the far gate, past its rim, the way the ship was going.
	glm::vec2 placeOut(glm::vec2 far, glm::vec2 heading) { return far + heading * (gateRadius + exitClear); }

	glm::vec2 headingOf(const movement::Body &b)
	{
		const float speed = glm::length(b.velocity);
		return speed > 50.f ? b.velocity / speed : b.facing;
	}
}

void start(const std::vector<level::JumpPair> &p)
{
	pairs = p;
	restLeft.assign(pairs.size(), 0.f);
	transitLeft = 0.f;
	moved = false;
	graceLeft = 0.f;
	flashLeft = 0.f;
	followLeft = 0.f;
}

bool updatePlayer(movement::Body &ship, bool canUse, float dt)
{
	spin += spinRate * dt;
	for (float &r : restLeft) { r = std::max(0.f, r - dt); }
	followLeft = std::max(0.f, followLeft - dt);
	graceLeft = std::max(0.f, graceLeft - dt);
	flashLeft = std::max(0.f, flashLeft - dt);

	bool movedNow = false;
	if (transitLeft > 0.f)
	{
		transitLeft = std::max(0.f, transitLeft - dt);
		// At the whitest, half way, the move.
		if (!moved && transitLeft <= transitSeconds * 0.5f)
		{
			ship.position = exitAt;
			moved = true;
			movedNow = true;
			graceLeft = graceSeconds + transitLeft; // the grace starts at the far end, counted from the white clearing
			openFollow();
		}
		return movedNow;
	}

	if (!canUse) { return false; }
	for (int g = 0; g < (int)pairs.size() * 2; g++)
	{
		if (restLeft[(size_t)(g / 2)] > 0.f) { continue; }
		if (glm::distance(ship.position, gateAt(g)) > gateRadius) { continue; }
		const glm::vec2 heading = headingOf(ship);
		exitAt = placeOut(otherEnd(g), heading);
		restLeft[(size_t)(g / 2)] = restSeconds;
		pendingFrom = gateAt(g);
		pendingTo = otherEnd(g);
		if (transit == Transit::Instant)
		{
			ship.position = exitAt;
			openFollow();
			flashLeft = instantFlashSeconds;
			graceLeft = graceSeconds;
			return true;
		}
		transitLeft = transitSeconds;
		moved = false;
		return false;
	}
	return false;
}

bool updateEnemy(movement::Body &body, bool chasing, unsigned &followed)
{
	if (!chasing || followLeft <= 0.f || followed == jumpSerial) { return false; }
	if (glm::distance(body.position, followFrom) > gateRadius) { return false; }
	body.position = placeOut(followTo, headingOf(body));
	followed = jumpSerial; // through once: not again, however it comes back
	return true;
}

bool inTransit() { return transitLeft > 0.f; }
bool shielded() { return transitLeft > 0.f || graceLeft > 0.f; }

namespace
{
	// 0 at the transit's ends, 1 at its middle, where the move is.
	float transitPeak()
	{
		if (transitLeft <= 0.f || transitSeconds <= 0.f) { return 0.f; }
		const float t = 1.f - transitLeft / transitSeconds; // 0 .. 1
		return 1.f - std::abs(t * 2.f - 1.f);
	}
}

float whiteOut()
{
	if (flashLeft > 0.f) { return flashLeft / std::max(instantFlashSeconds, 0.01f); }
	const float p = transitPeak();
	return p * p; // white only near the middle
}

float warpBlur() { return transitPeak(); }
float stretch() { return 1.f + 2.f * transitPeak(); }

int count() { return (int)pairs.size() * 2; }
glm::vec2 position(int g) { return gateAt(g); }
bool resting(int g) { return restLeft[(size_t)(g / 2)] > 0.f; }
float radius() { return gateRadius; }

bool nearest(glm::vec2 from, float within, glm::vec2 &at)
{
	float best = within;
	bool found = false;
	for (int g = 0; g < count(); g++)
	{
		if (resting(g)) { continue; }
		const float d = glm::distance(from, gateAt(g));
		if (d < best) { best = d; at = gateAt(g); found = true; }
	}
	return found;
}

float swirlStrength() { return swirl; }
float swirlRadius() { return gateRadius * 2.2f; }

void drawBodies(wgpu2d::Renderer2D &renderer)
{
	const glm::vec4 view = renderer.getViewRect();
	for (int g = 0; g < count(); g++)
	{
		const glm::vec2 at = gateAt(g);
		const float reach = gateRadius * 1.5f;
		if (at.x + reach < view.x || at.y + reach < view.y || at.x - reach > view.x + view.z
			|| at.y - reach > view.y + view.w) { continue; }
		const float light = resting(g) ? 0.4f : 1.f;
		// Each end of a pair turns the other way.
		gate::drawHole(renderer, at, gateRadius, (g % 2 == 0 ? spin : -spin),
			{colour * light + glm::vec3(0.35f) * (1.f - light), 1.f});
	}
}

void drawRings(wgpu2d::Renderer2D &renderer, float zoom)
{
	const float px = 1.f / std::max(zoom, 0.01f);
	const glm::vec4 view = renderer.getViewRect();
	for (int g = 0; g < count(); g++)
	{
		const glm::vec2 at = gateAt(g);
		if (at.x + gateRadius * 2.f < view.x || at.y + gateRadius * 2.f < view.y
			|| at.x - gateRadius * 2.f > view.x + view.z || at.y - gateRadius * 2.f > view.y + view.w) { continue; }
		const float bright = resting(g) ? 0.35f : 0.9f + 0.2f * std::sin(spin * 3.f);
		const glm::vec3 line = colour * bright;
		renderer.renderCircleOutline(at, {line * 0.4f, 1.f}, gateRadius, 14.f * px, 64);
		renderer.renderCircleOutline(at, {line, 1.f}, gateRadius, 3.f * px, 64);
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("jumpGates", {
	{"transit", transit},
	{"transitSeconds", transitSeconds},
	{"instantFlashSeconds", instantFlashSeconds},
	{"restSeconds", restSeconds},
	{"graceSeconds", graceSeconds},
	{"followSeconds", followSeconds},
	{"gateRadius", gateRadius},
	{"exitClear", exitClear},
	{"spinRate", spinRate},
	{"colour", colour},
	{"swirl", swirl},
});

void debugUi()
{
	ImGui::Text("%d pairs; %s, grace %.1f s, chasers %.1f s", (int)pairs.size(),
		transitLeft > 0.f ? "in transit" : "not jumping", graceLeft, followLeft);
	int t = (int)transit;
	{
		tune::Highlight h(&transit); // the radios edit a copy
		ImGui::RadioButton("Short transit", &t, (int)Transit::Short);
		ImGui::SameLine();
		ImGui::RadioButton("Instant", &t, (int)Transit::Instant);
	}
	transit = (Transit)t;
	if (transit == Transit::Short) { tune::SliderFloat("Transit", &transitSeconds, 0.1f, 3.f, "%.2f s, the move half way"); }
	else { tune::SliderFloat("Flash", &instantFlashSeconds, 0.f, 1.f, "%.2f s"); }
	tune::SliderFloat("Rest", &restSeconds, 0.f, 20.f, "%.1f s before a pair works again");
	tune::SliderFloat("Grace", &graceSeconds, 0.f, 5.f, "%.1f s coming out: not hit, not firing");
	tune::SliderFloat("Chasers follow", &followSeconds, 0.f, 20.f, "%.1f s to reach the gate");
	tune::SliderFloat("Radius", &gateRadius, 200.f, 2000.f, "%.0f");
	tune::SliderFloat("Out past the rim", &exitClear, 0.f, 2000.f, "%.0f");
	tune::ColorEdit3("Colour", &colour.x);
	tune::SliderFloat("Swirl", &swirl, 0.f, 1.f, "%.2f");
}

}
