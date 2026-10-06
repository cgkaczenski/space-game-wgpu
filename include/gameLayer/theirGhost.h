#pragma once

// Where the enemies think the player is (sight roadmap S5): a faint outline of
// the player's own ship at the spot a searching enemy is flying to, facing the
// way the player faced when it was lost. The mirror of S4's ghosts -- those
// say where the player last saw them, this where they last saw the player --
// and it is what makes slipping away readable: the ghost stays, and the
// enemies go to it.
//
// The memory is the enemies' own: a searching enemy holds `lastKnown` and
// `lastKnownFacing`. An engaged one knows where the player is, and an unaware
// one has given up, so neither leaves a ghost. Beliefs close together are
// drawn as one, stronger for each enemy behind it. Drawn after the fog, so it
// keeps its colour.

#include <render/wgpu2d.h>
#include <enemy.h>
#include <functional>
#include <vector>

namespace theirGhost
{
	// `known`: whether the player knows of an enemy -- sees it, or holds its
	// ghost. Asked only when the Show selection is "Enemies you know of".
	// `playerCell` is the player's sprite cell in `shipSheet`.
	void draw(wgpu2d::Renderer2D &renderer, const std::vector<Enemy> &enemies,
		const std::function<bool(const Enemy &)> &known,
		wgpu2d::Texture shipSheet, glm::vec4 playerCell, float shipSize);

	void debugUi();
}
