#include <engine/regionMask.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace region
{

namespace
{
	// Which cell column or row a world coordinate falls in.
	int cellIndex(float world, float origin, float cell)
	{
		return (int)std::floor((world - origin) / cell);
	}
}

Mask build(const std::vector<std::vector<Stamp>> &layers, float cellSize)
{
	Mask mask;
	mask.cell = std::max(cellSize, 1.f);

	// The grid covers every painted stamp. Erasers only cut what is painted,
	// so they never widen it.
	bool any = false;
	glm::vec2 lo = {}, hi = {};
	for (const std::vector<Stamp> &layer : layers)
	{
		for (const Stamp &s : layer)
		{
			if (s.erase || s.radius <= 0.f) { continue; }
			const glm::vec2 a = s.centre - glm::vec2(s.radius), b = s.centre + glm::vec2(s.radius);
			lo = any ? glm::min(lo, a) : a;
			hi = any ? glm::max(hi, b) : b;
			any = true;
		}
	}
	if (!any) { return mask; }

	// Aligned to whole cells from the world's origin, so a cell is the same
	// patch of world whatever else was painted.
	mask.origin = glm::vec2(std::floor(lo.x / mask.cell), std::floor(lo.y / mask.cell)) * mask.cell;
	mask.width = std::max(1, (int)std::ceil((hi.x - mask.origin.x) / mask.cell));
	mask.height = std::max(1, (int)std::ceil((hi.y - mask.origin.y) / mask.cell));
	mask.label.assign((size_t)mask.width * (size_t)mask.height, -1);

	// Each layer in a scratch grid of its own -- its erasers must not cut
	// another layer's paint -- then laid over the labels.
	std::vector<uint8_t> scratch;
	for (int layerIndex = 0; layerIndex < (int)layers.size(); layerIndex++)
	{
		const std::vector<Stamp> &layer = layers[layerIndex];
		int x0 = mask.width, y0 = mask.height, x1 = -1, y1 = -1;
		for (const Stamp &s : layer)
		{
			if (s.erase || s.radius <= 0.f) { continue; }
			x0 = std::min(x0, cellIndex(s.centre.x - s.radius, mask.origin.x, mask.cell));
			y0 = std::min(y0, cellIndex(s.centre.y - s.radius, mask.origin.y, mask.cell));
			x1 = std::max(x1, cellIndex(s.centre.x + s.radius, mask.origin.x, mask.cell));
			y1 = std::max(y1, cellIndex(s.centre.y + s.radius, mask.origin.y, mask.cell));
		}
		x0 = std::max(x0, 0);
		y0 = std::max(y0, 0);
		x1 = std::min(x1, mask.width - 1);
		y1 = std::min(y1, mask.height - 1);
		if (x1 < x0 || y1 < y0) { continue; }

		const int w = x1 - x0 + 1, h = y1 - y0 + 1;
		scratch.assign((size_t)w * (size_t)h, 0);
		for (const Stamp &s : layer)
		{
			if (s.radius <= 0.f) { continue; }
			// Only the cells this stamp's square overlaps, so a stamp costs
			// its own area, not the grid's.
			const int sx0 = std::max(x0, cellIndex(s.centre.x - s.radius, mask.origin.x, mask.cell));
			const int sy0 = std::max(y0, cellIndex(s.centre.y - s.radius, mask.origin.y, mask.cell));
			const int sx1 = std::min(x1, cellIndex(s.centre.x + s.radius, mask.origin.x, mask.cell));
			const int sy1 = std::min(y1, cellIndex(s.centre.y + s.radius, mask.origin.y, mask.cell));
			const float r2 = s.radius * s.radius;
			for (int y = sy0; y <= sy1; y++)
			{
				const float dy = mask.origin.y + (y + 0.5f) * mask.cell - s.centre.y;
				for (int x = sx0; x <= sx1; x++)
				{
					const float dx = mask.origin.x + (x + 0.5f) * mask.cell - s.centre.x;
					if (dx * dx + dy * dy <= r2) { scratch[(size_t)(y - y0) * w + (x - x0)] = s.erase ? 0 : 1; }
				}
			}
		}
		for (int y = 0; y < h; y++)
		{
			for (int x = 0; x < w; x++)
			{
				if (scratch[(size_t)y * w + x])
				{
					mask.label[(size_t)(y + y0) * mask.width + (x + x0)] = (int16_t)layerIndex;
				}
			}
		}
	}
	return mask;
}

int labelOf(const Mask &mask, int x, int y)
{
	if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) { return -1; }
	return mask.label[(size_t)y * mask.width + x];
}

