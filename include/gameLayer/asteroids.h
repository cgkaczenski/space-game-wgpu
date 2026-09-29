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

	// Which rock a circle touches, or -1 if none.
	int hitCircle(glm::vec2 centre, float radius);

	// How far along a ray (unit `direction`) the first rock is, or -1 if none
	// within `maxDistance`. `rock`, when given, is set to which one, or -1.
	float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance, int *rock = nullptr);

	// A rock lies anywhere on the line from `from` to `to`, ends included.
	bool blocksSight(glm::vec2 from, glm::vec2 to);

	// ---- Physics (gameplay roadmap A2) ----
	//
	// Every rock is a rigid body: pushed where it is hit, so an off-centre hit
	// spins it as well as shoving it. Single rocks drift freely and slowly come
	// to rest. Field rocks spring gently back to where they grew, so a field's
	// painted area stays the truth about where its cover is. Rocks bump each
	// other as circles of their own area; they pass through ships.

	// Game time: moving rocks drift, turn, spring home and bump.
	void update(float gameDeltaTime);

	// A shot of `damage` hit `rock` at `point`, flying along `direction`
	// (unit). Missiles push harder.
	void shot(int rock, glm::vec2 point, glm::vec2 direction, float damage, bool missile);

	// The beam on `rock` at `point` for `gameDeltaTime`: a steady push.
	void beam(int rock, glm::vec2 point, glm::vec2 direction, float gameDeltaTime);

	// An explosion at `at`: rocks near it are shoved outward, less further off.
	void blast(glm::vec2 at, float strength = 1.f);

	// The ram's prow, a circle, moving along `direction`: each rock it
	// touches is struck once per ram. `newRam` on its first frame.
	void ram(glm::vec2 centre, float radius, glm::vec2 direction, bool newRam);

	// ---- Cores ----
	//
	// Each field has a core: a rock bigger than any of the field's own, at the
	// painted area's middle unless dragged in the editor. It is what the
	// field's rocks are seen to fall back toward. It never moves, is never
	// pushed, beamed or broken, and rocks bounce off it. Ships do not pass through it: a ship
	// that touches one is thrown back out and hit, the player's shield
	// blocking it (and breaking) or the hull taking damage.

	struct CoreRules
	{
		float damage = 0.15f;        // to the player's hull, or an enemy's life
		float bounce = 0.6f;         // of the speed into the core that comes back out
		float minOutSpeed = 700.f;   // however gently it was touched
		float grace = 0.5f;          // seconds before touching it hurts again
		float enemyKnock = 900.f;    // how hard an enemy is thrown back out
		float enemyStun = 0.35f;     // and for how long it tumbles
	};
	const CoreRules &coreRules();

	struct CoreContact
	{
		glm::vec2 outward;  // unit, from the core's centre toward the ship
		glm::vec2 pushTo;   // where the ship's centre goes so it just touches
	};

	// Whether a circle -- a ship's hitbox -- touches any core; if so, which
	// way is out, and where to put it.
	bool coreContact(glm::vec2 centre, float radius, CoreContact &out);

	// Where a field's core is: dragged there, or the painted area's middle.
	glm::vec2 fieldCore(const level::AsteroidField &field);

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
