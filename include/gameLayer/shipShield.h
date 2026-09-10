#pragma once

// A shield bubble around the player ship: a bright rim with a faint fill,
// drawn additively so it glows over whatever is behind it.
//
// It is two draws in two blend modes, and that split is the whole point.
//
// The rim is light, so it is additive: overlapping rings accumulate into a
// bright edge. But **additive cannot darken** -- it can only ever add -- so an
// additive-only bubble makes the hull *brighter*, which reads as light in
// front of the ship rather than glass around it. The tinted-glass half is a
// dark blue disc in ordinary alpha, underneath the rim, because `C*a +
// dst*(1-a)` is the equation that can pull a colour down.
//
// One effect, two blend equations, and neither can do the other's job. They
// still share one additive pipeline with the engine plume (outline 14): the
// pipeline count follows the distinct blend modes, not the effects.
//
// Both textures are generated at init and radially symmetric, so no rotation is
// needed: a ring for the rim, a feathered disc for the glass. The shape lives
// in the texture's alpha; the vertex colour carries intensity for the rim and
// coverage for the glass.
//
// The glass fades out as a hit flares the shield -- an impact blows it bright,
// and a tint over that would only mute the flash -- and it follows the shield
// down, so a lowered bubble takes its glass with it.

#include <render/wgpu2d.h>

namespace shield
{
	// Builds the ring texture. Call once from initGame, after the renderer
	// exists. False if the texture could not be created.
	bool init();
	void cleanup();

	// Whether the shield is up. The visual eases in and out, so this can be
	// flipped freely.
	void setActive(bool active);
	bool isActive();

	// Something struck the shield: flare, and start a ripple travelling out
	// from where it landed.
	//
	// `offsetFromShip` is the impact relative to the ship's centre, not an
	// absolute position — the shield moves with the ship, so the wave has to be
	// anchored to the bubble rather than to the world. Only its *direction* is
	// used: the collision that produced it happened against the hull, which is
	// inside the bubble, and a shell is a surface, so the wave starts where
	// that direction meets it. `strength` 1 is a solid hit.
	//
	// Several hits ripple at once, up to a small limit. Each is its own quad
	// with its own parameters, which is what the per-quad channel made cheap:
	// no array in a uniform, no upper bound baked into a shader.
	void hit(glm::vec2 offsetFromShip, float strength = 1.f);

	// Draws the bubble. Call *after* the ship, so the rim reads as being in
	// front of the hull. `dt` is game time, like the plume's.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec2 shipPos, float shipSize, float dt);

	// This feature's own debug controls (roadmap R11): the panel calls this
	// rather than growing a block per feature.
	void debugUi();
}