int labelAt(const Mask &mask, glm::vec2 point)
{
	if (mask.width <= 0) { return -1; }
	return labelOf(mask, cellIndex(point.x, mask.origin.x, mask.cell),
		cellIndex(point.y, mask.origin.y, mask.cell));
}

bool march(const Mask &mask, glm::vec2 a, glm::vec2 b,
	const std::function<bool(float distance, int label)> &visit)
{
	const glm::vec2 line = b - a;
	const float length = glm::length(line);
	if (mask.width <= 0 || mask.height <= 0) { return !visit(0.f, -1); }
	if (length <= 0.f) { return !visit(0.f, labelAt(mask, a)); }
	const glm::vec2 dir = line / length;
	const float inf = std::numeric_limits<float>::infinity();

	// Clip the segment to the grid's rectangle (the slab test): outside it
	// everything is -1, so there is nothing to step through out there.
	const glm::vec2 lo = mask.origin;
	const glm::vec2 hi = mask.origin + glm::vec2((float)mask.width, (float)mask.height) * mask.cell;
	float tEnter = 0.f, tLeave = length;
	for (int k = 0; k < 2; k++)
	{
		if (dir[k] == 0.f)
		{
			if (a[k] < lo[k] || a[k] >= hi[k]) { tEnter = inf; }
			continue;
		}
		float t0 = (lo[k] - a[k]) / dir[k], t1 = (hi[k] - a[k]) / dir[k];
		if (t0 > t1) { std::swap(t0, t1); }
		tEnter = std::max(tEnter, t0);
		tLeave = std::min(tLeave, t1);
	}
	if (tEnter >= tLeave) { return !visit(0.f, -1); } // misses the grid entirely
	if (tEnter > 0.f && !visit(0.f, -1)) { return true; }

	// The first cell inside. Clamped, because a start exactly on the far edge
	// of the rectangle rounds to one cell past it.
	const glm::vec2 start = a + dir * tEnter;
	int ix = std::clamp(cellIndex(start.x, lo.x, mask.cell), 0, mask.width - 1);
	int iy = std::clamp(cellIndex(start.y, lo.y, mask.cell), 0, mask.height - 1);
	if (!visit(tEnter, labelOf(mask, ix, iy))) { return true; }

	// Distances are all measured from `a`: to the next vertical grid line
	// (tMaxX), the next horizontal one (tMaxY), and between two of each
	// (tDelta). A line parallel to an axis never crosses that axis's lines.
	const int stepX = dir.x > 0.f ? 1 : -1;
	const int stepY = dir.y > 0.f ? 1 : -1;
	const float tDeltaX = dir.x != 0.f ? mask.cell / std::fabs(dir.x) : inf;
	const float tDeltaY = dir.y != 0.f ? mask.cell / std::fabs(dir.y) : inf;
	float tMaxX = dir.x != 0.f ? (lo.x + (ix + (stepX > 0 ? 1 : 0)) * mask.cell - a.x) / dir.x : inf;
	float tMaxY = dir.y != 0.f ? (lo.y + (iy + (stepY > 0 ? 1 : 0)) * mask.cell - a.y) / dir.y : inf;

	float last = tEnter;
	while (true)
	{
		float t;
		if (tMaxX < tMaxY) { t = tMaxX; ix += stepX; tMaxX += tDeltaX; }
		else { t = tMaxY; iy += stepY; tMaxY += tDeltaY; }
		if (t > length) { return false; }
		// Never backwards: the clamp above can leave the first crossing a
		// hair before the entry.
		t = std::max(t, last);
		last = t;

		const bool inside = ix >= 0 && iy >= 0 && ix < mask.width && iy < mask.height;
		if (!visit(t, inside ? labelOf(mask, ix, iy) : -1)) { return true; }
		if (!inside) { return false; } // left the grid: -1 from here to b
	}
}

}
