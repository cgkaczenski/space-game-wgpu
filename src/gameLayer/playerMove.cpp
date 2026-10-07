#include <playerMove.h>
#include <tuning.h>

#include <engine/movement.h>
#include "imgui.h"
#include "platformInput.h"

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

	float held(int a, int b)
	{
		return (platform::isButtonHeld(a) || platform::isButtonHeld(b)) ? 1.f : 0.f;
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
	shipMode::Mode mode)
{
	using platform::Button;
	const float right = held(Button::D, Button::Right) - held(Button::A, Button::Left);
	const float forward = held(Button::W, Button::Up) - held(Button::S, Button::Down);

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

	if (drifting)
	{
		// Momentum with no acceleration and no drag: exactly constant velocity,
		// through the same step as everything else. No thrust, so no plume.
		ship.move = movement::momentum(0.f, 0.f);
		movement::step(ship, intent, gameDeltaTime);
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

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("player", {
	{"controls", controls},
	{"turnSpeed", turnSpeed},
	{"useMomentum", useMomentum},
	{"momentum.acceleration", momentumOptions.acceleration},
	{"momentum.topSpeed", momentumOptions.maxSpeed},
	{"momentum.falloff", momentumOptions.drag},
	{"instant.speed", instantOptions.maxSpeed},
});

void debugUi()
{
	int c = (int)controls;
	{
		tune::Highlight h(&controls); // the radios edit a copy
		ImGui::RadioButton("W/S toward mouse", &c, (int)Controls::MouseThrust);
		ImGui::RadioButton("A/D turn, mouse aims", &c, (int)Controls::TurnWithKeys);
		ImGui::RadioButton("WASD screen directions", &c, (int)Controls::ScreenDirections);
	}
	controls = (Controls)c;

	if (controls == Controls::TurnWithKeys)
	{
		tune::SliderFloat("Turn speed", &turnSpeed, 0.5f, 10.f, "%.1f rad/s");
	}

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
