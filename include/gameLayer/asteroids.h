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
#include <engine/regionMask.h>
#include <level.h>
#include <functional>
#include <vector>

namespace asteroids
{
	// Loads the rock texture. Call once from initGame, after the renderer
	// exists. False if it failed to load.
	bool init();
	void cleanup();

	// A new round: the level's single rocks, grown from their seeds, and its
	// fields' cores. The fields' own rocks come with `stream`.
	void reset(const std::vector<level::Asteroid> &placed,
		const std::vector<level::AsteroidField> &fields);

	// The field rocks round `viewRect` (x, y, width, height in the world)
	// (sight roadmap W3): a field is made a chunk at a time, for the chunks
	// in and round the view, and dropped once the view is well away; made
	// again, a chunk is the same rocks, with what happened to them kept.
	// Once a frame, after the camera is placed and before anything asks
	// about rocks. The first call of a round makes everything in view.
	void stream(glm::vec4 viewRect);

	// The next `stream` makes everything in view at once, as the first of a
	// round does: after a jump (W5), so the far end's rocks are all there
	// when the flash clears rather than coming in over the next frames.
	void streamAllNext();

	// The Chunks section's debug views, when on: the chunk grid, which are
	// made, waiting or just made or dropped, the rectangles streaming used,
	// and the remembered rocks. In the world's camera, over the fog.
	void drawChunkDebug(wgpu2d::Renderer2D &renderer);

	// Inside any field's painted area: hidden, like tall grass. Answered from
	// the painted area's grid (paintMask), so it agrees with sight.
	bool inField(glm::vec2 point);

	// Every field's painted area as one grid (sight roadmap S1), each cell
	// labelled with the field painted there or -1. Built when a round starts;
	// sight walks lines through it to find where they cross a field's edge.
	const region::Mask &paintMask();

	// How much light a ship in a field keeps: its hull is tinted by this, so
	// in the gaps between rocks it looks in shadow.
	float hiddenShade();

	// Hit tests against this round's rocks, in world units.

	// Which rocks a query looks at (sight roadmap S1). Inside its field's
	// paint, a field rock can be passed by sight and shots; everything else
	// -- a single rock, a core, or a field rock knocked out of the paint -- is
	// solid to them always.
	enum class Which
	{
		All,
		Solid,    // single rocks, cores, and field rocks outside the paint
		InPaint,  // field rocks inside the paint, cores excepted
		Cores,    // the fields' cores alone: what stops a missile (S2)
	};

	// Which rock a circle touches, or -1 if none.
	int hitCircle(glm::vec2 centre, float radius, Which which = Which::All);

	// How far along a ray (unit `direction`) the first rock is, or -1 if none
	// within `maxDistance`. `rock`, when given, is set to which one, or -1.
	// `ignore`, when given, is a rock the ray passes through: the one a ship
	// is over, looking out (S2).
	float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance, int *rock = nullptr,
		Which which = Which::All, int ignore = -1);

	// Each rock of `which` whose outline may reach within `radius` of
	// `centre`, with its outline in world units, corner by corner round it,
	// and its bounding circle: what sight writes into a polar map (S2).
	void outlinesNear(glm::vec2 centre, float radius, Which which,
		const std::function<void(int rock, const std::vector<glm::vec2> &outline, glm::vec2 boundCentre, float bound)> &visit);

	// The same for every rock whose buckets the box from `boxMin` to `boxMax`
	// overlaps: a cone's bounds, for the scope (S4b).
	void outlinesIn(glm::vec2 boxMin, glm::vec2 boxMax, Which which,
		const std::function<void(int rock, const std::vector<glm::vec2> &outline, glm::vec2 boundCentre, float bound)> &visit);

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
	// (unit). Missiles push harder. It hurts the rock too: anything breaks a
	// rock (A4) -- along its cracks, into shards that fly apart, sink into the
	// background out of play, and gather where the rock stood -- but only the
	// beam mines one.
	void shot(int rock, glm::vec2 point, glm::vec2 direction, float damage, bool missile);

	// The beam on `rock` at `point` for `gameDeltaTime`: a slight push -- the
	// rock creeps, it is not shoved -- and mining (A4) -- it wears the rock at `damagePerSecond` (scaled for rock)
	// and the rock sheds its ore as orbs. Only the beam mines; the core
	// neither yields nor glows.
	void beam(int rock, glm::vec2 point, glm::vec2 direction, float damagePerSecond, float gameDeltaTime);

	// An explosion at `at`: rocks near it are shoved outward, less further off.
	void blast(glm::vec2 at, float strength = 1.f);

	// A ram's prow, a circle, moving along `direction`: each rock it touches
	// is struck once per ram. `ramSerial` is that ram's number
	// (ram::Ram::serial), which tells one ram from the next -- any ship's.
	void ram(glm::vec2 centre, float radius, glm::vec2 direction, unsigned ramSerial);

	// ---- Cores ----
	//
	// Each field has a core: a rock bigger than any of the field's own, at the
	// painted area's middle unless dragged in the editor -- and never past the
	// paint: in a narrow field it shrinks to fit. It is what the
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

	// Whether `rock` (an index from hitCircle or raycast) is a field's core --
	// the one rock the beam cannot touch -- and if so, its centre.
	bool isCore(int rock, glm::vec2 *centre = nullptr);

	// Where a field's cores are: where the designer placed them, or, with
	// none placed, one at the painted area's middle (W1). None for a field
	// with no paint.
	std::vector<glm::vec2> fieldCores(const level::AsteroidField &field);

	// How far a field's `core`th core reaches from its centre: `coreScale`
	// times the field's max rock size, shrunk if needed so it lies inside the
	// painted area. 0 for a field with no paint.
	float coreRadius(const level::AsteroidField &field, int core);

	// This round's single rocks, in the world, under the ships -- and under
	// them, what broken rocks became: shards, out of play (A4).
	void draw(wgpu2d::Renderer2D &renderer);

	// This round's field rocks, over the ships.
	void drawFields(wgpu2d::Renderer2D &renderer);

	// How dark a ship at `centre`, of hull `radius`, is in the field rocks'
	// shadows: 0 in the light, up to the shadow's darkness when fully covered
	// (A3). Shadows fall only on ships -- never on the starfield, which is far
	// behind everything -- so the game tints its ships by this instead of any
	// shadow being drawn.
	float shadowOn(glm::vec2 centre, float radius);

	// Small dark debris round each field, in front of everything in the
	// world, moving faster than the world as the camera moves (A3). Never
	// solid: decoration nearer the eye than the play. Call last in the world.
	void drawForeground(wgpu2d::Renderer2D &renderer);

	// The level's placements as they would be drawn, grown each call: the
	// editor's view, where radius, seed and the painted area change under the
	// mouse. Fields show their painted area as a tint of dots.
	void drawPlacements(wgpu2d::Renderer2D &renderer, const std::vector<level::Asteroid> &placed,
		const std::vector<level::AsteroidField> &fields);

	void debugUi();
}
