#include <energy.h>

#include <cloak.h>
#include <shipShield.h>
#include "imgui.h"

namespace energy
{

namespace
{
	enum class State { Shielded, Breaking, Down, Cloaked };

	State state = State::Shielded;
	float amount = 1.f;

	// Seconds left before a Breaking shield drops.
	float breakTimer = 0.f;

	// Seconds from empty to full. A debug slider while it is tuned.
	float refillSeconds = 8.f;

	const char *stateName(State s)
	{
		switch (s)
		{
		case State::Shielded: return "shielded";
		case State::Breaking: return "breaking";
		case State::Down:     return "down, refilling";
		case State::Cloaked:  return "cloaked";
		}
		return "";
	}

	void raiseShield()
	{
		state = State::Shielded;
		amount = 1.f;
		shield::setActive(true);
	}
}

void reset()
{
	breakTimer = 0.f;
	cloak::setActive(false);
	raiseShield();
}

void update(float gameDeltaTime)
{
	switch (state)
	{
	case State::Shielded:
	case State::Cloaked:
		break;

	case State::Breaking:
		breakTimer -= gameDeltaTime;
		if (breakTimer <= 0.f)
		{
			shield::setActive(false);
			state = State::Down;
		}
		break;

	case State::Down:
		amount += gameDeltaTime / refillSeconds;
		if (amount >= 1.f) { raiseShield(); }
		break;
	}
}

HitResult onHit(glm::vec2 offsetFromShip)
{
	switch (state)
	{
	case State::Shielded:
		shield::hit(offsetFromShip);
		amount = 0.f;
		// The shield drops when this hit's ripple has run, so the break is
		// something the player watches happen rather than a sudden absence.
		breakTimer = shield::rippleSeconds();
		state = State::Breaking;
		return HitResult::Blocked;

	case State::Breaking:
		shield::hit(offsetFromShip); // ripples, but the break time stands
		return HitResult::Blocked;

	case State::Down:
		amount = 0.f;
		return HitResult::Damaged;

	case State::Cloaked:
		return HitResult::Missed;
	}
	return HitResult::Damaged;
}

void cloak()
{
	// Only from a full bar: the cloak spends everything, so there has to be
	// everything to spend. That also rules out cloaking mid-break.
	if (state != State::Shielded) { return; }
	state = State::Cloaked;
	amount = 0.f;
	breakTimer = 0.f;
	shield::setActive(false);
	cloak::setActive(true);
}

void uncloak()
{
	if (state != State::Cloaked) { return; }
	cloak::setActive(false);
	state = State::Down; // the refill starts from empty
}

bool isCloaked() { return state == State::Cloaked; }

float level() { return amount; }

void debugUi()
{
	ImGui::Text("%s", stateName(state));
	ImGui::SameLine();
	ImGui::ProgressBar(amount, {-1.f, 0.f});
	ImGui::SliderFloat("Refill time", &refillSeconds, 1.f, 30.f, "%.1f s");
	// Shield visuals still need testing without taking a hit and waiting.
	if (ImGui::Button("Refill energy") && state != State::Cloaked) { raiseShield(); }
}

}
