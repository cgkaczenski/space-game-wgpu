#include <arena.h>
#include <tuning.h>

#include <shipSprite.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace arena
{

namespace
{
	float arenaRadius = 0.f;

	bool ringVisible = true;
	float ringWidthPixels = 3.f;

	// ---- The closing circle (gameplay roadmap L4) ----

	zone::ClosingZone closing;
	bool closingPaused = false; // debug: hold the schedule where it is

	// Every stage holds this long, and closes faster than the last: the first
	// close's edge moves at `firstCloseSpeed`, each after it `speedGrowth`
	// times the one before.
	float holdSeconds = 60.f;
	float firstCloseSpeed = 150.f;
	float speedGrowth = 1.6f;

	// The last seconds of a hold, when the ring flashes faster and faster and
	// turns from blue through amber to red.
	float warnSeconds = 10.f;
	float warnStartHz = 1.f;
	float warnEndHz = 7.f;

	// Outside: health per second, landing in ticks so each one can be felt.
	// Enemies have the same 1 of life as the player, and their own rate.
	float burnPerSecond = 0.08f;
	float enemyBurnPerSecond = 0.08f;
	float burnTickSeconds = 0.5f;
	float burnTimer = 0.f;
	float flash = 0.f;
	float flashFadePerSecond = 4.f;

	// One ship's ticks. Grazing the edge costs nothing: a tick needs a full
	// tick outside, and coming back in starts the count again.
	float tickBurn(glm::vec2 position, float dt, float perSecond, float &timer, float &shipFlash)
	{
		if (!outside(position))
		{
			timer = 0.f;
			return 0.f;
		}

		float damage = 0.f;
		timer += dt;
		const float tick = std::max(burnTickSeconds, 0.05f);
		while (timer >= tick)
		{
			timer -= tick;
			damage += perSecond * tick;
			shipFlash = 1.f;
		}
		return damage;
	}

	float pulseClock = 0.f; // game seconds, for the ring's pulse

	const glm::vec3 calmBlue = {0.35f, 0.65f, 1.00f};
	const glm::vec3 amber = {1.00f, 0.65f, 0.15f};
	const glm::vec3 red = {1.00f, 0.22f, 0.15f};

	// The sliders are read live, so tuning shows without a restart.
	void applyTimings()
	{
		float speed = firstCloseSpeed;
		for (zone::Stage &s : closing.stages)
		{
			s.holdSeconds = holdSeconds;
			s.closeSpeed = speed;
			speed *= speedGrowth;
		}
	}

	// Two passes of the same circle: a wide dim one for a glow, a thin one for
	// the line. Widths in world units, divided by zoom so they hold on screen.
	// 256 segments: at a 20000 radius a chord is off the true circle by under
	// two units, far below a pixel at any zoom the view allows.
	void drawRing(wgpu2d::Renderer2D &renderer, zone::Circle c, glm::vec3 colour,
		float brightness, float widthPixels, float px)
	{
		if (c.radius <= 0.f) { return; }
		const glm::vec3 line = colour * brightness;
		const glm::vec3 glow = colour * (brightness * 0.45f);
		renderer.renderCircleOutline(c.centre, {glow, 1.f}, c.radius, widthPixels * 5.f * px, 256);
		renderer.renderCircleOutline(c.centre, {line, 1.f}, c.radius, widthPixels * px, 256);
	}
}

void start(float r, const std::vector<level::Ring> &rings)
{
	arenaRadius = std::max(r, 0.f);

	std::vector<zone::Stage> stages;
	if (arenaRadius > 0.f && !rings.empty())
	{
		std::vector<level::Ring> sorted = rings;
		std::sort(sorted.begin(), sorted.end(),
			[](const level::Ring &a, const level::Ring &b) { return a.radius > b.radius; });
		for (const level::Ring &ring : sorted)
		{
			stages.push_back({{ring.position, std::min(ring.radius, arenaRadius)}});
		}
		// And then to nothing, where the last ring was.
		stages.push_back({{sorted.back().position, 0.f}});
	}
	closing = zone::begin({{0.f, 0.f}, arenaRadius}, std::move(stages));
	applyTimings();

	burnTimer = 0.f;
	flash = 0.f;
	pulseClock = 0.f;
}

float radius() { return arenaRadius; }

void update(float dt)
{
	applyTimings();
	if (!closingPaused) { zone::update(closing, dt); }
	pulseClock = std::fmod(pulseClock + dt, 1000.f);
	flash = std::max(0.f, flash - flashFadePerSecond * dt);
}

zone::Circle safeZone() { return zone::current(closing); }
bool closes() { return !closing.stages.empty(); }

bool outside(glm::vec2 position)
{
	return arenaRadius > 0.f && zone::outside(safeZone(), position) > 0.f;
}

bool onFinalRing()
{
	return closes() && closing.stage >= (int)closing.stages.size() - 1;
}

bool collapsed()
{
	return closes() && closing.phase == zone::Phase::Done;
}

float burn(glm::vec2 position, float dt)
{
	return tickBurn(position, dt, burnPerSecond, burnTimer, flash);
}

float burnFlash() { return flash; }

float burnEnemy(Enemy &enemy, float dt)
{
	enemy.burnFlash = std::max(0.f, enemy.burnFlash - flashFadePerSecond * dt);
	return tickBurn(enemy.body.position, dt, enemyBurnPerSecond, enemy.burnTimer, enemy.burnFlash);
}

void drawBurnFlash(wgpu2d::Renderer2D &renderer, float shipFlash, glm::vec2 position,
	float size, wgpu2d::Texture sheet, glm::vec4 cell, glm::vec2 facing, float alpha, float stretch)
{
	if (shipFlash <= 0.f) { return; }
	// The hull's own sprite, tinted red and added on top: the hull lights up
	// red in its own shape, whatever the grade did to the colours under it.
	renderSpaceShip(renderer, position, size, sheet, cell, facing,
		{1.f, 0.25f, 0.15f, shipFlash * alpha}, stretch);
}

bool wayBackIn(glm::vec2 position, glm::vec2 &to)
{
	if (!outside(position)) { return false; }
	to = safeZone().centre;
	return true;
}

void draw(wgpu2d::Renderer2D &renderer, float zoom)
{
	if (!ringVisible || arenaRadius <= 0.f) { return; }

	const float px = 1.f / std::max(zoom, 0.01f);
	const float tau = 6.2831853f;
	const zone::Circle safe = safeZone();

	if (!closes())
	{
		drawRing(renderer, safe, calmBlue, 1.f, ringWidthPixels, px);
		return;
	}

	// Where it goes next: faint and thin, so it reads as a plan, not a wall.
	if (closing.phase != zone::Phase::Done)
	{
		drawRing(renderer, zone::next(closing), {1.f, 1.f, 1.f}, 0.35f, ringWidthPixels * 0.5f, px);
	}

	glm::vec3 colour = red;
	float brightness = 1.f;
	float width = ringWidthPixels;
	if (closing.phase == zone::Phase::Holding)
	{
		const float hold = zone::phaseSeconds(closing);
		const float warnFrom = std::max(hold - warnSeconds, 0.f);
		const float t = closing.elapsed;
		if (t < warnFrom)
		{
			// Calm: a slow breath.
			colour = calmBlue;
			brightness = 0.8f + 0.2f * std::sin(tau * 0.4f * pulseClock);
		}
		else
		{
			// The warning. The flash rate climbs linearly from start to end,
			// so its phase is the integral of that: f0 t + (f1 - f0) t^2 / 2T.
			// Reading the rate straight into sin(2 pi f t) instead would make
			// the flashes jump backwards as f grows.
			const float span = std::max(hold - warnFrom, 0.001f);
			const float w = std::clamp((t - warnFrom) / span, 0.f, 1.f);
			const float tw = t - warnFrom;
			const float cycles = warnStartHz * tw + (warnEndHz - warnStartHz) * tw * tw / (2.f * span);
			const float on = 0.5f + 0.5f * std::cos(tau * cycles);
			colour = w < 0.5f ? glm::mix(calmBlue, amber, w * 2.f) : glm::mix(amber, red, (w - 0.5f) * 2.f);
			brightness = 0.25f + 1.f * on * on;
			width = ringWidthPixels * (1.f + 0.6f * w);
		}
	}
	else if (closing.phase == zone::Phase::Closing)
	{
		brightness = 0.75f + 0.35f * std::sin(tau * 1.5f * pulseClock);
		width = ringWidthPixels * 1.6f;
	}
	drawRing(renderer, safe, colour, brightness, width, px);
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("arena", {
	{"ringVisible", ringVisible},
	{"ringWidthPixels", ringWidthPixels},
	{"closingPaused", closingPaused},
	{"holdSeconds", holdSeconds},
	{"firstCloseSpeed", firstCloseSpeed},
	{"speedGrowth", speedGrowth},
	{"warnSeconds", warnSeconds},
	{"warnStartHz", warnStartHz},
	{"warnEndHz", warnEndHz},
	{"burnPerSecond", burnPerSecond},
	{"enemyBurnPerSecond", enemyBurnPerSecond},
	{"burnTickSeconds", burnTickSeconds},
});

void debugUi()
{
	ImGui::Text("Arena radius: %.0f", arenaRadius);
	tune::Checkbox("Edge ring", &ringVisible);
	tune::SliderFloat("Ring width px", &ringWidthPixels, 0.5f, 10.f);

	ImGui::SeparatorText("Closing circle");
	if (!closes())
	{
		ImGui::TextDisabled("This level has no rings: nothing closes");
		return;
	}
	const char *phase = closing.phase == zone::Phase::Holding ? "holding"
		: closing.phase == zone::Phase::Closing ? "closing" : "closed";
	const zone::Circle safe = safeZone();
	ImGui::Text("Stage %d of %d, %s, %.0f s left", std::min(closing.stage + 1, (int)closing.stages.size()),
		(int)closing.stages.size(), phase,
		zone::phaseSeconds(closing) * (1.f - zone::phaseProgress(closing)));
	ImGui::Text("Safe zone: %.0f, %.0f  radius %.0f", safe.centre.x, safe.centre.y, safe.radius);
	if (ImGui::Button("Skip phase")) { zone::skipPhase(closing); }
	ImGui::SameLine();
	tune::Checkbox("Hold schedule", &closingPaused);
	tune::SliderFloat("Hold", &holdSeconds, 1.f, 180.f, "%.0f s");
	tune::SliderFloat("First close speed", &firstCloseSpeed, 10.f, 2000.f, "%.0f u/s");
	tune::SliderFloat("Speed growth", &speedGrowth, 1.f, 3.f, "x%.2f per stage");
	tune::SliderFloat("Warning", &warnSeconds, 0.f, 30.f, "%.1f s");
	tune::SliderFloat("Warning flash from", &warnStartHz, 0.2f, 5.f, "%.1f Hz");
	tune::SliderFloat("Warning flash to", &warnEndHz, 1.f, 15.f, "%.1f Hz");
	tune::SliderFloat("Burn", &burnPerSecond, 0.f, 0.5f, "%.3f health/s");
	tune::SliderFloat("Enemy burn", &enemyBurnPerSecond, 0.f, 0.5f, "%.3f life/s");
	tune::SliderFloat("Burn tick", &burnTickSeconds, 0.05f, 2.f, "%.2f s");
}

}
