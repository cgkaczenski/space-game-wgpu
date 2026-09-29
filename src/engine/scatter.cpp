#include <engine/scatter.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace scatter
{

namespace
{
	uint32_t hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU;
		x ^= x >> 15; x *= 0x846ca68bU;
		x ^= x >> 16;
		return x;
	}

	// A cell's own number stream: the seed and both coordinates folded in, so
	// neighbouring cells are unrelated and the same cell always gets the same
	// numbers.
	struct CellRandom
	{
		uint32_t state;
		CellRandom(uint32_t seed, int x, int y)
			: state(hash(seed ^ hash((uint32_t)x * 0x8da6b343U ^ hash((uint32_t)y * 0xd8163841U)))) {}
		float next() { state = hash(state + 0x9e3779b9U); return (state >> 8) * (1.f / 16777216.f); }
	};

	// A lattice point's random value, 0 .. 1.
	float latticeValue(uint32_t seed, int x, int y)
	{
		return (hash(seed ^ hash((uint32_t)x * 0x27d4eb2dU ^ hash((uint32_t)y * 0x165667b1U))) >> 8)
			* (1.f / 16777216.f);
	}

	// Value noise: random values at the corners of a square lattice, blended
	// across each square with a smoothstep so the result has no creases along
	// the lattice lines.
	float valueNoise(glm::vec2 p, uint32_t seed)
	{
		const glm::vec2 cell = glm::floor(p);
		const glm::vec2 f = p - cell;
		const glm::vec2 s = f * f * (3.f - 2.f * f); // smoothstep, per axis
		const int x = (int)cell.x, y = (int)cell.y;
		const float a = latticeValue(seed, x, y), b = latticeValue(seed, x + 1, y);
		const float c = latticeValue(seed, x, y + 1), d = latticeValue(seed, x + 1, y + 1);
		return (a + (b - a) * s.x) + ((c + (d - c) * s.x) - (a + (b - a) * s.x)) * s.y;
	}

	// Items already placed, bucketed by a grid as coarse as the first layer's
	// cells. Two items can only come within `gap` of each other if their
	// centres are closer than the largest radius twice plus the gap -- one
	// such cell -- so a candidate need only look in its own bucket and the
	// eight round it.
	struct Placed
	{
		float bucket;
		std::unordered_map<uint64_t, std::vector<size_t>> buckets;

		static uint64_t key(int x, int y) { return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y; }

		void add(const std::vector<Item> &items, size_t index)
		{
			const glm::vec2 p = items[index].position;
			buckets[key((int)std::floor(p.x / bucket), (int)std::floor(p.y / bucket))].push_back(index);
		}

		bool clear(const std::vector<Item> &items, glm::vec2 at, float radius, float clearance) const
		{
			const int bx = (int)std::floor(at.x / bucket), by = (int)std::floor(at.y / bucket);
			for (int y = by - 1; y <= by + 1; y++)
			{
				for (int x = bx - 1; x <= bx + 1; x++)
				{
					const auto found = buckets.find(key(x, y));
					if (found == buckets.end()) { continue; }
					for (size_t i : found->second)
					{
						const float reach = items[i].radius + radius + (items[i].clearance + clearance) * 0.5f;
						const glm::vec2 d = items[i].position - at;
						if (glm::dot(d, d) < reach * reach) { return false; }
					}
				}
			}
			return true;
		}
	};
}

float density(glm::vec2 point, uint32_t seed, float clumpSize)
{
	// Two octaves: broad clumps, and half-size ones at half the weight, so the
	// clumps have ragged edges rather than soft round blobs. Renormalized so
	// the result still spans 0 .. 1.
	const glm::vec2 p = point / std::max(clumpSize, 1.f);
	const uint32_t s = hash(seed ^ 0x68e31da4U);
	const float raw = (valueNoise(p, s) + 0.5f * valueNoise(p * 2.f + glm::vec2(17.3f, 5.1f), hash(s))) / 1.5f;
	// Flattened. Blending lattice values, then averaging two octaves, both
	// pull toward the middle: measured, raw values sit in a bell round 0.5
	// with a spread of about 0.14, and a threshold at 0.3 caught almost
	// nothing. A logistic with that spread is close to the bell's own
	// cumulative curve, so it maps raw values to their rank -- close to even
	// over 0 .. 1 -- and "below t" then means about a fraction t of the area.
	return 1.f / (1.f + std::exp(-12.2f * (raw - 0.5f)));
}

