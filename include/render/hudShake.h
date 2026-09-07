#pragma once

// A damage shake for the HUD: this game's numbers, on top of the general
// layer effect in render/layerEffect.h.
//
// Everything general moved there -- the target, the round trip, the draw back.
// What is left here is policy: how hard a hit shakes, how fast that decays,
// how fast it oscillates, how far it tips, and that taking damage is what
// starts it. Another game wants a layer effect; it does not want these numbers.
//
// This file is still under src/render/ only because engine/gameLayer has
// nowhere for it yet. Roadmap R4 moves it into the HUD module, where it
// belongs -- it is the standing counter-example in AGENTS.md and it stops
// being one then.

#include <render/wgpu2d.h>

namespace render
{
	// Starts (or reinforces) the shake. Called by the game the moment the
	// player takes damage, because nothing in the render layer can see that
	// happen. `strength` 1 is a full hit.
	void hudShakeTrigger(float strength = 1.f);

	// Flushes everything recorded since the last flush -- the HUD -- through
	// the shake target, then records one quad that draws that target back to
	// the screen. The caller's next flush puts it on screen, on top of the
	// world it flushed earlier.
	//
	// `width` and `height` are the framebuffer size, the same values the game
	// passes to updateWindowMetrics; the target follows them.
	void hudShakeFlush(wgpu2d::Renderer2D &renderer, int width, int height);
}
