#include <cameraFollow.h>

#include <glm/geometric.hpp>

namespace camera
{

// Moved from wgpu2d::Camera::follow with its arithmetic unchanged, so the
// camera keeps feeling exactly the way it did. Two notes on what was left
// alone and what was not:
//
// - gl2d ended this function with a disabled anti-jitter branch: it recomputed
//   the delta, compared signs, and had the correction commented out. Every
//   value it produced was unused, so the whole tail is gone. Recording that it
//   existed because the jitter it was aimed at may still be there.
// - gl2d normalized the delta before testing whether it was going to use it,
//   so sitting exactly on the target produced a NaN direction. Nothing ever
//   read it, but only by accident: the dead-zone test happened to return
//   first, and only because deadZone is never negative. The early return now
//   comes before the normalize, which makes the guard the actual reason
//   rather than a coincidence.
glm::vec2 follow(glm::vec2 current, glm::vec2 target, glm::vec2 viewSize,
	const FollowParams &params)
{
	// The camera's position is the view's top-left, so centring the target
	// means backing off by half a view.
	target.x -= viewSize.x / 2.f;
	target.y -= viewSize.y / 2.f;

	const glm::vec2 delta = target - current;
	const float distance = glm::length(delta);

	// Both of these have to precede the normalize below: there is no direction
	// to a zero-length delta, and normalizing it yields NaN.
	if (distance == 0.f) { return current; }
	if (distance <= params.deadZone) { return current; }

	// glm::normalize, not delta / distance: they differ in the last digit
	// (normalize uses an inverse square root) and this is meant to be the same
	// arithmetic as before, only reordered.
	const glm::vec2 direction = glm::normalize(delta);

	// Ease as it closes, so the camera settles instead of arriving hard.
	float speed = params.speed;
	if (distance < params.deadZone * 2) { speed /= 4.f; }
	else if (distance < params.deadZone * 4) { speed /= 2.f; }

	if (distance > params.leash)
	{
		// Past the leash: placed, not eased, so the target can never outrun it.
		return target - (params.leash * direction);
	}
	return current + direction * speed;
}

}
