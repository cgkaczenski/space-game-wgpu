#pragma once

// Line of sight, and what stops a shot (sight roadmap S1, M1): one rule for
// everyone, the player and every enemy alike.
//
// **The rule.** A line is blocked by any single rock, any core, and the edge of
// a field's painted area. Inside the paint, the field's own rocks block nothing:
// a field is a room, and its edge is the wall from both sides. So a level that
// is half field and half open plays the same on either side -- two ships in
// one field see and shoot each other, two outside do, and the edge stands
// between a ship inside and one outside. A field rock knocked out of its paint
// is a rock on its own, solid like any other, until its spring brings it home.
//
// **Weapons do not follow it** (M1). Field edges are for sight alone: shots,
// missiles and beams pass every rock and every edge and stop only at a core --
// except a beam fired in flight mode, which stops at the first rock on its
// line and mines it (shipMode).
//
// **Every part of the sight rule is a selection** in the debug panel's Sight
// section, so the alternatives can be played against each other: today's
// rules ("Rocks") stay one click away, and "Into fields only" is the first
// proposal, where a field hides you but you can see out of it. The defaults
// are the rule above.
//
// The painted area is a grid (asteroids::paintMask, engine/regionMask); this
// walks lines through it and decides what a change of field means.
//
// **What the player sees (S2)** is the same rule, all round the ship at once:
// a polar map (engine/visibility) rebuilt each frame from everything the rule
// says blocks, as segments -- the outlines of the solid rocks in range, and
// the sides of the grid's cells where the rule says an edge stands, seen from
// where the player is. Its fan is what the fog will draw (S3); `playerSees`
// asks the same fan, so what is drawn and what the rules say agree.
//
// A ship over a single rock looks out of it, by default: the rock under it
// does not block its own view, though it still hides the ship. "Blind" is
// the alternative.

