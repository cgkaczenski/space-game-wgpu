#pragma once

// The two clocks, named (roadmap R8).
//
// `real` is the frame's wall time. `game` is that scaled by the game speed
// slider. Everything that is the simulation -- ships moving, AI, bullets,
// spawning, health regen, the plume, shield and cloak animating -- takes
// `game`, so the slider slows all of it together. Everything that is looking
// at the simulation -- camera follow, zoom, the frame stats -- takes `real`,
// so it still answers at 1%.
//
// Built once per frame and handed down already scaled. Nothing passes a speed
// multiplier as an argument any more; that was the second convention for one
// concept, and it is how the fire cooldown ended up on the wrong clock.
//
// A third clock exists and is deliberately left alone: the HUD shake reads
// steady_clock itself (hud.cpp), so a stalled frame does not stretch it.

struct FrameTime
{
	float real = 0.f;
	float game = 0.f;
};

namespace gameClock
{
	FrameTime tick(float realDeltaTime);

	// Freeze game time for `seconds` of real time: a hit-stop, the instant of
	// stillness that makes an impact read as landing (the ram, C4b). Real time
	// runs on, so the camera, the shake and the zoom keep moving through it.
	// A second stop during one extends it to the longer of the two.
	void hitStop(float seconds);

	// Holds game time at zero until released: a hit-stop with no end
	// (gameplay roadmap L1). Everything on game time freezes where it is;
	// real time runs on, so the camera, zoom and panel still answer. Not
	// touched by reset -- whether the game is paused is gameState's to say.
	void setPaused(bool paused);

	// A new round: any hit-stop in progress is something that happened, so it
	// does not carry into the next. The speed slider is a setting and stays.
	void reset();

	void debugUi();
}
