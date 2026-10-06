#pragma once

// Line of sight, and what stops a shot (sight roadmap S1): one rule for
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
// Shots follow the same rule along the path they flew this frame, so one can't
// slip through a gap in the rocks at the edge: it stops where it crosses, on
// the field rock there if there is one.
//
// **Every part of it is a selection** in the debug panel's Sight section, so
// the alternatives can be played against each other: today's rules ("Rocks")
// stay one click away, and "Into fields only" is the first proposal, where
// a field hides you but you can see and shoot out of it. The defaults are the
// rule above.
//
// The painted area is a grid (asteroids::paintMask, engine/regionMask); this
// walks lines through it and decides what a change of field means.

#include <render/wgpu2d.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace sight
{
	// `from` can see `to`. Cones, range and hearing are the enemy AI's, on
	// top of this.
	bool clear(glm::vec2 from, glm::vec2 to);

	// How far from `from` toward `to` the line is blocked, or -1 if it is
	// clear all the way.
	float blockedAt(glm::vec2 from, glm::vec2 to);

	// One frame of a shot's flight, from where it was checked last to where
	// it is now. A shot is first checked from its shooter's position, so a
	// ship outside a field cannot fire in by poking its nose across the edge.
	// One fired inside flies out. `radius` is its hitbox's.
	struct Stop
	{
		bool stopped = false;
		glm::vec2 point = {};  // where it stops
		int rock = -1;         // the rock it strikes there, or -1: it bursts on nothing
	};
	Stop shot(glm::vec2 from, glm::vec2 to, float radius);

	// How far a beam from `origin` (unit `direction`) reaches before a rock or
	// an edge on the way into paint stops it, or -1 if nothing does within
	// `reach`. A beam fired inside flies out. `rock` is set to the rock it
	// burns there, or -1. The player's beam mines that rock; enemies' beams
	// follow the same rule.
	float beam(glm::vec2 origin, glm::vec2 direction, float reach, int *rock);

	// The hidden outline: whether it turns to `seenColour` while an enemy can
	// see the player in a field.
	bool warnsWhenSeen();
	glm::vec3 seenColour();

	// The debug views, when switched on: the painted area's grid, and a line
	// from each of `viewers` to `target` -- green while it can see, red past
	// where it is blocked. Call with the world's camera.
	void drawDebug(wgpu2d::Renderer2D &renderer, const std::vector<glm::vec2> &viewers, glm::vec2 target);

	void debugUi();
}
