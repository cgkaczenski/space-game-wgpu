#include <textLook.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace textLook
{

namespace
{
	wgpu2d::Font proggy;

	// ProggyClean is drawn on a 13 px grid; baked at anything else its
	// pixels stop landing on texels (Dear ImGui's FONTS.md: size 13).
	const float bakePixels = 13.f;

	// One step of scale per this many pixels of framebuffer height: 1 in the
	// 500 px default window, 3 at 1440.
	const float pixelsPerStep = 480.f;

	const float shadowAlpha = 0.8f;
}

bool init()
{
	if (!proggy.createFromFile(RESOURCES_PATH "fonts/ProggyClean.ttf", bakePixels, true))
	{
		std::cerr << "textLook: failed to load the font\n";
		return false;
	}
	return true;
}

void cleanup()
{
	proggy.cleanup();
}

const wgpu2d::Font &font() { return proggy; }

float screenScale(int height, int size)
{
	return std::max(1.f, std::floor((float)height / pixelsPerStep)) * (float)std::max(size, 1);
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec2 position, const char *text,
	glm::vec4 colour, float scale, glm::vec2 anchor)
{
	renderer.renderText(position + glm::vec2(scale), text, proggy,
		{0.f, 0.f, 0.f, colour.a * shadowAlpha}, scale, anchor);
	renderer.renderText(position, text, proggy, colour, scale, anchor);
}

}
