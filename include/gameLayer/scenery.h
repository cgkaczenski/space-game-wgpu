#pragma once

// A level's backdrop pieces (gameplay roadmap L2): the planets cut from
// background4.png, and the small black-hole and shattered-planet art from
// resources/space, each at its own parallax depth. Decoration only -- nothing
// collides with it.
//
// It owns those textures, because it is the only thing that draws them
// (roadmap R10). The parallax is the starfield's idea (TiledRenderer) for a
// single sprite: a piece at depth d moves (1 - d) as far on screen as the
// world does.

#include <level.h>
#include <render/wgpu2d.h>
#include <vector>

namespace scenery
{
	bool init();
	void cleanup();

	// Under the current (world) camera, after the background, before ships.
	// Unknown art names are skipped.
	void draw(wgpu2d::Renderer2D &renderer, const std::vector<level::Scenery> &pieces);

	// The names a level file may use, for the editor.
	int artCount();
	const char *artName(int index);
}
