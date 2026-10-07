#pragma once

// The ram's aim: while the right mouse button is held, a red line from the
// ship toward the pointer, as long as the ram will carry it, with a bar
// across where it will end. Letting go rams along it (ram.h).
//
// The line ends where the ship would: the surge less the dip before it
// (ram::reach), or short of the first field core on the way, which the ship
// bounces off. Rocks and enemies do not shorten it -- the ram goes through
// both. Dimmed while the ram is cooling down, when letting go does nothing.

#include <render/wgpu2d.h>
#include <glm/vec2.hpp>

namespace ramPath
{
	// Where a ram starting now from `from` along unit `direction` would end,
	// for a hull of `radius`.
	glm::vec2 end(glm::vec2 from, glm::vec2 direction, float radius);

	// The line and its end bar, in the world's camera, a fixed width on
	// screen at any zoom. `ready`: the ram can go now.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec2 from, glm::vec2 to, bool ready);

	void debugUi();
}
