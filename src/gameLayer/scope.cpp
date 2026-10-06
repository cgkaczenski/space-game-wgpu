#include <scope.h>
#include <tuning.h>

#include <engine/cameraFollow.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace scope
{

namespace
{
	// The cone and the zoom, with the pointer at the screen's centre and at
	// its edge: the further out it is, the further the scope looks -- longer,
	// narrower, and zoomed further out.
	float nearDegrees = 30.f;
	float farDegrees = 10.f;
	float nearRange = 6000.f;
	float farRange = 16000.f;
	float nearZoom = 0.5f;         // of the normal zoom, fully scoped
	float farZoom = 0.2f;

	float aroundRadius = 800.f;    // the all-round sight while scoped
	float leanShare = 0.8f;        // of half the view, with the pointer at the edge
	float brakeHalfLife = 0.1f;    // seconds for the ship's speed to halve
	float easeSeconds = 0.4f;      // in and out

	// A periscope's lag: how long the scope takes to follow the pointer
	// outward and inward, and to swing round to the aim; and how quickly the
	// view follows its lean.
	float lookSeconds = 0.5f;
	float turnSeconds = 0.6f;
	float viewRate = 2.f;          // per second

	bool down = false;
	float scoped = 0.f;            // eased 0 .. 1
	glm::vec2 look = {};           // the pointer from the centre, eased
	glm::vec2 facing = {1.f, 0.f}; // where the scope points, eased

	float reach() { return std::min(glm::length(look), 1.f); }
}

void update(bool held, glm::vec2 pointerFraction, float realDeltaTime)
{
	down = held;
	const float step = easeSeconds > 0.f ? realDeltaTime / easeSeconds : 1.f;
	scoped = std::clamp(scoped + (held ? step : -step), 0.f, 1.f);
	const glm::vec2 want = glm::clamp(pointerFraction, glm::vec2(-1.f), glm::vec2(1.f));
	const float follow = lookSeconds > 0.f ? 1.f - std::exp(-realDeltaTime / lookSeconds) : 1.f;
	look += (want - look) * follow;
}

void turnToward(glm::vec2 aim, float realDeltaTime)
{
	if (glm::length(aim) <= 0.f) { return; }
	const glm::vec2 want = glm::normalize(aim);
	if (scoped <= 0.f) { facing = want; return; } // down, it starts from the aim
	// By angle, as the sight's cone turns: a share of the way each moment.
	const float follow = turnSeconds > 0.f ? 1.f - std::exp(-realDeltaTime / turnSeconds) : 1.f;
	const float between = std::atan2(facing.x * want.y - facing.y * want.x, glm::dot(facing, want));
	const float turn = between * follow, c = std::cos(turn), s = std::sin(turn);
	facing = glm::normalize(glm::vec2(facing.x * c - facing.y * s, facing.x * s + facing.y * c));
}

bool held() { return down; }

float amount()
{
	// Smoothstep: eases in and out rather than starting and stopping hard.
	return scoped * scoped * (3.f - 2.f * scoped);
}

float brake(float gameDeltaTime)
{
	return std::exp2(-gameDeltaTime / std::max(brakeHalfLife, 0.001f));
}

sight::Look cone()
{
	const float r = reach();
	sight::Look look;
	look.facing = facing;
	look.halfAngle = glm::radians(glm::mix(nearDegrees, farDegrees, r) * 0.5f);
	look.range = glm::mix(nearRange, farRange, r);
	look.seesIntoFields = true;
	look.reachToRange = true;
	return look;
}

sight::Scope view()
{
	sight::Scope s;
	s.cone = cone();
	s.amount = amount();
	s.aroundRadius = aroundRadius;
	return s;
}

float zoom(float normalZoom)
{
	// In log space, as zoom is a ratio: toward the scope's, which is further
	// out the further out the pointer is.
	const float scopeZoom = std::max(glm::mix(nearZoom, farZoom, reach()), 0.01f);
	return normalZoom * std::pow(scopeZoom, amount());
}

glm::vec2 lean(glm::vec2 viewWorldSize)
{
	return camera::pointerLead(look, viewWorldSize, leanShare) * amount();
}

float cameraRate() { return viewRate; }

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("scope", {
	{"nearDegrees", nearDegrees},
	{"farDegrees", farDegrees},
	{"nearRange", nearRange},
	{"farRange", farRange},
	{"nearZoom", nearZoom},
	{"farZoom", farZoom},
	{"aroundRadius", aroundRadius},
	{"leanShare", leanShare},
	{"brakeHalfLife", brakeHalfLife},
	{"easeSeconds", easeSeconds},
	{"lookSeconds", lookSeconds},
	{"turnSeconds", turnSeconds},
	{"viewRate", viewRate},
});

void debugUi()
{
	ImGui::Text("Hold V. %s, %.2f in, looking %.2f out", down ? "held" : "off", amount(), reach());
	ImGui::TextDisabled("Pointer at the centre .. at the edge");
	tune::DragFloatRange2("Cone width", &farDegrees, &nearDegrees, 0.2f, 2.f, 120.f, "far %.0f deg", "near %.0f deg");
	tune::DragFloatRange2("Scope range", &nearRange, &farRange, 50.f, 1000.f, 60000.f, "near %.0f", "far %.0f");
	tune::DragFloatRange2("Zoom out", &farZoom, &nearZoom, 0.005f, 0.02f, 1.f, "far %.2f", "near %.2f");
	tune::SliderFloat("Sight round the ship", &aroundRadius, 100.f, 4000.f, "%.0f units while scoped");
	tune::SliderFloat("Lean", &leanShare, 0.f, 1.f, "%.2f of half the view");
	ImGui::TextDisabled("The periscope's lag");
	tune::SliderFloat("Follows the pointer", &lookSeconds, 0.f, 2.f, "%.2f s");
	tune::SliderFloat("Turns to the aim", &turnSeconds, 0.f, 2.f, "%.2f s");
	tune::SliderFloat("View follow", &viewRate, 0.2f, 20.f, "%.1f per second");
	tune::SliderFloat("Brake", &brakeHalfLife, 0.02f, 1.f, "%.2f s to half speed");
	tune::SliderFloat("Ease", &easeSeconds, 0.f, 1.f, "%.2f s in and out");
}

}
