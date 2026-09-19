#include <worldGrade.h>

#include "imgui.h"
#include <platformTools.h>

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

void apply(wgpu2d::Renderer2D &renderer, float amount, int width, int height)
{
	if (amount <= 0.f || effect.id == 0 || width <= 0 || height <= 0) { return; }

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
	params.a = {desaturate * amount, 1.f - (1.f - brightness) * amount, 0.f, 0.f};

	renderer.pushCamera();
	renderer.setBlendMode(wgpu2d::BlendMode::Premultiplied);
	renderer.setEffect(effect, params);
	renderer.renderRectangle({0.f, 0.f, (float)width, (float)height}, worldTarget.texture);
	renderer.clearEffect();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	renderer.popCamera();
}

void debugUi()
{
	ImGui::SliderFloat("Desaturate", &desaturate, 0.f, 1.f);
	ImGui::SliderFloat("Brightness", &brightness, 0.f, 1.f);
}

}
