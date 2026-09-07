#pragma once

// This game's HUD: the health bar, its layout, its textures, and the damage
// shake's feel.
//
// Everything general that used to be tangled up with it lives elsewhere now --
// the layer effect in wgpu2d, the layout maths in glui. What is here is the
// part no other game wants: that there is a health bar, that it sits at 65%
// across and 10% down, that a hit shakes the whole thing and how fast that
// settles.
//
// The test this module exists to pass: adding a second HUD element touches
// this file and nothing else.

#include <render/wgpu2d.h>

namespace hud
{
	// Loads the HUD's textures. Call once from initGame, after the renderer
	// exists. False if a texture failed to load.
	bool init();
	void cleanup();

	// The player took a hit. Gameplay has to say so, because nothing in here
	// can see it happen. `strength` 1 is a full hit; hits stack.
	void onDamage(float strength = 1.f);

	// Draws the HUD and puts it on screen.
	//
	// Flushes whatever the renderer has pending first, on purpose: the HUD is
	// a layer, so anything recorded before this call belongs to the layer
	// underneath it. Skipping that would send the world through the shake's
	// target along with the HUD.
	//
	// `width` and `height` are the framebuffer size, the same values the game
	// passes to updateWindowMetrics.
	void draw(wgpu2d::Renderer2D &renderer, float health, int width, int height);
}
