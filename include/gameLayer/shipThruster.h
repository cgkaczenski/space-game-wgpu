#pragma once

// The player ship's engine plume: a few soft glows stacked behind the hull,
// drawn additively so they accumulate into a bright core instead of covering
// each other.
//
// This is what additive blending is actually for. The game's other art is
// binary alpha -- every pixel fully opaque or fully clear -- so re-blending it
// changes almost nothing. A plume is the opposite: soft-edged, overlapping,
// and meant to read as light over dark space. The glow texture is generated at
// init rather than loaded, because a radial gradient is a formula, and because
// a radially symmetric sprite needs no rotation to line up with the ship.

#include <render/wgpu2d.h>

namespace thruster
{
	// Builds the glow texture. Call once from initGame, after the renderer
	// exists. False if the texture could not be created.
	bool init();
	void cleanup();

	// A new round: the plume starts cold rather than trailing off from the
	// last one.
	void reset();

	// Draws the plume behind a ship.
	//
	// `facing` is the unit vector the ship points along; the plume goes the
	// other way. `throttle` is 1 while the player is thrusting and 0
	// otherwise -- the easing and the flicker happen in here, so the caller
	// only has to say whether the engine is on.
	//
	// `dt` is seconds. Pass game time, not wall-clock: the ship's own motion
	// is scaled by the debug speed slider, so its exhaust should be too. (The
	// HUD shake deliberately does the opposite, for the opposite reason.)
	//
	// Call this *before* drawing the ship, so the hull covers the end of the
	// plume that is inside it.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec2 shipPos, float shipSize,
		glm::vec2 facing, float throttle, float dt);

	// The plume's easing on its own: `level` moved toward `throttle` by `dt`,
	// rising faster than it falls so the engine catches at once and dies
	// away, and settled to exactly 0 when nearly out. `draw` runs the
	// player's through this; anything else with an engine -- an enemy (P1) --
	// keeps its own level and eases it here.
	float ease(float level, float throttle, float dt);

	// The same plume with no memory: the caller supplies how lit it is and
	// where in its flicker, so any number of things can have one -- a missile's
	// exhaust -- without disturbing the ship's easing. `draw` above is this
	// plus the player's own easing and clock.
	//
	// **The caller must have set `BlendMode::Additive`**, as for bullet glows:
	// setting it inside would break a draw run per missile.
	void drawPlume(wgpu2d::Renderer2D &renderer, glm::vec2 position, float size,
		glm::vec2 facing, float level, float phase, glm::vec4 color);
}
