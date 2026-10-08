#include <gate.h>
#include <tuning.h>

#include "imgui.h"
#include <platformTools.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace gate
{

namespace
{
	bool present = false;
	glm::vec2 at = {};
	State current = State::Closed;

	float gateRadius = 600.f;    // matches the editor's marker
	float clearing = 2500.f;     // open space round it in any field (W1)
	float hoverSeconds = 3.f;    // inside, uncloaked, to start it
	float chargeSeconds = 30.f;  // then it spins up on its own

	float hovered = 0.f;         // seconds of the hover so far
	float charged = 0.f;         // seconds of the charge so far

	// The pulse. Open, a slow breath; charging, a rate climbing from
	// `chargeStartHz` to `chargeEndHz` -- the tempo is the countdown.
	float openHz = 0.3f;
	float chargeStartHz = 0.5f;
	float chargeEndHz = 6.f;
	float breathClock = 0.f;     // game seconds, for the open breath and the swirl
	float pulseNow = 0.f;

	float swirlReach = 2.2f;     // of the gate's radius

	// The body: Black Hole2.png, a black disc in a cyan ring. Its ring is
	// about 52 of its 64 pixels across, so this size puts the art's ring on
	// the gate's. Pixelated, like the scenery cut from the same set.
	wgpu2d::Texture body;
	float bodyPerRadius = 2.45f;
	float spin = 0.f;            // radians, accumulated
	float openSpin = 0.25f;      // radians per second, open
	float readySpin = 3.f;       // ... ready; charging climbs from open to this

	const float tau = 6.2831853f;
	const glm::vec3 closedGrey = {0.30f, 0.34f, 0.40f};
	const glm::vec3 openCyan = {0.30f, 0.90f, 1.00f};
	const glm::vec3 readyGold = {1.00f, 0.95f, 0.70f};

	// The charge's pulse. The rate climbs linearly, so its phase is the
	// integral: f0 t + (f1 - f0) t^2 / 2T -- as the closing ring's warning.
	float chargePulse()
	{
		const float span = std::max(chargeSeconds, 0.001f);
		const float t = std::min(charged, span);
		const float cycles = chargeStartHz * t + (chargeEndHz - chargeStartHz) * t * t / (2.f * span);
		return 0.5f + 0.5f * std::cos(tau * cycles);
	}

	// An arc of `fraction` of a turn round the gate, from the top, clockwise
	// on screen. Line segments: the renderer has no arc, and at this size
	// ninety-six of them are a circle.
	void drawArc(wgpu2d::Renderer2D &renderer, float r, float fraction, glm::vec4 colour, float width)
	{
		const int segments = std::max(1, (int)std::ceil(96.f * fraction));
		glm::vec2 last = at + glm::vec2(0.f, -r);
		for (int i = 1; i <= segments; i++)
		{
			const float a = tau * fraction * (float)i / (float)segments;
			const glm::vec2 next = at + glm::vec2(std::sin(a), -std::cos(a)) * r;
			renderer.renderLine(last, next, colour, width);
			last = next;
		}
	}
}

bool init()
{
	const char *path = RESOURCES_PATH "space/Black Hole2.png";
	body.loadFromFile(path, true);
	if (body.id == 0)
	{
		std::cerr << "gate: failed to load " << path << "\n";
		return false;
	}
	return true;
}

void cleanup() { body.cleanup(); }

void start(bool exists, glm::vec2 position)
{
	present = exists;
	at = position;
	current = State::Closed;
	hovered = 0.f;
	charged = 0.f;
	breathClock = 0.f;
	pulseNow = 0.f;
	spin = 0.f;
}

bool exists() { return present; }
glm::vec2 position() { return at; }
float radius() { return gateRadius; }
float clearingRadius() { return clearing; }
State state() { return current; }

bool update(float dt, bool open, bool enemiesCleared, glm::vec2 playerPos, bool playerCanUse)
{
	if (!present) { return false; }
	breathClock = std::fmod(breathClock + dt, 1000.f);

	if (current == State::Closed && open) { current = State::Open; }
	// Nobody left to stop you: no hover, no charge.
	if (enemiesCleared) { current = State::Ready; }

	const bool inside = playerCanUse && glm::distance(playerPos, at) <= gateRadius;
	bool leave = false;

	switch (current)
	{
	case State::Closed:
		pulseNow = 0.f;
		break;

	case State::Open:
		// The hover has to be unbroken: step out or cloak and it starts over.
		hovered = inside ? hovered + dt : 0.f;
		if (hovered >= hoverSeconds)
		{
			current = State::Charging;
			charged = 0.f;
			hovered = 0.f;
		}
		pulseNow = 0.5f + 0.5f * std::sin(tau * openHz * breathClock);
		break;

	case State::Charging:
		charged += dt;
		if (charged >= chargeSeconds) { current = State::Ready; }
		pulseNow = chargePulse();
		break;

	case State::Ready:
		leave = inside;
		pulseNow = 1.f;
		break;
	}

	// The body spins up with the gate: still while closed, turning slowly
	// open, faster as it charges, fastest ready.
	float rate = 0.f;
	if (current == State::Open) { rate = openSpin; }
	else if (current == State::Charging)
	{
		rate = openSpin + (readySpin - openSpin)
			* std::clamp(charged / std::max(chargeSeconds, 0.001f), 0.f, 1.f);
	}
	else if (current == State::Ready) { rate = readySpin; }
	spin = std::fmod(spin + rate * dt, tau);

	return leave;
}

void playerShot()
{
	hovered = 0.f;
	if (current == State::Charging || current == State::Ready)
	{
		current = State::Open;
		charged = 0.f;
	}
}

float pulse() { return present ? pulseNow : 0.f; }

glm::vec3 colour()
{
	switch (current)
	{
	case State::Open: return openCyan;
	case State::Charging:
		return glm::mix(openCyan, glm::vec3(1.f),
			std::clamp(charged / std::max(chargeSeconds, 0.001f), 0.f, 1.f));
	case State::Ready: return readyGold;
	default: return closedGrey;
	}
}

float swirlStrength()
{
	if (!present) { return 0.f; }
	switch (current)
	{
	case State::Open: return 0.25f + 0.05f * pulseNow;
	case State::Charging:
	{
		const float c = std::clamp(charged / std::max(chargeSeconds, 0.001f), 0.f, 1.f);
		return 0.3f + 0.5f * c + 0.1f * pulseNow;
	}
	case State::Ready: return 1.f;
	default: return 0.f;
	}
}

float swirlRadius() { return gateRadius * swirlReach; }

void drawBody(wgpu2d::Renderer2D &renderer)
{
	if (!present || body.id == 0) { return; }
	const float size = gateRadius * bodyPerRadius;
	// Dim while closed: the art's own cyan would otherwise say open.
	const float light = current == State::Closed ? 0.45f : 1.f;
	renderer.renderRectangle({at - glm::vec2(size * 0.5f), glm::vec2(size)}, body,
		{light, light, light, 1.f}, {}, glm::degrees(spin));
}

void drawHole(wgpu2d::Renderer2D &renderer, glm::vec2 where, float radius, float turn, glm::vec4 tint)
{
	if (body.id == 0) { return; }
	const float size = radius * bodyPerRadius;
	renderer.renderRectangle({where - glm::vec2(size * 0.5f), glm::vec2(size)}, body, tint, {}, glm::degrees(turn));
}

void draw(wgpu2d::Renderer2D &renderer, float zoom)
{
	if (!present) { return; }
	const float px = 1.f / std::max(zoom, 0.01f);

	float brightness = 0.5f;
	if (current == State::Open) { brightness = 0.6f + 0.4f * pulseNow; }
	else if (current == State::Charging) { brightness = 0.35f + 0.9f * pulseNow; }
	else if (current == State::Ready) { brightness = 1.2f; }

	// A ring over the art's own, in the state's colour, with a glow under it.
	const glm::vec3 line = colour() * brightness;
	renderer.renderCircleOutline(at, {line * 0.4f, 1.f}, gateRadius, 14.f * px, 64);
	renderer.renderCircleOutline(at, {line, 1.f}, gateRadius, 3.f * px, 64);

	// The arc: the hover's progress while open, the charge's while charging,
	// whole when ready.
	const float arcRadius = gateRadius * 1.15f;
	float fraction = 0.f;
	if (current == State::Open) { fraction = hovered / std::max(hoverSeconds, 0.001f); }
	else if (current == State::Charging) { fraction = charged / std::max(chargeSeconds, 0.001f); }
	else if (current == State::Ready) { fraction = 1.f; }
	if (fraction > 0.f)
	{
		drawArc(renderer, arcRadius, std::min(fraction, 1.f), {line, 1.f}, 5.f * px);
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("gate", {
	{"hoverSeconds", hoverSeconds},
	{"chargeSeconds", chargeSeconds},
	{"chargeStartHz", chargeStartHz},
	{"chargeEndHz", chargeEndHz},
	{"gateRadius", gateRadius},
	{"clearing", clearing},
	{"swirlReach", swirlReach},
	{"bodyPerRadius", bodyPerRadius},
	{"openSpin", openSpin},
	{"readySpin", readySpin},
});

void debugUi()
{
	tune::SliderFloat("Clearing", &clearing, 0.f, 8000.f, "%.0f units round it, in any field");
	ImGui::TextDisabled("  takes effect when the round restarts");
	if (!present)
	{
		ImGui::TextDisabled("This level has no gate");
		return;
	}
	static const char *names[] = {"Closed", "Open", "Charging", "Ready"};
	ImGui::Text("Gate: %s  hover %.1f s  charge %.1f s", names[(int)current], hovered, charged);
	if (ImGui::Button("Open now") && current == State::Closed) { current = State::Open; }
	ImGui::SameLine();
	if (ImGui::Button("Ready now")) { current = State::Ready; charged = chargeSeconds; }
	tune::SliderFloat("Hover to start", &hoverSeconds, 0.f, 10.f, "%.1f s");
	tune::SliderFloat("Charge", &chargeSeconds, 1.f, 120.f, "%.0f s");
	tune::SliderFloat("Charge pulse from", &chargeStartHz, 0.1f, 5.f, "%.1f Hz");
	tune::SliderFloat("Charge pulse to", &chargeEndHz, 1.f, 15.f, "%.1f Hz");
	tune::SliderFloat("Gate radius", &gateRadius, 100.f, 2000.f, "%.0f");
	tune::SliderFloat("Swirl reach", &swirlReach, 1.f, 5.f, "x%.1f radius");
	tune::SliderFloat("Art size", &bodyPerRadius, 1.f, 4.f, "x%.2f radius");
	tune::SliderFloat("Spin open", &openSpin, 0.f, 3.f, "%.2f rad/s");
	tune::SliderFloat("Spin ready", &readySpin, 0.f, 10.f, "%.2f rad/s");
}

}
