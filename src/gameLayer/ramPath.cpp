#include <ramPath.h>
#include <tuning.h>

#include <asteroids.h>
#include <ram.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace ramPath
{

namespace
{
	glm::vec4 colour = {1.f, 0.18f, 0.15f, 0.85f};
	float coolingDim = 0.35f;      // of the colour's alpha, while the ram cools down
	float widthPixels = 3.f;       // on screen
	float barPixels = 36.f;        // the end bar's length, on screen
}

glm::vec2 end(glm::vec2 from, glm::vec2 direction, float radius)
{
	const float reach = std::max(0.f, ram::reach());
	const float core = asteroids::raycast(from, direction, reach + radius, nullptr, asteroids::Which::Cores);
	const float along = core >= 0.f ? std::max(0.f, core - radius) : reach;
	return from + direction * along;
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec2 from, glm::vec2 to, bool ready)
{
	const glm::vec2 line = to - from;
	const float length = glm::length(line);
	if (length < 1.f) { return; }
	const glm::vec2 direction = line / length;
	const glm::vec2 across = {-direction.y, direction.x};

	// World units per screen pixel, so the line reads the same at any zoom.
	const float zoom = std::max(renderer.currentCamera.zoom, 1e-4f);
	const float width = widthPixels / zoom;
	const float bar = barPixels / zoom;

	glm::vec4 c = colour;
	if (!ready) { c.a *= coolingDim; }
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	renderer.renderLine(from, to, c, width);
	renderer.renderLine(to - across * (bar * 0.5f), to + across * (bar * 0.5f), c, width * 1.5f);
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("ramPath", {
	{"colour", colour},
	{"coolingDim", coolingDim},
	{"widthPixels", widthPixels},
	{"barPixels", barPixels},
});

void debugUi()
{
	ImGui::SeparatorText("Aim line (hold right click)");
	tune::ColorEdit4("Colour", &colour.x);
	tune::SliderFloat("Cooling down", &coolingDim, 0.f, 1.f, "%.2f of the alpha");
	tune::SliderFloat("Width", &widthPixels, 1.f, 10.f, "%.1f px");
	tune::SliderFloat("End bar", &barPixels, 0.f, 120.f, "%.0f px");
	ImGui::TextDisabled("  ends %.0f units out, or short of a core", ram::reach());
}

}
