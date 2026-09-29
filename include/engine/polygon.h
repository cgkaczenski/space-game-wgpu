#pragma once

// Flat polygons: a seeded shape, its triangles, and the hit tests that run on
// them (gameplay roadmap A1). Plain maths, like movement and closingZone: no
// drawing, no idea what the polygon is for. The asteroid is this game's use.
//
// **Star-shaped around the origin.** Every outline made here has one property
// the rest leans on: from the origin, every point of it can be seen. So the
// **triangle fan** from the origin -- triangle i is (origin, outline[i],
// outline[i + 1]) -- covers the polygon exactly, with no overlaps and no gaps,
// and needs no triangulation step at all. Ear clipping is the general
// alternative and waits until a shape can stop being star-shaped.
//
// The same outline is drawn and tested: the fan's triangles for "is this point
// inside", the edges for "where does this ray first touch it". What the player
// sees is exactly what a shot hits.
//
// Everything here works in the polygon's **own frame** -- its outline stored
// around the origin. To test a world point against a placed, rotated polygon,
// move the point into that frame (`toLocal`) rather than moving every corner
// into the world: one point is cheaper than thirty.

#include <glm/vec2.hpp>
#include <cstdint>
#include <vector>

namespace polygon
{
	struct StarParams
	{
		int vertexCount = 28;
		// How far the radius wanders from round, as a fraction of it. The
		// wander is a few smooth waves around the loop plus a little grit per
		// corner.
		float roughness = 0.22f;
		// How unevenly the corners are spaced round the loop, 0 even .. 1 as
		// uneven as can be without two swapping order.
		float angleJitter = 0.6f;
	};

	// A star-shaped outline of about `radius` around the origin, corners in
	// order of increasing angle. The same seed and parameters always give the
	// same shape, on any machine: the randomness is this file's own hash, not
	// the standard library's, whose distributions may differ between
	// implementations.
	std::vector<glm::vec2> starShaped(uint32_t seed, float radius, const StarParams &params = {});

	// The furthest corner from the origin: a circle that holds the polygon,
	// for ruling pairs out before testing triangles.
	float boundingRadius(const std::vector<glm::vec2> &outline);

	// Where a polygon is: its origin in the world, and its turn in radians.
	struct Placement
	{
		glm::vec2 position = {};
		float angle = 0.f;
	};
	glm::vec2 toLocal(const Placement &placement, glm::vec2 worldPoint);
	glm::vec2 toWorld(const Placement &placement, glm::vec2 localPoint);
	// A direction, turned but not moved.
	glm::vec2 directionToLocal(const Placement &placement, glm::vec2 worldDirection);

	// All in the polygon's own frame.

	// Inside one of the fan's triangles.
	bool contains(const std::vector<glm::vec2> &outline, glm::vec2 point);

	// A circle touches it: its centre is inside, or an edge is within reach.
	bool overlapsCircle(const std::vector<glm::vec2> &outline, glm::vec2 centre, float radius);

	// How far along a ray from `origin` (unit `direction`) it first touches an
	// edge, or -1 if not within `maxDistance`. 0 if the ray starts inside.
	float raycast(const std::vector<glm::vec2> &outline, glm::vec2 origin, glm::vec2 direction,
		float maxDistance);

	// The segment from `a` to `b` touches it: either end inside, or it crosses
	// an edge. A line of sight through a rock is blocked.
	bool touchesSegment(const std::vector<glm::vec2> &outline, glm::vec2 a, glm::vec2 b);
}