#include <render/wgpu2d.h>
#include <engine/visibility.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace sight
{
	// Which way a viewer looks: for seeing out of a field. From inside a
	// field, a line within this cone is not stopped where it leaves the paint
	// -- it sees on past the edge, a shorter way than in the open (the Looking
	// out selections). Outside the cone, and from outside looking in, the edge
	// is a wall as ever. The player's cone follows the aim; an enemy's is its
	// own sight cone. `range` is the viewer's sight range, for "Fraction of
	// range".
	struct Look
	{
		glm::vec2 facing = {1.f, 0.f}; // unit
		float halfAngle = 0.f;         // radians
		float range = 0.f;
		// The player's cone also sees into a field from outside, to the same
		// reach, with the field's own rocks casting shadows there: destroying
		// them opens sight deeper in. Enemies see a field's edge as a wall.
		bool seesIntoFields = false;
		// Past a field's edge, out or in, it reaches all the way to `range`
		// rather than by the Looking out selection: the scope's (S4b).
		bool reachToRange = false;
	};

	// The scope, while it is up (S4b): its cone, how far in it is (0 .. 1),
	// and the radius the all-round sight shrinks to meanwhile.
	struct Scope
	{
		Look cone;
		float amount = 0.f;
		float aroundRadius = 0.f;
	};

	// A circle of sight this frame, apart from the player's own: what the
	// player's beam is burning.
	struct Reveal
	{
		glm::vec2 centre = {};
		float radius = 0.f;
	};

	// `from` can see `to`. Cones, range and hearing are the enemy AI's, on
	// top of this. `look`, when given, is how `from` looks out of a field.
	bool clear(glm::vec2 from, glm::vec2 to, const Look *look = nullptr);

	// How far from `from` toward `to` the line is blocked, or -1 if it is
	// clear all the way.
	float blockedAt(glm::vec2 from, glm::vec2 to, const Look *look = nullptr);

	// A shot, missile or not, whose hitbox is at `at` with `radius`: whether
	// it stops there. Only a core stops it (M1); `rock` is that core.
	struct Stop
	{
		bool stopped = false;
		glm::vec2 point = {};  // where it stops
		int rock = -1;         // the rock it strikes there, or -1
	};
	Stop shot(glm::vec2 at, float radius);

	// How far a beam from `origin` (unit `direction`) reaches before what
	// stops it, or -1 if nothing does within `reach`. `mines` is the flight
	// mode's beam: the first rock on its line, in a field or out. Otherwise
	// only a core stops it. `rock` is set to the rock it ends on, or -1.
	float beam(glm::vec2 origin, glm::vec2 direction, float reach, bool mines, int *rock);

	// What one viewer at `position` sees with `look`, as a polar map over its
	// cone's bounds: the shape of the rule `clear` applies, for drawing an
	// enemy's cone (S6). Slices outside the cone are not meaningful.
	void coneView(glm::vec2 position, const Look &look, visibility::PolarMap &out);

	// ---- What the player sees (sight roadmap S2) ----

	// Rebuilds the player's polar map. Once a frame, after the player has
	// moved and before anything asks `playerSees`. `aim` (unit) is where the
	// player looks out of, and into, a field; the cone turns toward it over a
	// moment. Clears this frame's reveals. Real time, like the camera.
	// `scope`, when given, adds its cone and shrinks the all-round sight.
	void updatePlayer(glm::vec2 position, bool cloaked, glm::vec2 aim, float realDeltaTime,
		const Scope *scope = nullptr);

	// Inside the player's polar map's fan, or a reveal: what the player can
	// see now.
	bool playerSees(glm::vec2 point);
	const visibility::PolarMap &playerMap();

	// What the fog draws: the player's map eased slice by slice over a moment,
	// so rays crossing the grid's stair steps do not make the fog's edge
	// jitter. The rules use the exact map.
	const visibility::PolarMap &playerDrawnMap();

	// A circle of sight for this frame (until the next updatePlayer), and the
	// list of them for the fog to draw.
	void reveal(glm::vec2 centre, float radius);
	const std::vector<Reveal> &reveals();

	// How far round what the player's beam burns it lights (Beam lights); 0
	// for not at all.
	float beamLight();

	// Whether a missile may only lock onto a ship its shooter can see. A lock,
	// once made, holds whatever happens to the sight after.
	bool locksNeedSight();

	// ---- What the fog hides (sight roadmap S3) ----
	//
	// Under the fog the world is greyed, and a grey enemy still says where it
	// is, so what is information is not drawn at all where the player cannot
	// see it. These answer for the player's sight; the caller asks only while
	// there is fog.

	// A ship at `centre`, of hull `radius`, is in sight: its centre or any of
	// eight points round its hull. Once drawn, the part still in the fog
	// comes out grey -- its nose shows first.
	bool playerSeesShip(glm::vec2 centre, float radius);

	// Whether unseen enemies -- hulls and everything that gives one away --
	// are left out (the default) or drawn and greyed (a debug view).
	bool hidesUnseenEnemies();

	// Whether an explosion or a wreck at `point` is drawn: greyed by the fog
	// (the default -- it is how the player learns something died out there),
	// or left out where unseen.
	bool explosionShown(glm::vec2 point);

	// How far along a beam from `start` to `end` the player's sight first
	// reaches it, or -1 if it never does: an unseen ship's beam is drawn from
	// there, so it does not point back at the shooter.
	float beamSeenFrom(glm::vec2 start, glm::vec2 end);

	// The hidden outline: whether it turns to `seenColour` while an enemy can
	// see the player in a field.
	bool warnsWhenSeen();
	glm::vec3 seenColour();

	// The debug views, when switched on: the painted area's grid, a line
	// from each of `viewers` to `target` -- green while it can see, red past
	// where it is blocked -- and the player's polar map. Call with the world's
	// camera.
	struct Viewer
	{
		glm::vec2 position = {};
		Look look;
	};
	void drawDebug(wgpu2d::Renderer2D &renderer, const std::vector<Viewer> &viewers, glm::vec2 target);

	void debugUi();
}
