#pragma once

// This game's zoom: Ctrl + scroll or -/= to zoom, eased, within limits. The
// wheel alone switches weapons. The easing
// and the clamping are engine/cameraZoom; the bindings, the default and the
// limits are here.
//
// The default is the zoom-out floor: as much of the level as the window allows.
// It follows the floor as the window resizes until the player zooms in.
//
// The limits are not only taste. Zoomed in past 1 the pixel art is magnified
// with nothing gained. Zoomed out, the floor is wherever the enemy despawn
// ring would come into view: past it, the player watches enemies vanish. That
// depends on the framebuffer, so a small window can zoom out further than a
// fullsized one -- which is the point, since a small window is the one that
// cannot see a sniper.

#include <glm/vec2.hpp>

namespace zoomControl
{
	// Reads this frame's input and returns the zoom to draw with.
	//
	// `realDeltaTime` is wall time, deliberately not scaled by game speed: at
	// 1% speed the zoom should still respond. That makes this the second thing
	// on the real clock, after hudShake, and one more reason for R8 to name the
	// two clocks.
	//
	// `framebufferSize` and `despawnDistance` set the zoom-out floor.
	// `despawnDistance` 0 means no ring -- a level is loaded (gameplay roadmap
	// L2) -- and the floor is the fixed farthest zoom.
	float update(float realDeltaTime, glm::vec2 framebufferSize, float despawnDistance);

	void debugUi();
}
