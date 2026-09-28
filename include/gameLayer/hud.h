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
#include <bullet.h>

namespace hud
{
	// One weapon slot as the HUD draws it (gameplay roadmap C2). Values, not
	// the weapons module's own type, so the HUD does not depend on it.
	struct WeaponSlot
	{
		BulletStyle style = BulletStyle::Standard;
		float ready = 1.f;   // 0 just fired .. 1 ready
		int ammo = -1;       // -1: unlimited, no pips
		int maxAmmo = -1;
		bool selected = false;
		bool usable = true;
	};

	// Loads the HUD's textures. Call once from initGame, after the renderer
	// exists. False if a texture failed to load.
	bool init();
	void cleanup();

	// A new round: any shake in progress stops. The textures stay.
	void reset();

	// The player took a hit. Gameplay has to say so, because nothing in here
	// can see it happen. `strength` 1 is a full hit; hits stack.
	void onDamage(float strength = 1.f);

	// The way to the extraction gate (gameplay roadmap L5), for the next draw.
	// While `shown` and `target` (screen pixels) is off screen, a chevron sits
	// just inside the screen's edge on the line from the centre, pointing out
	// toward it. It brightens with `pulse` 0 .. 1 -- the gate's own -- in the
	// gate's `colour`.
	void pointTo(bool shown, glm::vec2 target, float pulse, glm::vec3 colour);

	// Draws the HUD and puts it on screen.
	//
	// Flushes whatever the renderer has pending first, on purpose: the HUD is
	// a layer, so anything recorded before this call belongs to the layer
	// underneath it. Skipping that would send the world through the shake's
	// target along with the HUD.
	//
	// `health` and `energy` are 0..1. Energy is the blue bar under the health
	// bar (gameplay roadmap C1). `slots` is the weapon row, drawn centred along
	// the bottom: an icon each, a shade over the part still cooling down, the
	// selected one framed brighter, and ammo as pips underneath.
	//
	// `width` and `height` are the framebuffer size, the same values the game
	// passes to updateWindowMetrics.
	// `ramReady` (0 just used .. 1 ready) is the ram's slot: a shield icon set
	// apart to the left of the weapon row, shaded the same way while it cools.
	void draw(wgpu2d::Renderer2D &renderer, float health, float energy,
		const WeaponSlot *slots, int slotCount, float ramReady, int width, int height);
}
