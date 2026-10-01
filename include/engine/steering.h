#pragma once

// Steering that knows about inertia (gameplay roadmap P1, step 2).
//
// A body with momentum cannot be put anywhere; it can only be pushed. So every
// behaviour here works out the velocity the body *should* have right now, and
// returns the thrust that gets it there -- the one controller underneath them
// all, `matchVelocity`. What differs between behaviours is only the velocity
// they want:
//
//   arrive   head for a point, no faster than it can still stop in the
//            distance left: v = sqrt(2 a d), from v² = 2 a d, the stopping
//            distance of a body braking at a
//   pursue   arrive at where a moving target will be, not where it is
//   orbit    circle a (moving) centre at a radius: along the circle, plus
//            arriving onto the ring, plus the target's own velocity
//
// Each returns a thrust in the world's frame, length 0..1, for a
// movement::Intent. Facing is not steering's business: what a body looks at is
// the caller's choice, which is how something can aim one way and fly another.
//
// Plain numbers, like movement: no game, no clock beyond what is passed in.

#include <engine/movement.h>
#include <glm/vec2.hpp>

namespace steering
{
	struct Params
	{
		// How quickly the controller closes a gap in velocity, in seconds:
		// the gap divided by this is the acceleration it asks for. Shorter is
		// tighter and twitchier; longer is lazier and lags.
		float responseTime = 0.25f;
		// The share of full thrust the braking curve assumes, below 1 so there
		// is thrust left over to steer with while it brakes. Drag helps braking
		// too and is ignored, so a body stops a little short rather than late.
		float brakeShare = 0.6f;
	};

	// The thrust that takes `body`'s velocity toward `desired`: the gap over
	// the response time, plus what holding `desired` against drag costs.
	// Length 0..1.
	glm::vec2 matchVelocity(const movement::Body &body, glm::vec2 desired, const Params &params = {});

	// Toward `target`, braking so as to stop there -- or, if the target moves
	// at `targetVelocity`, to sit on it, moving with it.
	glm::vec2 arrive(const movement::Body &body, glm::vec2 target, glm::vec2 targetVelocity = {},
		const Params &params = {});

	// Where a target at `target` moving at `targetVelocity` will be when
	// something at `from` moving at `speed` gets there -- the time to cover
	// the distance now, at most `maxLead` seconds ahead.
	glm::vec2 predict(glm::vec2 from, float speed, glm::vec2 target, glm::vec2 targetVelocity, float maxLead);

	// Arrive at where the target will be.
	glm::vec2 pursue(const movement::Body &body, glm::vec2 target, glm::vec2 targetVelocity,
		float maxLead, const Params &params = {});

	// Circle `centre` (moving at `centreVelocity`) at `radius`, going round at
	// `speed`, clockwise on screen when `clockwise`. Off the ring it arrives
	// onto it; far off (beyond twice the radius) it pursues, leading the
	// centre by up to `maxLead` seconds. Holding a circle takes a constant
	// pull toward the centre -- speed² / radius -- which is added up front
	// rather than left for the controller to discover by drifting outward.
	glm::vec2 orbit(const movement::Body &body, glm::vec2 centre, glm::vec2 centreVelocity,
		float radius, float speed, bool clockwise, float maxLead = 1.f, const Params &params = {});
}
