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
// - When the camera is already exactly on the target, `normalize` of a zero
//   vector is NaN. The `len > deadZone` test below is false in that case and
//   the NaN is never read, which is why this has never misbehaved. Preserved
//   rather than fixed: this is a move, and a behaviour change hidden inside
//   one is the hardest kind to find later.
glm::vec2 follow(glm::vec2 current, glm::vec2 target, glm::vec2 viewSize,
	const FollowParams &params)
{
	// The camera's position is the view's top-left, so centring the target
	// means backing off by half a view.
	target.x -= viewSize.x / 2.f;
	target.y -= viewSize.y / 2.f;

	glm::vec2 delta = target - current;
	const float distance = glm::length(delta);
	delta = glm::normalize(delta);

	// Ease as it closes, so the camera settles instead of arriving hard.
	float speed = params.speed;
	if (distance < params.deadZone * 2) { speed /= 4.f; }
	else if (distance < params.deadZone * 4) { speed /= 2.f; }

	if (distance <= params.deadZone) { return current; }

	if (distance > params.leash)
	{
		// Past the leash: placed, not eased, so the target can never outrun it.
		return target - (params.leash * delta);
	}
	return current + delta * speed;
}

}
