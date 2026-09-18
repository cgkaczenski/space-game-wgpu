#pragma once

// How much game time a frame is worth, paced to the display.
//
// The jitter this exists for: frames were measured as 2.4, 31, 2.4, 31 ms at a
// steady 60 Hz. The display still showed one image per refresh, but each image
// moved the world by the *measured* time -- 2.4 ms worth, then 31 ms worth --
// so motion lurched although the frame rate averaged 60. The measurement was
// of when the CPU got a drawable, not of when the picture was shown.
//
// Under FIFO presentation (the surface's mode, wgpuContext.cpp) every frame is
// on screen for a whole number of refreshes, at least one. So the step handed
// to the game is rounded to whole refreshes, never fewer than one, and what
// the rounding leaves over is carried into the next frame so game time does
// not drift from wall time. 2.4 then 31 becomes 16.7 then 16.7.
//
// That assumption only holds for a window the display is actually pacing. A
// hidden window is paced by nothing and runs faster than the refresh; rounding
// up to one refresh there would run the game fast. So pacing switches itself
// off while frames are clearly shorter than a refresh.

namespace framePacing
{
	// The game time to simulate for a frame that took `measuredSeconds`, on a
	// display refreshing at `refreshHz` (0 or less means unknown: 60 is used).
	// Also records both for the graph.
	float step(float measuredSeconds, float refreshHz);

	// The last frame's measured time, in seconds, before pacing.
	float lastMeasured();

	// The frame-time graph, the refresh it paces to, and an on/off switch.
	void debugUi();
}
