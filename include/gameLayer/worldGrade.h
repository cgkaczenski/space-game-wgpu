#pragma once

// A colour grade on the world alone: grey and dim, the HUD untouched. The
// paused look (gameplay roadmap L1).
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

namespace worldGrade
{
	bool init();
	void cleanup();

	// Call after the world is drawn and before it is flushed (before
	// cloak::flushWorld). `amount` 0 does nothing and costs nothing; 1 is the
	// full look.
	void apply(wgpu2d::Renderer2D &renderer, float amount, int width, int height);

	void debugUi();
}
