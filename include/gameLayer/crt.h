#pragma once

// A CRT filter over the whole finished picture: the game and the HUD go
// through it, the debug UI does not.
//
// This is the first consumer of `wgpu2d::setFinalEffect`, and the split is the
// usual one. The mechanism -- routing the frame through a target and
// compositing it through a shader at the moment something first draws to the
// surface -- is the library's. Which shader, and what its knobs are set to, is
// this game's, and lives in resources/shaders/crt.wgsl where it can be edited.
//
// Almost none of a CRT needs neighbouring pixels. Curvature, scanlines, the
// aperture mask, fringing and the vignette are all arithmetic on one sample at
// a coordinate the shader picks. The one part that is a real convolution is the
// phosphor glow, and that is roadmap N4 step two, deliberately not here: the
// linear filtering the curved sample already uses supplies much of the same
// softness for nothing.

#include <render/wgpu2d.h>

namespace crt
{
	// Loads and compiles the effect. Call once from initGame, after the
	// renderer exists. False if the file is missing or does not compile.
	bool init();

	// Hands the current settings to the renderer, or clears the effect when
	// the filter is off. Call once a frame, before drawing: the sliders are
	// read here, so a change shows up on the next frame.
	void apply();

	void setEnabled(bool enabled);
	bool isEnabled();

	// This feature's own debug controls (roadmap R11).
	void debugUi();
}
