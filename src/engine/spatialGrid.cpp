#include <engine/spatialGrid.h>

#include <glm/common.hpp>
#include <algorithm>
#include <cmath>

namespace spatial
{

namespace
{
	// Above this many cells the grid's size is the problem, not the items:
	// bigger cells instead.
	constexpr double maxCells = 4'000'000.0;

	// A fresh query number. On wrapping, every stamp is cleared so an old one
	// cannot match.
	uint32_t nextQuery(const Grid &grid)
	{
		if (++grid.query == 0)
		{
			std::fill(grid.stamp.begin(), grid.stamp.end(), 0u);
			grid.query = 1;
		}
		return grid.query;
	}

	// The cell range a box covers, clamped to the grid. False if it misses.
	bool cellRange(const grid::Frame &f, glm::vec2 min, glm::vec2 max, int &x0, int &y0, int &x1, int &y1)
	{
		x0 = std::max(0, grid::cellOf(min.x, f.origin.x, f.cell));
		y0 = std::max(0, grid::cellOf(min.y, f.origin.y, f.cell));
		x1 = std::min(f.width - 1, grid::cellOf(max.x, f.origin.x, f.cell));
		y1 = std::min(f.height - 1, grid::cellOf(max.y, f.origin.y, f.cell));
		return x0 <= x1 && y0 <= y1;
	}
}

void build(Grid &grid, const std::vector<collision::Circle> &circles, float cellSize)
{
	grid.frame = {};
	grid.cellStart.clear();
	grid.items.clear();
	grid.stamp.assign(circles.size(), 0u);
	grid.query = 0;
	if (circles.empty()) { return; }

	glm::vec2 lo = circles[0].center - glm::vec2(circles[0].radius);
	glm::vec2 hi = circles[0].center + glm::vec2(circles[0].radius);
	for (const collision::Circle &c : circles)
	{
		lo = glm::min(lo, c.center - glm::vec2(c.radius));
		hi = glm::max(hi, c.center + glm::vec2(c.radius));
	}

	float cell = std::max(cellSize, 1.f);
	const double area = (double)(hi.x - lo.x) * (double)(hi.y - lo.y);
	if (area / ((double)cell * cell) > maxCells) { cell = (float)std::sqrt(area / maxCells); }

	grid::Frame &f = grid.frame;
	f.cell = cell;
	f.origin = glm::vec2(std::floor(lo.x / cell), std::floor(lo.y / cell)) * cell;
	f.width = std::max(1, grid::cellOf(hi.x, f.origin.x, cell) + 1);
	f.height = std::max(1, grid::cellOf(hi.y, f.origin.y, cell) + 1);

	// Count, then prefix-sum the counts into where each cell's run starts.
	const size_t cells = (size_t)f.width * (size_t)f.height;
	grid.cellStart.assign(cells + 1, 0);
	for (const collision::Circle &c : circles)
	{
		int x0, y0, x1, y1;
		if (!cellRange(f, c.center - glm::vec2(c.radius), c.center + glm::vec2(c.radius), x0, y0, x1, y1)) { continue; }
		for (int y = y0; y <= y1; y++)
		{
			for (int x = x0; x <= x1; x++) { grid.cellStart[(size_t)y * f.width + x + 1]++; }
		}
	}
	for (size_t i = 0; i < cells; i++) { grid.cellStart[i + 1] += grid.cellStart[i]; }

	// Fill: each item at its cell's next free slot, in item order, so a
	// cell's items stay in index order.
	grid.items.resize((size_t)grid.cellStart[cells]);
	std::vector<int> next(grid.cellStart.begin(), grid.cellStart.end() - 1);
	for (int i = 0; i < (int)circles.size(); i++)
	{
		const collision::Circle &c = circles[i];
		int x0, y0, x1, y1;
		if (!cellRange(f, c.center - glm::vec2(c.radius), c.center + glm::vec2(c.radius), x0, y0, x1, y1)) { continue; }
		for (int y = y0; y <= y1; y++)
		{
			for (int x = x0; x <= x1; x++) { grid.items[(size_t)next[(size_t)y * f.width + x]++] = i; }
		}
	}
}

void query(const Grid &grid, glm::vec2 min, glm::vec2 max, const std::function<bool(int item)> &visit)
{
	int x0, y0, x1, y1;
	if (grid.frame.width <= 0 || !cellRange(grid.frame, min, max, x0, y0, x1, y1)) { return; }
	const uint32_t q = nextQuery(grid);
	for (int y = y0; y <= y1; y++)
	{
		for (int x = x0; x <= x1; x++)
		{
			const size_t c = (size_t)y * grid.frame.width + x;
			for (int k = grid.cellStart[c]; k < grid.cellStart[c + 1]; k++)
			{
				const int item = grid.items[(size_t)k];
				if (grid.stamp[(size_t)item] == q) { continue; }
				grid.stamp[(size_t)item] = q;
				if (!visit(item)) { return; }
			}
		}
	}
}

void raycast(const Grid &grid, glm::vec2 origin, glm::vec2 direction, float maxDistance,
	const std::function<bool(int item, float cellEntry)> &visit)
{
	if (grid.frame.width <= 0) { return; }
	const uint32_t q = nextQuery(grid);
	grid::walk(grid.frame, origin, origin + direction * maxDistance, [&](int x, int y, float entry)
	{
		if (!grid.frame.inside(x, y)) { return true; }
		const size_t c = (size_t)y * grid.frame.width + x;
		for (int k = grid.cellStart[c]; k < grid.cellStart[c + 1]; k++)
		{
			const int item = grid.items[(size_t)k];
			if (grid.stamp[(size_t)item] == q) { continue; }
			grid.stamp[(size_t)item] = q;
			if (!visit(item, entry)) { return false; }
		}
		return true;
	});
}

}
