#pragma once

// Asteroids (gameplay roadmap A1): rocks a level places, each grown from a
// seed into its own shape, textured from a real rock material, and solid.
//
// Solid to shots -- the player's and the enemies', missiles too -- to the
// beam, and to **sight**: an enemy cannot see the player through a rock, so a
// rock is somewhere to hide. Not solid to ships; nothing bumps into one. The
// rule is the same for everything: a shot that touches a rock stops, even one
// fired from on top of it, so a rock is cover to hide in, not a bunker to
// fight from.
//
// **Fields** (A1b) are areas painted in the editor, with rocks scattered
// through them from a seed: many, mostly small, one per grid cell so painting
// more never moves the rocks already there (engine/scatter). Their rocks are
// solid in exactly the same way, and are drawn **over** the ships, so a ship
// in a field is behind them. Anywhere inside a field's painted area -- gaps
// included, like tall grass -- the player is hidden from every enemy, and is
// drawn as an outline over the rocks to say so. The gaps still let shots
// through both ways, and an enemy hit from the grass is alerted, turns, and
// comes looking.
//
// A1 has no motion yet (A2), no lighting (A3), and no breaking (A4). What is
// here is the policy: how big the corners are spaced, how the texture sits,
// and the rim. The shape, the fan and the hit tests are engine/polygon, and
// the triangles are wgpu2d's renderTriangles.

#include <render/wgpu2d.h>
#include <level.h>
#include <vector>

namespace asteroids
{
	// Loads the rock texture. Call once from initGame, after the renderer
	// exists. False if it failed to load.
	bool init();
	void cleanup();

	// A new round: the level's rocks, grown from their seeds, and its fields,
	// scattered.
	void reset(const std::vector<level::Asteroid> &placed,
		const std::vector<level::AsteroidField> &fields);

	// Inside any field's painted area: hidden, like tall grass.
	bool inField(glm::vec2 point);

	// How much light a ship in a field keeps: its hull is tinted by this, so
	// in the gaps between rocks it looks in shadow.
	float hiddenShade();

	// Hit tests against this round's rocks, in world units.

	// A circle touches any rock.
	bool hitsCircle(glm::vec2 centre, float radius);

	// How far along a ray (unit `direction`) the first rock is, or -1 if none
	// within `maxDistance`.
	float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance);

	// A rock lies anywhere on the line from `from` to `to`, ends included.
	bool blocksSight(glm::vec2 from, glm::vec2 to);

	// This round's single rocks, in the world, under the ships.
	void draw(wgpu2d::Renderer2D &renderer);

	// This round's field rocks, over the ships.
	void drawFields(wgpu2d::Renderer2D &renderer);

	// The level's placements as they would be drawn, grown each call: the
	// editor's view, where radius, seed and the painted area change under the
	// mouse. Fields show their painted area as a tint of dots.
	void drawPlacements(wgpu2d::Renderer2D &renderer, const std::vector<level::Asteroid> &placed,
		const std::vector<level::AsteroidField> &fields);

	void debugUi();
}
