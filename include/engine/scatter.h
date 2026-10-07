#pragma once

// Scattering things through a region -- rocks through an asteroid field
// (gameplay roadmap A1b), but nothing here knows what they are.
//
// **One per grid cell, decided by the cell.** The region is covered by a grid
// of cells. Each cell whose chosen spot lands inside the region gets at most
// one item, and everything about it -- whether it is there, where in the
// cell, how big, its own seed -- comes from a hash of the scatter's seed and
// the cell's coordinates. No cell depends on another in its own grid.
//
// That is what makes it stable under editing: grow the region and the new
// cells fill, and every item already placed stays exactly where it was.
// Changing the seed reshuffles everything. A dart-throwing (Poisson disk)
// scatter spaces things more evenly, but each item depends on the ones placed
// before it, so painting one more patch would move the lot.
//
// **In layers, coarse to fine.** One grid alone leaves big gaps: its cells
// must fit its largest item, so small items sit alone in cells made for big
// ones. So there are several grids. The first holds the largest items in big
// cells, and each after it holds items half the size in cells half the size,
// filling the spaces between -- skipping any spot that would come within
// `gap` of an item a coarser layer already placed. A layer depends only on
// its own cells and the layers above it, so growing the region still leaves
// every item where it was; at most, a few small ones next to a newly painted
// edge give way to a big one that arrives there.
//
// **Each item keeps its own clearance.** A single gap for everything makes
// every spacing the same once the layers have filled in, which reads as a
// grid. So each item draws a clearance from its cell's hash, up to `gap`, and
// two items keep the average of theirs apart -- some pairs nearly touch, some
// sit a full gap apart, and none further than `gap` is the cap. Within a
// layer, an item, half its clearance and its offset from the cell centre all
// fit inside the cell, which is what keeps neighbours in one grid apart.

#include <glm/vec2.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace scatter
{
	struct Params
	{
		uint32_t seed = 1;
		float maxRadius = 400.f;
		float minRadius = 60.f;
		// The most clear space an item keeps round itself, edge to edge; two
		// items keep the average of their clearances apart.
		float gap = 40.f;
		// How much clearances vary: 0, every item keeps the full gap; 1, each
		// keeps anywhere from none to the full gap.
		float gapVariation = 1.f;
		// How many grids: 1 is a single grid of sizes min..max. Each layer
		// after the first halves the size; the last reaches down to min.
		int layers = 3;
		// Within a layer, how strongly sizes lean small: a radius is
		// lo + (hi - lo) * u^bias for u uniform in 0..1. 1 is even.
		float smallBias = 1.5f;
		float fill = 1.f;             // the chance a cell tries for an item at all

		// Clumps: a smooth density drifting across the region, from the seed
		// (value noise). Where it is low, cells go empty; where it is high,
		// they fill and items keep less clearance, so they pack. 0 is even
		// all over. `clumpSize` is how far apart the clumps are, in world
		// units. Read at each cell's centre, so it stays stable under editing.
		//
		// This game's hand-painted fields leave it at 0: a painted area means
		// "rocks here", and an empty clump gap inside one would say otherwise.
		// It is kept for procedural levels, where `density` can decide where
		// to paint in the first place.
		float clumping = 0.f;
		float clumpSize = 3000.f;

		// Circles nothing may be placed in: an item keeps its radius plus its
		// clearance from each. For something already standing in the region
		// -- an asteroid field's core.
		struct KeepOut { glm::vec2 centre; float radius; };
		std::vector<KeepOut> keepOut;
	};

	// The clump density at a point, 0 .. 1. Exposed so a caller can show it.
	float density(glm::vec2 point, uint32_t seed, float clumpSize);

	struct Item
	{
		glm::vec2 position = {};
		float radius = 0.f;
		float clearance = 0.f;        // the space it keeps round itself
		uint32_t seed = 0;            // for the item's own shape
	};

	// Items for every cell overlapping the rectangle from `boundsMin` to
	// `boundsMax` whose spot is `inside` the region.
	std::vector<Item> scatter(glm::vec2 boundsMin, glm::vec2 boundsMax,
		const std::function<bool(glm::vec2)> &inside, const Params &params);

	// **A piece of a region, exactly as the whole would have it** (sight
	// roadmap W3): the items whose position is in [partMin, partMax), the
	// same ones, in size and place and seed, as scattering the whole region
	// and keeping those. So a big region can be scattered a chunk at a time,
	// dropped, and scattered again identically.
	//
	// A cell's own item depends on nothing but the cell; whether it is kept
	// depends on the coarser layers' items within `dependencyReach` of it,
	// and theirs on the layers above them. So the part is scattered with
	// that margin round it, then cut to the part.
	std::vector<Item> scatterPart(glm::vec2 partMin, glm::vec2 partMax,
		const std::function<bool(glm::vec2)> &inside, const Params &params);

	// How far round a part the scatter has to look so every item in it comes
	// out as the whole region's would: one coarse cell -- the furthest a
	// coarser item can reach a finer one -- for each layer of the chain.
	float dependencyReach(const Params &params);
}
