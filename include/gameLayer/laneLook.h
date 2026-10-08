#pragma once

// How the lanes look (sight roadmap W4's engine ideas), apart from how they
// move ships (lanes.h):
//
//   - **Streaks** flowing along every lane, both ways, as a lane carries
//     either way: short soft bars of light in additive, scrolled with time,
//     bright enough that FinalGlow (the CRT's phosphor glow) blooms them. The
//     lane the player rides is brighter.
//   - **Speed** read off the player while riding a lane or coasting out of
//     one: the hull stretched along its heading, as the warp-out does, and
//     the camera easing out, as the scope does -- both by how far the ship is
//     past its own top speed.
//   - **A flash on entering** a lane in flight mode: a short pulse of the
//     CRT's zoom blur, the warp-out's streaking from the middle.
//
// Streaks are vertex-coloured triangles on a white pixel, so they need no
// texture of their own.

#include <render/wgpu2d.h>
#include <glm/vec2.hpp>
#include <vector>

namespace level { struct Lane; }

namespace laneLook
{
	bool init();
	void cleanup();
	// A new round: no stretch, no zoom, no flash left over.
	void reset();

	// Once a frame, after the player has moved. `speed` against `ownTopSpeed`
	// (the ship's own, mode and all) is how fast it is going; `riding` is in a
	// lane or coasting out of one. Real time: the camera's clock.
	void update(float speed, float ownTopSpeed, bool riding, float realDeltaTime);

	// The player came into a lane in flight mode: the flash starts.
	void entered();

	float zoomFactor();   // multiplies the camera's zoom: below 1, further out
	float stretch();      // the hull's stretch along its heading; 1 is none
	float flash();        // 0 .. 1: the CRT's zoom blur, on entering

	// The streaks, in the world's camera, under the ships. `riding` is the
	// lane the player is in, or -1. Game time, so a pause stops them.
	void drawStreaks(wgpu2d::Renderer2D &renderer, const std::vector<level::Lane> &lanes, int riding,
		float gameTime);

	void debugUi();
}
