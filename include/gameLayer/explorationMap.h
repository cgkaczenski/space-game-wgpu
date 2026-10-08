#pragma once

// A map of what has been seen (sight roadmap W6). A small one in the HUD's
// corner shows the area round the ship; holding M shows the whole level.
//
// **How it remembers.** A render target the size of the map keeps whatever is
// drawn into it until it is cleared. Each frame the player's sight -- S2's fan
// -- is drawn into it at map scale, textured with a *base map* made when the
// round starts: field paint, open space and lanes, in muted colours. So the
// target builds up the real map wherever the player has looked, and stays
// transparent everywhere else. No shader of its own: the fan's texture
// coordinates do the masking.
//
// On screen the explored map is drawn dimmed, what is seen now is drawn over
// it at full brightness (the same fan, this frame's), and on top: the gates
// the player has seen (the exit gold, jump gates violet, a pair's line once
// both ends are known), the closing circle, S4's ghosts, the enemies in sight
// now and the player's arrow. A new round starts black.

#include <render/wgpu2d.h>
#include <engine/visibility.h>
#include <sight.h>
#include <glm/vec2.hpp>
#include <vector>

namespace level { struct Level; }

namespace explorationMap
{
	bool init();
	void cleanup();

	// A new round: the base map made from this round's paint and lanes (after
	// asteroids::reset and lanes::start), and the explored map cleared.
	// `arenaRadius` 0: no level, no map.
	void start(const level::Level &level, float arenaRadius);

	// Draws `seen` into the explored map. Its own flush, into its own target:
	// call while nothing else is waiting to be drawn -- before the frame's
	// first draw.
	//
	// `scopeCone`, while the scope is up (S4b), is mapped too, whole: a sector
	// from `seen`'s origin out to the cone's range, nothing blocking it. The
	// scope surveys -- what it points at stops being black (S7) and shows as
	// seen before -- though only what is truly in sight shows in colour, with
	// its enemies.
	void reveal(wgpu2d::Renderer2D &renderer, const visibility::PolarMap &seen,
		const sight::Look *scopeCone = nullptr);

	// What the map marks, this frame.
	struct Marks
	{
		glm::vec2 player = {};
		glm::vec2 facing = {1.f, 0.f};
		std::vector<glm::vec2> enemies;  // in sight now
		const visibility::PolarMap *seen = nullptr;
	};

	// The corner map, and the whole level while `full`. Screen space, after
	// the HUD. Notes which gates the player can see now, for later.
	void draw(wgpu2d::Renderer2D &renderer, int width, int height, const Marks &marks, bool full);

	// The explored map for the fog's never-visited level (worldGrade): its
	// texture, opaque where the player has looked, and the world rectangle it
	// covers (x, y, width, height). False with no map.
	bool exploredMask(wgpu2d::Texture &texture, glm::vec4 &worldRect);

	// Whether the player has seen jump gate `g` (jumpGates' numbering), so
	// the HUD only points at gates it knows.
	bool jumpGateSeen(int g);

	void debugUi();
}
