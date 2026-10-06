#pragma once

// A sprite drawn as its own outline (resources/shaders/outline.wgsl): the
// player hidden in an asteroid field, drawn over the rocks (gameplay roadmap
// A1b). Like tall grass in other games: you still see where you are, and the
// outline is the sign that no enemy can.
//
// Nothing in it knows about ships or fields. Set it, draw any sprite the
// ordinary way, clear it; the sprite comes out as a line round its shape,
// and optionally a flat silhouette inside it -- never the sprite's own
// colours, which over a rock would read as the ship blended into it.

#include <render/wgpu2d.h>

namespace outline
{
	bool init();
	void cleanup();

	// Everything drawn between these two is outlined. `pulse` 0 .. 1 brightens
	// the line, so it can breathe.
	void begin(wgpu2d::Renderer2D &renderer, float pulse = 0.f);
	// The same in a colour of the caller's instead of the tuned one -- a
	// warning, say. Width, pulse and silhouette stay as tuned.
	void begin(wgpu2d::Renderer2D &renderer, float pulse, glm::vec3 lineColour);
	void end(wgpu2d::Renderer2D &renderer);

	void debugUi();
}