std::vector<Item> scatter(glm::vec2 boundsMin, glm::vec2 boundsMax,
	const std::function<bool(glm::vec2)> &inside, const Params &params)
{
	std::vector<Item> items;
	const float gap = std::max(params.gap, 0.f);
	const float variation = std::clamp(params.gapVariation, 0.f, 1.f);
	const float clumping = std::clamp(params.clumping, 0.f, 1.f);
	const float maxR = std::max(params.maxRadius, 1.f);
	const float minR = std::clamp(params.minRadius, 0.5f, maxR);
	const int layers = std::max(params.layers, 1);

	Placed placed{2.f * maxR + gap};

	for (int layer = 0; layer < layers; layer++)
	{
		// This layer's sizes: the top half of what is left, or all of it for a
		// single layer or the last one.
		const float hi = maxR / std::pow(2.f, (float)layer);
		if (hi < minR) { break; } // already finer than the smallest item
		const bool last = layer == layers - 1 || hi * 0.5f < minR;
		const float lo = last ? minR : hi * 0.5f;

		const float cell = 2.f * hi + gap;
		const uint32_t layerSeed = hash(params.seed + 0x9e3779b9U * (uint32_t)(layer + 1));
		const size_t coarser = items.size(); // everything placed before this layer

		const int x0 = (int)std::floor(boundsMin.x / cell), x1 = (int)std::floor(boundsMax.x / cell);
		const int y0 = (int)std::floor(boundsMin.y / cell), y1 = (int)std::floor(boundsMax.y / cell);
		for (int y = y0; y <= y1; y++)
		{
			for (int x = x0; x <= x1; x++)
			{
				CellRandom random(layerSeed, x, y);

				// The clumps: thin where the density is low, full and packed
				// where it is high. Read at the cell's centre, so the cell's
				// fate still depends on nothing but the seed and the cell.
				const glm::vec2 cellCentre = {((float)x + 0.5f) * cell, ((float)y + 0.5f) * cell};
				float fillHere = params.fill;
				float packing = 1.f;
				if (clumping > 0.f)
				{
					// Below a threshold that rises with clumping, a cell is empty
					// in *every* layer. Thinning each layer by the same fraction
					// did little: the fine layers have 4 and 16 times the cells,
					// and packed the thin parts back in. So clumping is, roughly,
					// how much of the field is gap. A soft edge either side of
					// the threshold keeps the clumps from ending on a hard line.
					const float d = density(cellCentre, params.seed, params.clumpSize);
					const float threshold = clumping * 0.7f; // at 1, about 70% gap
					const float edge = std::clamp((d - threshold) / 0.08f + 0.5f, 0.f, 1.f);
					fillHere *= edge;
					// And the densest parts pack tighter.
					const float dense = std::clamp((d - threshold) / std::max(1.f - threshold, 0.01f), 0.f, 1.f);
					packing = 1.f - 0.8f * clumping * dense;
				}
				if (random.next() >= fillHere) { continue; }

				const float u = random.next();
				const float radius = lo + (hi - lo) * std::pow(u, std::max(params.smallBias, 0.01f));
				const float clearance = gap * packing * (1.f - variation * random.next());

				// However big it came out, what is left of the cell -- less half
				// its clearance -- is how far it may wander from the centre and
				// still keep its share of the space from the next cell's item.
				const float slack = std::max(cell * 0.5f - radius - clearance * 0.5f, 0.f);
				const glm::vec2 at = cellCentre +glm::vec2(random.next() * 2.f - 1.f, random.next() * 2.f - 1.f) * slack;
				if (!inside(at)) { continue; }
				bool blocked = false;
				for (const Params::KeepOut &k : params.keepOut)
				{
					if (glm::distance(at, k.centre) < k.radius + radius + clearance) { blocked = true; break; }
				}
				if (blocked) { continue; }
				// Coarser layers only: this layer's own cells keep their
				// distance by construction, and checking them would make a
				// cell depend on its neighbours.
				if (coarser > 0 && !placed.clear(items, at, radius, clearance)) { continue; }

				items.push_back({at, radius, clearance, hash(random.state ^ 0x51ed270bU)});
			}
		}

		// This layer joins the ones the next must keep clear of.
		for (size_t i = coarser; i < items.size(); i++) { placed.add(items, i); }
	}
	return items;
}

}
