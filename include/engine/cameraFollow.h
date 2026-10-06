#pragma once

// Chase a point with a camera: dead zone, leash, and easing as it closes.
//
// This was `wgpu2d::Camera::follow`, a method on a drawing library's camera.
// It draws nothing. It is position maths — the same kind of thing as the
// movement integrator — and it was a method only because gl2d put it there.
//
// It lives under gameLayer/ because `src/engine/` does not exist yet. Roadmap
// R7 creates it and moves this in beside collision; that is a file move,
// because the signature below already keeps the two apart. **It takes no
// `wgpu2d::Camera`**, deliberately: the moment a behaviour takes the drawing
// library's types, the future engine has to include `wgpu2d.h` and the
// boundary is gone on day one. Plain maths in, plain maths out; the caller
// assigns the result wherever it keeps its camera.

#include <glm/vec2.hpp>

namespace camera
{
	// gl2d's `speed`, `min` and `max`, named for what they do.
	struct FollowParams
	{
		// How far the camera closes on the target per call. The caller scales
		// this by its own delta time — this function has no clock.
		float speed = 0.f;

		// Slack: the camera does not move at all while it is within this of
		// the target, and eases as it approaches (quarter speed inside 2x,
		// half inside 4x). Zero means chase always.
		//
		// Beware the easing's steps with a moving target: one slower than
		// `speed` but faster than a quarter of it keeps crossing them -- the
		// camera falls behind at the slow step and catches up at the fast one,
		// every few frames, and the view jitters. Zero turns the steps off.
		float deadZone = 0.f;

		// Leash: the camera is never allowed to fall further behind than
		// this; past it, it is placed exactly this far back rather than eased.
		// Zero with a zero dead zone therefore means snap to the target.
		float leash = 0.f;
	};

	// The camera's next position. `viewSize` centres the target in the view,
	// so pass the same width and height the projection uses.
	glm::vec2 follow(glm::vec2 current, glm::vec2 target, glm::vec2 viewSize,
		const FollowParams &params);

	// The same, closing a share of the way each moment instead of at a speed:
	// `rate` per second, so a big jump settles as quickly as a small one and
	// nothing is snapped. For a target that leaps -- a view leaning toward
	// the pointer (sight roadmap S4b).
	glm::vec2 ease(glm::vec2 current, glm::vec2 target, glm::vec2 viewSize, float rate,
		float deltaTime);

	// How far to lean the view toward the pointer: `pointerFraction` is the
	// pointer from the screen's centre as a share of half the screen, -1 .. 1
	// on each axis; the lean is that share of half the view, times `share`.
	// Measured on the screen, so moving the view does not move it -- no
	// feedback.
	glm::vec2 pointerLead(glm::vec2 pointerFraction, glm::vec2 viewWorldSize, float share);
}
