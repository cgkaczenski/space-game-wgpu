#pragma once

// The level editor (gameplay roadmap L2b). While it is on, the game is not
// played: the round is set aside, and the frame shows the level as data -- the
// start, every placement, every marker and every piece of scenery -- under a
// camera the editor owns, which can pan anywhere and zoom out over the whole
// arena.
//
//   Left click         select what is under the cursor, and drag to move it;
//                      on empty space with a place tool chosen, place one
//   Right click        delete what is under the cursor (not the start)
//   Right drag / WASD  pan
//   Wheel              zoom, about the cursor
//
// It edits a Level and nothing else. Loading, saving and what happens when it
// closes are the game's: the editor's panel asks for them, and the game does
// them, because the game owns the file path and the round.

#include <level.h>
#include <render/wgpu2d.h>

namespace levelEditor
{
	bool active();

	// Starts looking from `cameraPosition` at `zoom`, so opening the editor
	// does not jump the view.
	void open(glm::vec2 cameraPosition, float zoom);
	void close();

	// How a placement is drawn: the game's ship sheet and cells, lent.
	struct Look
	{
		wgpu2d::Texture shipSheet;
		glm::vec4 playerCell;
		glm::vec4 rusherCell;
		glm::vec4 sniperCell;
		float shipSize;
		float enemySize;
	};

	// Real time. Reads the mouse and keys, moves the camera, edits `level`,
	// and sets the renderer's camera. `mouse` is in window pixels.
	void update(level::Level &level, wgpu2d::Renderer2D &renderer,
		glm::vec2 mouse, int width, int height, float realDeltaTime);

	// The resource and gate markers: gold rings and a cyan pair, line widths
	// held on screen at any zoom. Also drawn in play, as debug outlines, until
	// L3 and L5 give them a look.
	void drawMarkers(const level::Level &level, wgpu2d::Renderer2D &renderer, float zoom);

	// The level, over whatever background the caller drew first.
	void draw(const level::Level &level, wgpu2d::Renderer2D &renderer, const Look &look);

	// Where the view is centred, for "test from here".
	glm::vec2 cameraCentre();

	// What the panel asked the game to do this frame.
	enum class Request { None, Save, Reload, Exit, TestHere };
	Request debugUi(level::Level &level, bool unsaved);

	// Edits since the last save or load. The game clears it when it does one.
	bool changed();
	void clearChanged();
}
