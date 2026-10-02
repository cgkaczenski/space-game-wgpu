#include <outline.h>
#include <tuning.h>

#include "imgui.h"
#include <platformTools.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace outline
{

namespace
{
	wgpu2d::Effect effect;

	glm::vec3 colour = {0.75f, 1.0f, 0.85f};
	// Inside the line: off by default. When on, a flat silhouette of this
	// colour, never the sprite's own -- over a rock, the sprite's colours
	// would tint the rock, and the ship would look blended into it.
	float fill = 0.f;
	glm::vec3 fillColour = {0.02f, 0.03f, 0.05f};
	float widthTexels = 1.2f;   // the line, in the sprite's own pixels...
	float minWidthPixels = 1.5f; // ...but never thinner than this on screen
	float pulseGain = 0.35f;    // how much `pulse` brightens it

	bool readFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}
}

bool init()
{
	std::string source;
	const char *path = RESOURCES_PATH "shaders/outline.wgsl";
	if (!readFile(path, source))
	{
		std::cerr << "outline: cannot read " << path << "\n";
		return false;
	}
	effect = wgpu2d::createEffect(source.c_str(), "outline");
	if (effect.id == 0)
	{
		std::cerr << "outline: effect did not compile\n";
		return false;
	}
	return true;
}

void cleanup() {}

void begin(wgpu2d::Renderer2D &renderer, float pulse)
{
	wgpu2d::EffectParams params;
	params.a = {colour * (1.f + pulseGain * pulse), fill};
	params.b = {widthTexels, minWidthPixels, 0.f, 0.f};
	params.c = {fillColour, 0.f};
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	renderer.setEffect(effect, params);
}

void end(wgpu2d::Renderer2D &renderer)
{
	renderer.clearEffect();
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("outline", {
	{"colour", colour},
	{"fill", fill},
	{"fillColour", fillColour},
	{"widthTexels", widthTexels},
	{"minWidthPixels", minWidthPixels},
	{"pulseGain", pulseGain},
});

void debugUi()
{
	tune::ColorEdit3("Hidden outline", &colour.x);
	tune::SliderFloat("Silhouette", &fill, 0.f, 1.f, "%.2f");
	tune::ColorEdit3("Silhouette colour", &fillColour.x);
	tune::SliderFloat("Outline width", &widthTexels, 0.5f, 4.f, "%.1f texels");
	tune::SliderFloat("Outline min", &minWidthPixels, 0.5f, 6.f, "%.1f screen px");
	tune::SliderFloat("Outline pulse", &pulseGain, 0.f, 1.f, "%.2f");
}

}
