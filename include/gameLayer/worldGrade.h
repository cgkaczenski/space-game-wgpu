#pragma once

// A colour grade on the world alone: grey and dim, the HUD untouched. The
// paused look (gameplay roadmap L1), and outside the closing circle (L4).
//
// The outside look is the same grade with a circle: the shader measures each
// pixel's distance from the circle's centre and greys only what is past its
// edge, fading in over a few pixels. The circle is in world units here and
// converted to screen pixels on the CPU, once, the way the cloak places its
// field -- the shader has no camera. So the edge of the safe zone shows as the
// line where colour stops, even when the ring itself is off screen.
//
// Why it goes in before the cloak rather than after. The cloak draws the
// world back through its own shader, straight to the screen, and a second
// full-screen pass would have to read that result -- a second target. But a
// grade is per pixel and the cloak only moves pixels, so the order does not
// change the picture: grading first and bending the graded world is the same
// image as bending first and grading. So the world is flushed into a target
// here, and one screen-covering quad of it goes back into the batch through
// the grade effect (F6's per-quad effects); the cloak then takes that batch
// exactly as it would have taken the world.
//
// The shader lives in resources/shaders/worldGrade.wgsl.

#include <render/wgpu2d.h>
#include <engine/closingZone.h>

namespace worldGrade
{
	bool init();
	void cleanup();

	// Call after the world is drawn and before it is flushed (before
	// cloak::flushWorld). `pauseAmount` 0 .. 1 is how far into the paused look.
	// `safe`, when given, greys everything outside it; a view wholly inside it
	// is left alone. With neither, it does nothing and costs nothing -- the
	// world is not sent through a target at all.
	void apply(wgpu2d::Renderer2D &renderer, float pauseAmount, const zone::Circle *safe,
		int width, int height);

	void debugUi();
}
