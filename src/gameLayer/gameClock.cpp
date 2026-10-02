#include <gameClock.h>
#include <tuning.h>

#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace gameClock
{

namespace
{
	float gameSpeedScale = 50.f;

	float gameSpeedMultiplier()
	{
		// 50 stays 1x. 100 is 1.5x. Below 50 uses an exponential curve so 0 is ~1% speed.
		if (gameSpeedScale <= 50.f)
		{
			float t = gameSpeedScale / 50.f;
			return 0.01f * glm::pow(100.f, t);
		}

		float t = (gameSpeedScale - 50.f) / 50.f;
		return 1.f + t * 0.5f;
	}
}

namespace
{
	float stopLeft = 0.f; // seconds of real time
	bool paused = false;
}

FrameTime tick(float realDeltaTime)
{
	// Before the hit-stop, so a pause taken during one does not use it up.
	if (paused) { return {realDeltaTime, 0.f}; }
	if (stopLeft > 0.f)
	{
		stopLeft -= realDeltaTime;
		return {realDeltaTime, 0.f};
	}
	return {realDeltaTime, realDeltaTime * gameSpeedMultiplier()};
}

void hitStop(float seconds)
{
	stopLeft = std::max(stopLeft, seconds);
}

void setPaused(bool p) { paused = p; }

void reset()
{
	stopLeft = 0.f;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("clock", {
	{"gameSpeedScale", gameSpeedScale},
});

void debugUi()
{
	tune::SliderFloat("Game speed", &gameSpeedScale, 0, 100);
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fx", gameSpeedMultiplier());
}

}
