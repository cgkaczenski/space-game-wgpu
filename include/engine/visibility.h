#pragma once

// What can be seen from one point: a polar map (sight roadmap S2). Segments
// in, a distance per direction out. Rocks and fields are this game's use;
// nothing here knows what blocks or why.
//
// **A one-dimensional shadow map.** The circle round the viewer is cut into
// slices of equal angle, and each slice holds how far the viewer can see
// along its centre line: the range to begin with, and less wherever a segment
// crosses that line nearer. A segment is written into the slices whose centre
// lines it crosses -- the distance along each is one line intersection -- and
// each slice keeps the nearest. Every edge of an outline written this way
// leaves the outline's silhouette, its far side hidden for free.
//
// **The fan is the truth.** Slice i ends at `corner(i)`, and every corner can
// be seen from the viewer, so the corners make a polygon that is star-shaped
// from there: a triangle fan from the viewer draws it, as A1 draws a rock.
// `sees` asks whether a point is inside that fan -- between the two slices
// round it, on the viewer's side of the straight line joining their corners
// -- so what is drawn and what the rules say are the same shape. Where
// neighbouring slices differ a lot, that line cuts across: a wedge about one
// slice wide at the side of a shadow.
//
// Plain numbers, like regionMask and spatialGrid: no drawing, no clock.

#include <glm/vec2.hpp>
#include <vector>

namespace visibility
{
	struct PolarMap
	{
		glm::vec2 origin = {};
		float range = 0.f;
		// One per slice. Slice i is centred on the angle (i + 0.5) * 2pi / N,
		// measured as atan2 measures it in the world's frame (y down, so
		// clockwise on screen).
		std::vector<float> distance;
	};

	// Every slice at `range`. At least 3 slices.
	void begin(PolarMap &map, glm::vec2 origin, float range, int slices);

	// Something solid from `a` to `b`, in world units: the slices whose centre
	// lines it crosses see no further than it.
	void addSegment(PolarMap &map, glm::vec2 a, glm::vec2 b);

	// Nothing can be seen at all: the viewer is inside something solid.
	void blockAll(PolarMap &map);

	// The unit direction of slice i's centre line, and where it ends.
	glm::vec2 direction(const PolarMap &map, int slice);
	glm::vec2 corner(const PolarMap &map, int slice);

	// The point is inside the fan the corners make: visible.
	bool sees(const PolarMap &map, glm::vec2 point);
}
