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

	// Inside the polygon -- any simple polygon, star-shaped or not. A ray from
	// the point crosses the outline an odd number of times exactly when the
	// point is inside (the crossing-number, or even-odd, rule). It was a test
	// against the fan's triangles, which only holds while the fan is valid;
	// broken pieces (A4) need not be star-shaped from anywhere in particular.
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

	// ---- Beyond star-shaped (gameplay roadmap A4) ----

	// The polygon's area, signed: positive when its corners run
	// counter-clockwise in y-up terms (clockwise on a y-down screen).
	float signedArea(const std::vector<glm::vec2> &outline);
	glm::vec2 centroid(const std::vector<glm::vec2> &outline);

	// A fan from `apex` covers the polygon exactly: every fan triangle turns
	// the same way, none flat. What "star-shaped from apex" means in practice.
	bool fanWorksFrom(const std::vector<glm::vec2> &outline, glm::vec2 apex);

	// Triangles for any simple polygon, by **ear clipping**: an ear is three
	// neighbouring corners whose triangle turns the polygon's way and holds no
	// other corner. Cut it off, and the rest is a polygon one corner smaller;
	// repeat. O(n²), and fine for rocks of a few dozen corners. Returns corner
	// indices, three per triangle; fewer than n - 2 triangles means it gave up
	// on a polygon that crosses itself.
	std::vector<int> earClip(const std::vector<glm::vec2> &outline);

	// One piece of a Voronoi fracture: the part of the polygon nearer its site
	// than any other. `crack[i]` says whether the edge from points[i] to
	// points[i + 1] is a crack -- a border with a neighbouring piece -- rather
	// than part of the old outline. `neighbour[i]` is that piece's index, or -1.
	struct Piece
	{
		std::vector<glm::vec2> points;
		std::vector<int> neighbour;
	};

	// Breaks the polygon along the Voronoi diagram of `sites`: one piece per
	// site. Each piece is the polygon clipped, one bisector at a time, to the
	// side nearer its own site (Sutherland-Hodgman with half-planes). Every
	// border between two pieces is where cracks show before it breaks.
	std::vector<Piece> voronoiFracture(const std::vector<glm::vec2> &outline,
		const std::vector<glm::vec2> &sites);
}
