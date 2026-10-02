#include <energy.h>
#include <tuning.h>

#include <shipShield.h>
#include "imgui.h"

namespace energy
{

namespace
{
	// Seconds from empty to full. Shared by every ship.
	float refillSeconds = 8.f;

	// A beam held on the shield ripples it this often, not every frame: the
	// bubble keeps only a few ripples at once.
	const float beamRippleSeconds = 0.15f;

	const char *stateName(State s)
	{
		switch (s)
		{
		case State::Full:     return "full";
		case State::Breaking: return "breaking";
		case State::Down:     return "down, refilling";
		case State::Cloaked:  return "cloaked";
		}
		return "";
	}

	// Full again: the shield back up, if the ship has one.
	void fill(Energy &e)
	{
		e.state = State::Full;
		e.amount = 1.f;
		shield::setActive(e.bubble, e.hasShield);
	}

	// Emptied by a hit that reached the hull.
	void empty(Energy &e)
	{
		e.amount = 0.f;
		e.state = State::Down;
		shield::setActive(e.bubble, false);
	}

	bool shieldHolds(const Energy &e)
	{
		return e.hasShield && (e.state == State::Full || e.state == State::Breaking);
	}
}

void reset(Energy &e)
{
	e.breakTimer = 0.f;
	e.beamRippleLeft = 0.f;
	fill(e);
}

void update(Energy &e, float gameDeltaTime)
{
	switch (e.state)
	{
	case State::Full:
	case State::Cloaked:
		break;

	case State::Breaking:
		e.breakTimer -= gameDeltaTime;
		if (e.breakTimer <= 0.f)
		{
			shield::setActive(e.bubble, false);
			e.state = State::Down;
		}
		break;

	case State::Down:
		e.amount += gameDeltaTime / refillSeconds;
		if (e.amount >= 1.f) { fill(e); }
		break;
	}
}

HitResult onHit(Energy &e, glm::vec2 offsetFromShip)
{
	if (e.state == State::Cloaked) { return HitResult::Missed; }

	if (shieldHolds(e))
	{
		shield::hit(e.bubble, offsetFromShip);
		if (e.state == State::Full)
		{
			e.amount = 0.f;
			// The shield drops when this hit's ripple has run, so the break is
			// something to watch happen rather than a sudden absence. A hit
			// while Breaking ripples, but the break time stands.
			e.breakTimer = shield::rippleSeconds();
			e.state = State::Breaking;
		}
		return HitResult::Blocked;
	}

	// Down -- or full with no shield to take it: the hull, and the bar empty.
	empty(e);
	return HitResult::Damaged;
}

HitResult onBeam(Energy &e, glm::vec2 offsetFromShip, float gameDeltaTime)
{
	if (e.state == State::Cloaked) { return HitResult::Missed; }

	if (shieldHolds(e))
	{
		e.beamRippleLeft -= gameDeltaTime;
		if (e.beamRippleLeft <= 0.f)
		{
			shield::hit(e.bubble, offsetFromShip, 0.5f);
			e.beamRippleLeft = beamRippleSeconds;
		}
		return HitResult::Blocked;
	}

	empty(e);
	return HitResult::Damaged;
}

void cloak(Energy &e)
{
	// Only from a full bar: the cloak spends everything, so there has to be
	// everything to spend. That also rules out cloaking mid-break.
	if (!e.canCloak || e.state != State::Full) { return; }
	e.state = State::Cloaked;
	e.amount = 0.f;
	e.breakTimer = 0.f;
	shield::setActive(e.bubble, false);
}

void uncloak(Energy &e)
{
	if (e.state != State::Cloaked) { return; }
	e.state = State::Down; // the refill starts from empty
}

bool isCloaked(const Energy &e) { return e.state == State::Cloaked; }

float level(const Energy &e) { return e.amount; }

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("energy", {
	{"refillSeconds", refillSeconds},
});

void debugUi(Energy &e)
{
	ImGui::Text("%s", stateName(e.state));
	ImGui::SameLine();
	ImGui::ProgressBar(e.amount, {-1.f, 0.f});
	tune::SliderFloat("Refill time", &refillSeconds, 1.f, 30.f, "%.1f s");
	// Shield visuals still need testing without taking a hit and waiting.
	if (ImGui::Button("Refill energy") && e.state != State::Cloaked) { fill(e); }
}

}
