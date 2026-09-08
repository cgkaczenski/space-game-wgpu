#pragma once

// The light a bullet throws, drawn under the bullet sprite.
//
// The sprite stays exactly as it was, in alpha, and keeps the pixel art
// crisp. This adds one capsule of light underneath it, in additive. Going
// additive on the *sprite* was tried and was a poor trade: it replaced the art
// instead of lighting it, and five overlapping trail quads summed to 4x and
// clipped the sprite into a featureless white blob (outline 14). A glow behind
// the art is the version that works -- the art reads, and the light glows.
//
// The texture is a capsule distance field, generated: distance to a line
// segment with a soft falloff. That *is* the trail, so one quad replaces the
// five stacked squares the sprite path uses to fake one.
//
// One texture serves both sides. As with the plume and the shield, the shape
// lives in the texture's alpha and the colour comes from the vertex colour --
// which is why player and enemy differ by a constant here rather than by an
// atlas cell.

#include <render/wgpu2d.h>

namespace bulletGlow
{
	// Builds the capsule texture. Call once from initGame, after the renderer
	// exists. False if the texture could not be created.
	bool init();
	void cleanup();

	// Draws one bullet's glow, centred on its visible length and rotated to
	// its heading.
	//
	// **The caller must have set `BlendMode::Additive` already.** That is not
	// laziness: setting it inside would break a draw run twice per bullet
	// instead of twice per frame. Draw every glow, then every sprite.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec2 position,
		glm::vec2 direction, bool isEnemy);
}
