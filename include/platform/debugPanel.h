#pragma once

// The debug panel's own pieces: the renderer's numbers, and the frame each
// feature's controls are drawn inside (roadmap R11).
//
// This is app-shell code rather than library code. The stats are wgpu2d's, but
// wgpu2d does not link ImGui and should not have to -- a game taking the
// renderer should not be made to take a UI library with it. The widgets that
// show those stats are ImGui wiring, and ImGui wiring lives in platform.

namespace debugPanel
{
	// The last frame's cost and counts, and the render scale slider. `deltaTime`
	// is the loop's own, smoothed here so it can be read.
	void renderStats(float deltaTime);

	// Draws one feature's controls under a heading, with the heading pushed
	// onto ImGui's ID stack.
	//
	// ImGui identifies a widget by hashing its label with that stack, so two
	// widgets labelled "Strength" in one window are the same widget and the
	// second stops responding. A feature writing its own controls cannot see
	// anyone else's labels; the push is what lets it use short ones anyway.
	void section(const char *name, void (*controls)());
}
