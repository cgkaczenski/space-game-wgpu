#include <worldGrade.h>
#include <tuning.h>

#include "imgui.h"
#include <platformTools.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace worldGrade
{

namespace
{
	wgpu2d::Effect effect;

	// Created the first time the look is used, like the cloak's.
	wgpu2d::FrameBuffer worldTarget;

	float desaturate = 0.85f; // 1 is fully grey
	float brightness = 0.45f; // what is left of the light

	// Outside the safe zone: greyer than paused, but lit enough to fight in.
	bool outsideOn = true;
	float outsideDesaturate = 0.9f;
	float outsideBrightness = 0.6f;
	float outsideEdgePixels = 24.f; // the fade from colour to grey, on screen

	// Whether any of the view is outside `safe`: its furthest corner is.
	bool viewReaches(glm::vec4 view, const zone::Circle &safe)
	{
		const float dx = std::max(std::abs(view.x - safe.centre.x), std::abs(view.x + view.z - safe.centre.x));
		const float dy = std::max(std::abs(view.y - safe.centre.y), std::abs(view.y + view.w - safe.centre.y));
		return dx * dx + dy * dy > safe.radius * safe.radius;
	}

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
	const char *path = RESOURCES_PATH "shaders/worldGrade.wgsl";
	if (!readFile(path, source))
	{
		std::cerr << "worldGrade: cannot read " << path << "\n";
		return false;
	}

	effect = wgpu2d::createEffect(source.c_str(), "worldGrade");
	if (effect.id == 0)
	{
		std::cerr << "worldGrade: effect did not compile\n";
		return false;
	}
	return true;
}

void cleanup()
{
	worldTarget.cleanup();
}

void apply(wgpu2d::Renderer2D &renderer, float pauseAmount, const zone::Circle *safe,
	int width, int height)
{
	if (effect.id == 0 || width <= 0 || height <= 0) { return; }

	const glm::vec4 view = renderer.getViewRect();
	const bool outside = outsideOn && safe && view.z != 0.f && view.w != 0.f
		&& viewReaches(view, *safe);
	if (pauseAmount <= 0.f && !outside) { return; }

	if (worldTarget.fbo == 0)
	{
		worldTarget.create((unsigned)width, (unsigned)height);
		if (worldTarget.fbo == 0) { return; } // no target: the world stays ungraded
	}
	worldTarget.resize((unsigned)width, (unsigned)height);

	worldTarget.clear();
	renderer.flushFBO(worldTarget);

	// Back into the batch as one quad over the whole view, in screen space.
	// Premultiplied, because a target's contents are (outline 12).
	wgpu2d::EffectParams params;
	params.a = {desaturate * pauseAmount, 1.f - (1.f - brightness) * pauseAmount, 0.f, 0.f};
	if (outside)
	{
		// World to screen pixels: the same mapping the projection does, for
		// one point and one length. The quad's uv times the view's size is the
		// pixel the shader is on, y down, like this.
		const float screenPerWorld = (float)width / view.z;
		params.b = {(safe->centre.x - view.x) * screenPerWorld,
			(safe->centre.y - view.y) / view.w * (float)height,
			safe->radius * screenPerWorld, std::max(outsideEdgePixels, 0.5f)};
		params.c = {outsideDesaturate, outsideBrightness, 1.f, 0.f};
	}

	renderer.pushCamera();
	renderer.setBlendMode(wgpu2d::BlendMode::Premultiplied);
	renderer.setEffect(effect, params);
	renderer.renderRectangle({0.f, 0.f, (float)width, (float)height}, worldTarget.texture);
	renderer.clearEffect();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	renderer.popCamera();
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("worldGrade", {
	{"desaturate", desaturate},
	{"brightness", brightness},
	{"outsideOn", outsideOn},
	{"outsideDesaturate", outsideDesaturate},
	{"outsideBrightness", outsideBrightness},
	{"outsideEdgePixels", outsideEdgePixels},
});

void debugUi()
{
	ImGui::TextDisabled("Paused");
	tune::SliderFloat("Desaturate", &desaturate, 0.f, 1.f);
	tune::SliderFloat("Brightness", &brightness, 0.f, 1.f);
	ImGui::TextDisabled("Outside the closing circle");
	tune::Checkbox("Grey outside", &outsideOn);
	tune::SliderFloat("Outside desaturate", &outsideDesaturate, 0.f, 1.f);
	tune::SliderFloat("Outside brightness", &outsideBrightness, 0.f, 1.f);
	tune::SliderFloat("Edge fade px", &outsideEdgePixels, 1.f, 200.f, "%.0f");
}

}
