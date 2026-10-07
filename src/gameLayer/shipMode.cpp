#include <shipMode.h>
#include <tuning.h>

#include "imgui.h"

namespace shipMode
{

namespace
{
	// Flight's speed, against fight's: the player's own movement tuning
	// times these.
	float flightTopSpeed = 1.6f;
	float flightAcceleration = 1.5f;
}

void toggle(Mode &mode, energy::Energy &energy)
{
	mode = mode == Mode::Fight ? Mode::Flight : Mode::Fight;
	energy::setShield(energy, mode == Mode::Fight);
}

bool beamMines(Mode mode) { return mode == Mode::Flight; }

movement::Options movementFor(Mode mode, movement::Options options)
{
	if (mode == Mode::Flight)
	{
		options.maxSpeed *= flightTopSpeed;
		options.acceleration *= flightAcceleration;
	}
	return options;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("flight", {
	{"topSpeed", flightTopSpeed},
	{"acceleration", flightAcceleration},
});

void debugUi(Mode &mode, energy::Energy &energy)
{
	ImGui::Text("%s (Tab)", mode == Mode::Fight ? "fight" : "flight");
	ImGui::SameLine();
	if (ImGui::SmallButton("Switch")) { toggle(mode, energy); }
	tune::SliderFloat("Flight top speed", &flightTopSpeed, 1.f, 3.f, "%.2fx");
	tune::SliderFloat("Flight acceleration", &flightAcceleration, 1.f, 3.f, "%.2fx");
	ImGui::TextDisabled("  of the Player section's speed");
}

}
