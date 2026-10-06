#pragma once

// A painted region as a grid of labels, and a walk along a line through it
// (sight roadmap S1). An asteroid field's area is this game's use; nothing here
// knows what a region is for.
//
// **Painted once, then looked up.** A region is circles painted and erased in
// order, in layers. Asking "is this point inside" of the stamps themselves
// means replaying every stamp for every question. `build` replays them once,
// into cells: each layer's stamps in order, the last one covering a cell's
// centre deciding it, and where layers overlap the later layer's index is the
// cell's label. After that a lookup is one division and one read. The grid is
// the truth: its edges are stair steps a cell wide, and everything that asks
// agrees on them.
//
// **Walking a line (a DDA).** A straight line crosses the vertical grid lines
// at evenly spaced distances along itself, and the horizontal ones likewise,
// and it changes cell exactly at those crossings. So `march` keeps the
// distance to the next crossing of each kind, steps across whichever is
// nearer, and adds that kind's spacing to it -- a merge of two arithmetic
// sequences (Amanatides & Woo, 1987). Every cell the line touches is visited
// once, in order, corner clips included, with the distance at which the line
// entered it; the cost is the number of cells, not the line's length over some
// sampling step. What a change of label *means* is the caller's.
//
// Plain numbers, like closingZone and scatter: no drawing, no clock.

#include <glm/vec2.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace region
{
	struct Stamp
	{
		glm::vec2 centre = {};
		float radius = 0.f;
		bool erase = false;
	};

	struct Mask
	{
		glm::vec2 origin = {};       // the world position of cell (0, 0)'s corner
		float cell = 50.f;           // world units per cell, square
		int width = 0;
		int height = 0;
		std::vector<int16_t> label;  // row by row; -1 where nothing is painted
	};

	// Each layer's stamps, applied in order: paint sets a cell, erase clears
	// it, and a cell belongs to the layer when the last of its stamps covering
	// the cell's centre painted. Where layers overlap, the later layer's index
	// wins. Erasing in one layer does not touch another's paint. The grid
	// covers every painted stamp and is aligned to multiples of `cellSize`, so
	// the same world point is in the same cell however the bounds come out.
	Mask build(const std::vector<std::vector<Stamp>> &layers, float cellSize);

	// The label at `point`: a layer's index, or -1 outside the grid or where
	// nothing is painted.
	int labelAt(const Mask &mask, glm::vec2 point);

	// The label of cell (x, y), -1 outside the grid.
	int labelOf(const Mask &mask, int x, int y);

	// Walks from `a` to `b`, calling `visit` for each cell the segment passes
	// through, in order: first the cell `a` is in at distance 0, then each
	// cell entered, with the distance from `a` at which the segment entered
	// it. Outside the grid counts as one cell labelled -1. Return false from
	// `visit` to stop; `march` then returns true.
	bool march(const Mask &mask, glm::vec2 a, glm::vec2 b,
		const std::function<bool(float distance, int label)> &visit);
}
