#pragma once

// Walking a line through a grid of square cells, cell by cell (sight roadmap
// S1, shared in S1b). Two grids use it: the painted area's labels
// (region::march) and the rock buckets (spatial::raycast). One walk, so there
// is one to trust.
//
// A straight line crosses the vertical grid lines at evenly spaced distances
// along itself, and the horizontal ones likewise, and it changes cell exactly
// at those crossings. So the walk keeps the distance to the next crossing of
// each kind, steps across whichever is nearer, and adds that kind's spacing to
// it -- a merge of two arithmetic sequences (Amanatides & Woo, 1987). Every
// cell the line touches is visited once, in order, corner clips included, with
// the distance from the start at which the line entered it.

#include <glm/vec2.hpp>
#include <functional>

namespace grid
{
	// Where a grid is: cell (0, 0)'s corner, the cells' size, and how many.
	struct Frame
	{
		glm::vec2 origin = {};
		float cell = 1.f;
		int width = 0;
		int height = 0;

		bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
	};

	// The cell column or row a world coordinate falls in, unclamped.
	int cellOf(float world, float origin, float cell);

	// Walks from `a` to `b`, calling `visit(x, y, distance)` for each cell in
	// order, `distance` being how far from `a` the line entered it. The part
	// outside the grid is reported as one cell outside it (`frame.inside`
	// false): where `a` is, when it starts outside, and where the line
	// leaves, after which the walk ends. A segment that misses the grid is one
	// visit outside it. Return false from `visit` to stop; `walk` then
	// returns true.
	bool walk(const Frame &frame, glm::vec2 a, glm::vec2 b,
		const std::function<bool(int x, int y, float distance)> &visit);
}
