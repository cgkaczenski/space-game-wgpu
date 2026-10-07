#include <interior.h>
#include <tuning.h>

#include <asteroids.h>
#include "imgui.h"

namespace interior
{

namespace
{
	enum class Rule
	{
		Normal,     // fields change nothing: movement before W2
		SpeedCap,   // in paint, the top speed is cut at once
		BleedsOff,  // in paint, the top speed is cut, and the excess lost over a moment
	};

	Rule rule = Rule::BleedsOff;
	float topSpeed = 0.5f;          // of a ship's own top speed, in paint
	float settleHalfLife = 0.15f;   // seconds for the excess to halve, entering fast
}

movement::Medium at(glm::vec2 position)
{
	if (rule == Rule::Normal || !asteroids::inField(position)) { return {}; }
	movement::Medium m;
	m.topSpeed = topSpeed;
	m.settleHalfLife = rule == Rule::BleedsOff ? settleHalfLife : 0.f;
	return m;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("interior", {
	{"rule", rule},
	{"topSpeed", topSpeed},
	{"settleHalfLife", settleHalfLife},
});

void debugUi()
{
	int r = (int)rule;
	{
		tune::Highlight h(&rule); // the radios edit a copy
		ImGui::RadioButton("Normal", &r, (int)Rule::Normal);
		ImGui::SameLine();
		ImGui::RadioButton("Speed cap", &r, (int)Rule::SpeedCap);
		ImGui::SameLine();
		ImGui::RadioButton("Bleeds off", &r, (int)Rule::BleedsOff);
	}
	rule = (Rule)r;
	if (rule == Rule::Normal) { return; }
	tune::SliderFloat("Top speed in a field", &topSpeed, 0.1f, 1.f, "%.2f of a ship's own");
	if (rule == Rule::BleedsOff)
	{
		tune::SliderFloat("Slowing", &settleHalfLife, 0.02f, 1.f, "%.2f s for the excess to halve");
	}
	ImGui::TextDisabled("  every ship; flight's boost multiplies it; the ram bursts through");
}

}
