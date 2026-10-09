#include <engine/hold.h>

#include <algorithm>
#include <glm/glm.hpp>

namespace hold
{

Shape turned(const Shape &shape, int quarterTurns)
{
	Shape out = shape;
	const int q = ((quarterTurns % 4) + 4) % 4;
	for (glm::ivec2 &c : out.cells)
	{
		// Clockwise on a y-down grid: (x, y) -> (-y, x).
		for (int k = 0; k < q; k++) { c = {-c.y, c.x}; }
	}
	if (out.cells.empty()) { return out; }
	glm::ivec2 low = out.cells[0];
	for (const glm::ivec2 &c : out.cells) { low = glm::min(low, c); }
	for (glm::ivec2 &c : out.cells) { c -= low; }
	return out;
}

glm::ivec2 extent(const Shape &shape)
{
	glm::ivec2 high = {-1, -1};
	for (const glm::ivec2 &c : shape.cells) { high = glm::max(high, c); }
	return high + glm::ivec2(1);
}

Grid make(int width, int height)
{
	Grid g;
	g.width = std::max(width, 0);
	g.height = std::max(height, 0);
	g.cells.assign((size_t)g.width * g.height, -1);
	return g;
}

int at(const Grid &g, glm::ivec2 s)
{
	if (s.x < 0 || s.y < 0 || s.x >= g.width || s.y >= g.height) { return -1; }
	return g.cells[(size_t)s.y * g.width + s.x];
}

bool fits(const Grid &g, const Shape &shape, int turns, glm::ivec2 where, int ignoring)
{
	const Shape t = turned(shape, turns);
	if (t.cells.empty()) { return false; }
	for (const glm::ivec2 &c : t.cells)
	{
		const glm::ivec2 s = where + c;
		if (s.x < 0 || s.y < 0 || s.x >= g.width || s.y >= g.height) { return false; }
		const int id = g.cells[(size_t)s.y * g.width + s.x];
		if (id != -1 && id != ignoring) { return false; }
	}
	return true;
}

bool remove(Grid &g, int id)
{
	const auto it = std::find_if(g.pieces.begin(), g.pieces.end(), [&](const Piece &p) { return p.id == id; });
	if (it == g.pieces.end()) { return false; }
	for (int &c : g.cells) { if (c == id) { c = -1; } }
	g.pieces.erase(it);
	return true;
}

bool place(Grid &g, int id, const Shape &shape, int turns, glm::ivec2 where)
{
	if (!fits(g, shape, turns, where, id)) { return false; }
	remove(g, id); // a move: out of where it was, into where it goes
	Piece p;
	p.id = id;
	p.shape = turned(shape, turns);
	p.turns = ((turns % 4) + 4) % 4;
	p.at = where;
	for (const glm::ivec2 &c : p.shape.cells)
	{
		const glm::ivec2 s = where + c;
		g.cells[(size_t)s.y * g.width + s.x] = id;
	}
	g.pieces.push_back(p);
	return true;
}

const Piece *find(const Grid &g, int id)
{
	for (const Piece &p : g.pieces) { if (p.id == id) { return &p; } }
	return nullptr;
}

bool findSpot(const Grid &g, const Shape &shape, glm::ivec2 &where, int &turns)
{
	for (int y = 0; y < g.height; y++)
	{
		for (int x = 0; x < g.width; x++)
		{
			for (int q = 0; q < 4; q++)
			{
				if (fits(g, shape, q, {x, y})) { where = {x, y}; turns = q; return true; }
			}
		}
	}
	return false;
}

int freeSquares(const Grid &g)
{
	return (int)std::count(g.cells.begin(), g.cells.end(), -1);
}

}
