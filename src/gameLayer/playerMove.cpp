#include <playerMove.h>
#include <controls.h>
#include <tuning.h>

#include <engine/movement.h>
#include "imgui.h"
#include "platformInput.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace playerMove
{

namespace
{
	Controls controls = Controls::MouseThrust;

	// Momentum with a light falloff: the cap sets the top speed, the old 2000,
	// and the drag only decides how long the ship coasts once thrust stops --
	// at 0.3 its speed halves in about 2.3 s. The drag slider goes to 0, which
	// coasts forever.
	movement::Options momentumOptions = []
	{
		movement::Options o = movement::momentum(6000.f, 0.3f);
		o.maxSpeed = 2000.f;
		return o;
	}();
	movement::Options instantOptions = movement::instant(2000.f);
	bool useMomentum = true;

	// Radians per second, game time. About 200 degrees a second.
	float turnSpeed = 3.5f;

	// Shift: seconds for the ship's speed to halve. From flight's top speed
	// it is all but stopped in about half a second.
	float brakeHalfLife = 0.08f;
	// Below this the brake finishes the job, rather than halving for ever.
	const float stoppedSpeed = 15.f;

	float oneIfHeld(controls::Action a)
	{
		return controls::held(a) ? 1.f : 0.f;
	}

	// Screen space is y-down, so a positive angle turns clockwise on screen.
	glm::vec2 rotate(glm::vec2 v, float radians)
	{
		const float c = std::cos(radians);
		const float s = std::sin(radians);
		return {v.x * c - v.y * s, v.x * s + v.y * c};
	}
}

glm::vec2 update(movement::Body &ship, glm::vec2 mouseDirection, float gameDeltaTime, bool drifting,
	bool braking, shipMode::Mode mode)
{
	using controls::Action;
	const float right = oneIfHeld(Action::Right) - oneIfHeld(Action::Left);
	const float forward = oneIfHeld(Action::Forward) - oneIfHeld(Action::Back);

	// Turning is the same whether or not the ship is drifting: the cloak takes
	// thrust away, not the hull's heading.
	movement::Intent intent;
	switch (controls)
	{
	case Controls::MouseThrust:
	case Controls::ScreenDirections:
		intent.face = mouseDirection;
		break;
	case Controls::TurnWithKeys:
		intent.face = rotate(ship.facing, right * turnSpeed * gameDeltaTime);
		break;
	}
	ship.turnRate = 0.f; // the hull snaps where it is told (P1: the player's keeps snapping)

	if (drifting || braking)
	{
		// Momentum with no acceleration and no drag: exactly constant velocity,
		// through the same step as everything else. No thrust, so no plume.
		ship.move = movement::momentum(0.f, 0.f);
		movement::step(ship, intent, gameDeltaTime);
		if (braking && !drifting)
		{
			ship.velocity *= std::exp2(-gameDeltaTime / std::max(brakeHalfLife, 0.001f));
			if (glm::length(ship.velocity) < stoppedSpeed) { ship.velocity = {}; }
		}
		return mouseDirection;
	}

	switch (controls)
	{
	case Controls::MouseThrust:
	case Controls::TurnWithKeys:
		intent.forward = forward;
		break;
	case Controls::ScreenDirections:
		intent.thrust = {right, -forward};
		break;
	}

	ship.move = shipMode::movementFor(mode, useMomentum ? momentumOptions : instantOptions);
	movement::step(ship, intent, gameDeltaTime);
	return mouseDirection;
}

Controls scheme() { return controls; }
void setScheme(Controls c) { controls = c; }

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("player", {
	{"turnSpeed", turnSpeed},
	{"brakeHalfLife", brakeHalfLife},
	{"useMomentum", useMomentum},
	{"momentum.acceleration", momentumOptions.acceleration},
	{"momentum.topSpeed", momentumOptions.maxSpeed},
	{"momentum.falloff", momentumOptions.drag},
	{"instant.speed", instantOptions.maxSpeed},
});

void debugUi()
{
	// The player's setting since K2 (menu: Settings > Controls), not tuning:
	// changed here, it lasts until the game is closed.
	int c = (int)controls;
	ImGui::RadioButton("W/S toward mouse", &c, (int)Controls::MouseThrust);
	ImGui::RadioButton("A/D turn, mouse aims", &c, (int)Controls::TurnWithKeys);
	ImGui::RadioButton("WASD screen directions", &c, (int)Controls::ScreenDirections);
	controls = (Controls)c;

	if (controls == Controls::TurnWithKeys)
	{
		tune::SliderFloat("Turn speed", &turnSpeed, 0.5f, 10.f, "%.1f rad/s");
	}

	tune::SliderFloat("Brake (Shift)", &brakeHalfLife, 0.02f, 0.5f, "%.2f s to half speed");

	tune::Checkbox("Momentum", &useMomentum);
	if (useMomentum)
	{
		tune::SliderFloat("Acceleration", &momentumOptions.acceleration, 500.f, 30000.f, "%.0f",
			ImGuiSliderFlags_Logarithmic);
		tune::SliderFloat("Top speed", &momentumOptions.maxSpeed, 200.f, 6000.f, "%.0f");

		// Linear, not logarithmic, so the slider reaches 0.
		tune::SliderFloat("Falloff", &momentumOptions.drag, 0.f, 3.f, "%.2f");
		if (momentumOptions.drag > 0.f)
		{
			ImGui::TextDisabled("coasting speed halves every %.1f s", std::log(2.f) / momentumOptions.drag);
		}
		else
		{
			ImGui::TextDisabled("no falloff: coasts until you thrust against it");
		}
	}
	else
	{
		tune::SliderFloat("Speed", &instantOptions.maxSpeed, 200.f, 6000.f, "%.0f");
	}
}

}
