#pragma once

// A damage shake for the HUD, built on milestone 10's render targets.
//
// The HUD is drawn into an offscreen target and that target is drawn back to
// the screen displaced and slightly rotated, so the whole HUD moves as one
// composed image instead of as a pile of separately nudged quads. Rotation is
// the reason the target earns its place: individual quads cannot be rotated
// about a shared centre without shearing their layout apart.
//
// The effect is on the WebGPU path only. On the OpenGL path both calls are
// harmless: the trigger does nothing and the flush is an ordinary flush, so
// that build renders exactly as it did before.

#include <render/renderer.h>

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
	void hudShakeFlush(r2d::Renderer2D &renderer, int width, int height);
}
