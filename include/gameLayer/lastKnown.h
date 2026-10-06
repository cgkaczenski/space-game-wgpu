#pragma once

// Where the player last saw each enemy (sight roadmap S4): a ghost of its hull
// left where it went out of sight, at the facing it had, with a short line the
// way it was heading. A memory, never a position -- it says where something
// was. Drawn after the fog, so it is not greyed, in a muted red.
//
// The memory is engine/contactMemory; what is here is this game's policy: an
// enemy is lost when the fog hides it or when it cloaks in sight; a ghost
// stays when its enemy dies out of sight (the player has no way to know);
// and how long a ghost lasts and how it ages are selections in the Last
// known section. With the fog off nothing is out of sight, and there are no
// ghosts.

#include <render/wgpu2d.h>
#include <enemy.h>
#include <functional>
#include <vector>

namespace lastKnown
{
	// A new round: everything forgotten.
	void reset();

	// Once a frame, game time. `seen` says whether the player sees an enemy
	// now, and `cellOf` its sprite's cell in the ship sheet, kept for its
	// ghost after the enemy is gone. The ghosts' spots are checked against
	// the player's sight.
	void update(const std::vector<Enemy> &enemies, const std::function<bool(const Enemy &)> &seen,
		const std::function<glm::vec4(const Enemy &)> &cellOf, float gameDeltaTime);

	// The ghosts, outlined. After the fog's grade, so they keep their colour.
	void draw(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet);

	// Each ghost's spot and how strongly it shows (fading with age), and the
	// ghosts' colour: for the HUD's off-screen arrows, while they are on.
	void forEachGhost(const std::function<void(glm::vec2 position, float alpha)> &visit);
	glm::vec3 ghostColour();
	bool arrowsShown();

	void debugUi();
}
