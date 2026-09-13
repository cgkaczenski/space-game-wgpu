#pragma once

// The parallax starfield behind everything: four tiled layers, each moving
// slower than the one in front of it.
//
// It owns its textures because it is the only thing that draws them (roadmap
// R10). TiledRenderer is the mechanism -- tile one texture across a view with
// a parallax strength -- and this is which textures, and how strong.

#include <render/wgpu2d.h>

namespace background
{
	// Loads the layers. False if one of them did not load.
	bool init();
	void cleanup();

	// Draws every layer under the renderer's current camera, back to front.
	void draw(wgpu2d::Renderer2D &renderer);
}
