#include <gameState.h>

#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace gameState
{

namespace
{
	State state = State::Playing;
	float inState = 0.f; // real seconds since entering it

	// Focus lost while dying or extracting: there is no pause to take then,
	// so the next round starts paused instead of dropping the player into it.
	bool pauseOnNextRound = false;

	// After a restart, the transition runs backwards over the new round.
	enum class Reveal { None, SwitchOn, FromWhite };
	Reveal reveal = Reveal::None;
	float revealLeft = 0.f;

	float look = 0.f; // the paused look, eased

	// Tuning. All real seconds.
	float lingerSeconds = 2.0f;     // the world running on around the wreck
	float switchOffSeconds = 0.45f; // static, then the collapse to a line and a dot
	float switchOnSeconds = 0.3f;   // the same, backwards, over the new round
	float warpDuration = 0.8f;      // the surge before the white
	float whiteSeconds = 0.4f;      // up to white
	float fromWhiteSeconds = 0.6f;  // back down, over the new round
	float lookSeconds = 0.15f;      // into and out of the paused look

	// The warp: speed = start + gain * t^2, stretch = 1 + stretchGain * t^2,
	// with t the seconds into it.
	float warpStartSpeed = 300.f;
	float warpSpeedGain = 20000.f;
	float warpStretchGain = 6.f;

	void enter(State s)
	{
		state = s;
		inState = 0.f;
	}
}

bool update(float dt, const Input &input)
{
	inState += dt;

	if (reveal != Reveal::None)
	{
		revealLeft -= dt;
		if (revealLeft <= 0.f) { reveal = Reveal::None; revealLeft = 0.f; }
	}

	bool restart = false;
	switch (state)
	{
	case State::Playing:
		if (!input.focused || input.escapePressed || pauseOnNextRound)
		{
			pauseOnNextRound = false;
			enter(State::Paused);
		}
		break;

	case State::Paused:
		if (input.escapePressed) { enter(State::Playing); }
		break;

	case State::Dying:
		if (!input.focused) { pauseOnNextRound = true; }
		if (inState >= lingerSeconds + switchOffSeconds)
		{
			enter(State::Playing);
			reveal = Reveal::SwitchOn;
			revealLeft = switchOnSeconds;
			restart = true;
		}
		break;

	case State::Extracting:
		if (!input.focused) { pauseOnNextRound = true; }
		if (inState >= warpDuration + whiteSeconds)
		{
			enter(State::Playing);
			reveal = Reveal::FromWhite;
			revealLeft = fromWhiteSeconds;
			restart = true;
		}
		break;
	}

	const float target = state == State::Paused ? 1.f : 0.f;
	const float step = lookSeconds > 0.f ? dt / lookSeconds : 1.f;
	look = target > look ? std::min(target, look + step) : std::max(target, look - step);

	return restart;
}

State current() { return state; }
bool paused() { return state == State::Paused; }
bool controlsLive() { return state == State::Playing; }
bool playerPresent() { return state == State::Playing || state == State::Paused; }

void playerDied()
{
	if (state == State::Playing) { enter(State::Dying); }
}

void extract()
{
	if (state == State::Playing) { enter(State::Extracting); }
}

float warpSpeed()
{
	const float t = state == State::Extracting ? inState : 0.f;
	return warpStartSpeed + warpSpeedGain * t * t;
}

float warpStretch()
{
	const float t = state == State::Extracting ? std::min(inState, warpDuration) : 0.f;
	return 1.f + warpStretchGain * t * t;
}

// Smoothstep, so the look and the fades ease at both ends.
static float ease(float t)
{
	t = std::clamp(t, 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

float pauseLook() { return ease(look); }

// Linear: the shader shapes the switch-off itself, in stages, and wants
// progress it can cut into ranges rather than a curve it would have to undo.
float switchOff()
{
	if (state == State::Dying && inState > lingerSeconds)
	{
		return std::clamp((inState - lingerSeconds) / std::max(switchOffSeconds, 0.001f), 0.f, 1.f);
	}
	if (reveal == Reveal::SwitchOn) { return revealLeft / std::max(switchOnSeconds, 0.001f); }
	return 0.f;
}

float warpBlur()
{
	if (state != State::Extracting) { return 0.f; }
	const float t = std::clamp(inState / std::max(warpDuration, 0.001f), 0.f, 1.f);
	return t * t; // starts gently and runs away, like the surge
}

float whiteOut()
{
	if (state == State::Extracting && inState > warpDuration)
	{
		return ease((inState - warpDuration) / std::max(whiteSeconds, 0.001f));
	}
	if (reveal == Reveal::FromWhite) { return ease(revealLeft / std::max(fromWhiteSeconds, 0.001f)); }
	return 0.f;
}

void reset()
{
	if (state != State::Paused) { enter(State::Playing); }
	reveal = Reveal::None;
	revealLeft = 0.f;
	pauseOnNextRound = false;
}

void debugUi()
{
	static const char *names[] = {"Playing", "Paused", "Dying", "Extracting"};
	ImGui::Text("State: %s (%.1f s)", names[(int)state], inState);

	ImGui::SliderFloat("Death linger s", &lingerSeconds, 0.f, 5.f);
	ImGui::SliderFloat("Switch-off s", &switchOffSeconds, 0.05f, 2.f);
	ImGui::SliderFloat("Switch-on s", &switchOnSeconds, 0.05f, 2.f);
	ImGui::SliderFloat("Warp s", &warpDuration, 0.1f, 3.f);
	ImGui::SliderFloat("To white s", &whiteSeconds, 0.05f, 2.f);
	ImGui::SliderFloat("From white s", &fromWhiteSeconds, 0.05f, 2.f);
	ImGui::SliderFloat("Pause look s", &lookSeconds, 0.f, 1.f);
	ImGui::SliderFloat("Warp start speed", &warpStartSpeed, 0.f, 3000.f);
	ImGui::SliderFloat("Warp speed gain", &warpSpeedGain, 0.f, 60000.f);
	ImGui::SliderFloat("Warp stretch gain", &warpStretchGain, 0.f, 20.f);
}

}
