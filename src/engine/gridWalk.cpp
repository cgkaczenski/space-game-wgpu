#include <engine/gridWalk.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace grid
{

int cellOf(float world, float origin, float cell)
{
	return (int)std::floor((world - origin) / cell);
}

bool walk(const Frame &frame, glm::vec2 a, glm::vec2 b,
	const std::function<bool(int x, int y, float distance)> &visit)
{
	const glm::vec2 line = b - a;
	const float length = glm::length(line);
	if (frame.width <= 0 || frame.height <= 0) { return !visit(-1, -1, 0.f); }
	if (length <= 0.f)
	{
		return !visit(cellOf(a.x, frame.origin.x, frame.cell), cellOf(a.y, frame.origin.y, frame.cell), 0.f);
	}
	const glm::vec2 dir = line / length;
	const float inf = std::numeric_limits<float>::infinity();

	// Clip the segment to the grid's rectangle (the slab test): outside it
	// there is nothing to step through.
	const glm::vec2 lo = frame.origin;
	const glm::vec2 hi = frame.origin + glm::vec2((float)frame.width, (float)frame.height) * frame.cell;
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
	if (tEnter >= tLeave) { return !visit(-1, -1, 0.f); } // misses the grid entirely
	if (tEnter > 0.f && !visit(-1, -1, 0.f)) { return true; }

	// The first cell inside. Clamped, because a start exactly on the far edge
	// of the rectangle rounds to one cell past it.
	const glm::vec2 start = a + dir * tEnter;
	int ix = std::clamp(cellOf(start.x, lo.x, frame.cell), 0, frame.width - 1);
	int iy = std::clamp(cellOf(start.y, lo.y, frame.cell), 0, frame.height - 1);
	if (!visit(ix, iy, tEnter)) { return true; }

	// Distances are all measured from `a`: to the next vertical grid line
	// (tMaxX), the next horizontal one (tMaxY), and between two of each
	// (tDelta). A line parallel to an axis never crosses that axis's lines.
	const int stepX = dir.x > 0.f ? 1 : -1;
	const int stepY = dir.y > 0.f ? 1 : -1;
	const float tDeltaX = dir.x != 0.f ? frame.cell / std::fabs(dir.x) : inf;
	const float tDeltaY = dir.y != 0.f ? frame.cell / std::fabs(dir.y) : inf;
	float tMaxX = dir.x != 0.f ? (lo.x + (ix + (stepX > 0 ? 1 : 0)) * frame.cell - a.x) / dir.x : inf;
	float tMaxY = dir.y != 0.f ? (lo.y + (iy + (stepY > 0 ? 1 : 0)) * frame.cell - a.y) / dir.y : inf;

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

		if (!visit(ix, iy, t)) { return true; }
		if (!frame.inside(ix, iy)) { return false; } // left the grid: nothing more out there
	}
}

}
