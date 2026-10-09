#pragma once

// Hints on screen (hints roadmap H1): a speech bubble pointing at a place in
// the level or at a HUD element, with the keys to press drawn as key-caps,
// and the HUD element highlighted.
//
// The bubble is wgpu2d's (drawCallout). What is here is this game's: the
// hint colour, the size, turning an action's bindings into key-caps, and
// where hints sit in the frame -- over the HUD, under the menu, not shaken.
//
// H1 only draws. Which hint shows when, and what finishes it, are H2's
// scripts; until then the debug panel's Hints section shows test hints.

#include <render/wgpu2d.h>
#include <controls.h>
#include <hud.h>
#include <string>

namespace hints
{
	// The key-caps for `action`, from its bindings as they are now:
	// "[4] OR [mouse:wheel]", lit while the action is held. A modified
	// binding reads "[CTRL] + [mouse:wheel]".
	std::string keys(controls::Action action);

	// For the next draw. `markup` is drawCallout's: text, [KEY] caps,
	// [mouse:left] icons, '\n' between lines.
	//
	// At a place in the level: the tail ends on `world`, and a ring of
	// `ringRadius` world units pulses round it (0: no ring).
	void atWorld(glm::vec2 world, const std::string &markup, float ringRadius = 0.f);
	// At a HUD element: the tail ends on its edge nearest the screen's
	// centre, and the element is highlighted.
	void atHud(hud::Element element, const std::string &markup);

	// The test hints, if any are switched on in the debug panel. Call before
	// hud::draw, so their highlights reach it. `ship` and `facing` place the
	// world test ahead of the ship.
	void debugFrame(glm::vec2 ship, glm::vec2 facing);

	// Draws this frame's hints in screen space and forgets them. `view` is
	// the world camera's visible rectangle (getViewRect) -- read before the
	// HUD pushes its own camera. Call after the HUD and the map, before the
	// menu.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec4 view, int width, int height);

	void debugUi();
}
