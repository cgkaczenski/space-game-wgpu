#pragma once

// How this game's text looks (gameplay roadmap U1): which font, at what size,
// and the shadow under it. The drawing itself is wgpu2d's (Font, renderText);
// what is here is the part no other game wants -- ProggyClean, baked at the
// 13 px it was drawn for, scaled by whole numbers only so it stays on its
// own pixel grid, with a one-texel shadow so it reads over a bright world.

#include <render/wgpu2d.h>

namespace textLook
{
	// Loads the font. Call once from initGame, after the renderer exists.
	bool init();
	void cleanup();

	const wgpu2d::Font &font();

	// The whole-number scale for screen text in a framebuffer `height`
	// pixels tall, times `size` -- 1 for ordinary HUD text, 2 for a heading.
	// Whole numbers because anything between doubles some of the font's
	// pixels and not others.
	float screenScale(int height, int size = 1);

	// Text with its shadow: the same string one font pixel down and right,
	// in black at `colour`'s alpha, then the text itself. The arguments are
	// renderText's.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec2 position, const char *text,
		glm::vec4 colour, float scale, glm::vec2 anchor = {0, 0});
}
