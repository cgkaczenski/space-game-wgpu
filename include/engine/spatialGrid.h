#pragma once

// Circles in buckets: which items might be near a box or along a ray, without
// looking at all of them (sight roadmap S1b). Rocks are this game's use;
// nothing here knows what an item is.
//
// **A uniform grid.** The world is cut into square cells and each item is
// listed in every cell its bounding square overlaps, so a big item is simply
// listed many times. A question about a region looks only at the items listed
// in the cells it covers. Suited to items of similar size -- a field's rocks --
// with a few big ones; widely mixed sizes would want a tree.
//
// **Flat, rebuilt by counting.** Two arrays instead of a vector per cell: count
// how many items land in each cell, prefix-sum the counts into `cellStart`,
// then write each item's index into `items` at its cell's next free slot (a
// counting sort). Cell c's items are items[cellStart[c] .. cellStart[c + 1]).
// A rebuild is linear in the items and allocates only when they outgrow it.
// The bounds come from the items each time, so an item drifting far out
// cannot fall off the grid.
//
// **Each item once per query.** An item listed in several cells would be
// handed out several times by a query covering them; a stamp per item, set to
// the query's number, skips the repeats.
//
// **Rays walk the cells near to far** (engine/gridWalk). The caller can stop
// at the first cell that begins beyond its nearest hit: anything nearer
// contains the hit point, that point lies in a cell entered no later, and the
// item is listed there.

#include <engine/collisionSystem.h>
#include <engine/gridWalk.h>
#include <glm/vec2.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace spatial
{
	struct Grid
	{
		grid::Frame frame;            // where the cells are; width 0 when empty
		std::vector<int> cellStart;   // width * height + 1 offsets into `items`
		std::vector<int> items;       // item indices, grouped by cell

		// Per item: the last query that handed it out. Queries change these,
		// so a query is not const in spirit -- one at a time.
		mutable std::vector<uint32_t> stamp;
		mutable uint32_t query = 0;
	};

	// Lists item i under `circles[i]`, in cells of `cellSize`. Far-flung items
	// make the cells bigger rather than the grid enormous: past a few million
	// cells, the size grows until it fits.
	void build(Grid &grid, const std::vector<collision::Circle> &circles, float cellSize);

	// Each item listed in a cell the box from `min` to `max` overlaps, once.
	// Return false from `visit` to stop.
	void query(const Grid &grid, glm::vec2 min, glm::vec2 max, const std::function<bool(int item)> &visit);

	// Each item listed in a cell the ray from `origin` (unit `direction`)
	// passes within `maxDistance`, once, cell by cell near to far; `cellEntry`
	// is how far along the ray the item's cell begins. Return false to stop.
	void raycast(const Grid &grid, glm::vec2 origin, glm::vec2 direction, float maxDistance,
		const std::function<bool(int item, float cellEntry)> &visit);
}
