#pragma once

// How a bullet looks: a capsule of additive light, and the pixel-art sprite
// on top of it.
//
// Two draws, one picture. The sprite stays in alpha so the art stays crisp.
// The glow is additive underneath it. Going additive on the *sprite* was tried
// and was a poor trade: it replaced the art instead of lighting it, and five
// overlapping trail quads summed to 4x and clipped the sprite into a
// featureless white blob (outline 14). A glow behind the art is the version
// that works -- the art reads, and the light glows.
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

namespace bulletLook
{
	// Builds the capsule texture and loads the bullet sprite sheet. Call once
	// from initGame, after the renderer exists. False if either failed.
	//
	// The sheet is this module's because nothing else draws from it (roadmap
	// R10); the game used to load it and pass it into every draw.
	bool init();
	void cleanup();

	// Draws one bullet's glow, centred on its visible length and rotated to
	// its heading.
	//
	// **The caller must have set `BlendMode::Additive` already.** That is not
	// laziness: setting it inside would break a draw run twice per bullet
	// instead of twice per frame. Draw every glow, then every sprite.
	void drawGlow(wgpu2d::Renderer2D &renderer, glm::vec2 position,
		glm::vec2 direction, bool isEnemy);

	// The art on top of the glow: five overlapping quads along the heading,
	// fading in from the tail. Player and enemy use different cells of the sheet.
	//
	// **The caller must have set `BlendMode::Alpha` already.** Same reason as
	// `drawGlow`: the blend mode is a run break, so it is set once per pass.
	void drawSprite(wgpu2d::Renderer2D &renderer, glm::vec2 position,
		glm::vec2 direction, bool isEnemy);
}
