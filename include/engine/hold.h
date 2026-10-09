#pragma once

// A packing grid (inventory roadmap I1): a rectangle of squares, and pieces
// of any shape laid in it, each turned a quarter at a time, never two in one
// square. The hold of a ship; a backpack in another game.
//
// It knows squares and shapes and nothing about what a piece is. The caller
// names each piece with an id of its own and keeps what the id means.

#include <glm/vec2.hpp>
#include <vector>

namespace hold
{
	// The squares a piece covers, as offsets from its top-left. Any shape;
	// the offsets need not start at 0, `turned` normalises them.
	struct Shape
	{
		std::vector<glm::ivec2> cells;
	};

	// `shape` turned clockwise `quarterTurns` times (0-3), moved so its
	// smallest x and y are 0.
	Shape turned(const Shape &shape, int quarterTurns);

	// The width and height of the box round a shape's squares.
	glm::ivec2 extent(const Shape &shape);

	struct Piece
	{
		int id = -1;
		Shape shape;          // as laid: already turned
		int turns = 0;        // how far it was turned from its own shape
		glm::ivec2 at = {};   // the turned shape's top-left square
	};

	struct Grid
	{
		int width = 0;
		int height = 0;
		std::vector<int> cells;     // each square: the id of the piece on it, or -1
		std::vector<Piece> pieces;
	};

	Grid make(int width, int height);

	// Whether `shape`, turned `turns` times, fits with its top-left at `at`:
	// inside the grid and on empty squares. Squares under `ignoring` count
	// as empty -- the piece being moved.
	bool fits(const Grid &grid, const Shape &shape, int turns, glm::ivec2 at, int ignoring = -1);

	// Lays a piece; false, and nothing changes, if it does not fit. An id
	// already in the grid is moved instead.
	bool place(Grid &grid, int id, const Shape &shape, int turns, glm::ivec2 at);

	// Takes a piece out. False if it was not there.
	bool remove(Grid &grid, int id);

	const Piece *find(const Grid &grid, int id);

	// The first place `shape` fits, reading the grid as text is read -- top
	// row first, left to right -- trying each turn at each square before
	// moving on. False if it fits nowhere.
	bool findSpot(const Grid &grid, const Shape &shape, glm::ivec2 &at, int &turns);

	int freeSquares(const Grid &grid);

	// The id on a square, or -1 (empty, or outside the grid).
	int at(const Grid &grid, glm::ivec2 square);
}
