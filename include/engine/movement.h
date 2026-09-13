#pragma once

// Intent in, motion out: the one integrator the player and every enemy share
// (roadmap R8).
//
// **It does not know where the intent came from.** A direction of length 0..1
// is all it takes -- keys, an AI policy, a replay, a network packet. That is
// the rule that lets an enemy use the same movement as the player: enemies do
// not have keyboards. Turning keys into an intent, including thrust relative to
// facing, is the caller's business and happens before this.
//
// Plain numbers, like cameraFollow and cameraZoom: no wgpu2d types, no clock.
// The caller passes a delta on whichever clock it means -- for a ship, game
// time.

#include <glm/vec2.hpp>

namespace movement
{
	enum class Mode
	{
		// Velocity is the intent times maxSpeed, this frame. No inertia.
		Instant,

		// The intent accelerates; drag pulls velocity back toward zero. With
		// no intent the body coasts to a stop. Top speed is not a setting but
		// a consequence: acceleration / drag, where the two balance.
		Momentum,
	};

	struct Options
	{
		Mode mode = Mode::Instant;

		// Instant: the speed at full intent. Momentum: a hard cap, 0 for none.
		float maxSpeed = 0.f;

		// Momentum only. Units per second squared at full intent.
		float acceleration = 0.f;

		// Momentum only. Per second: with no intent, speed falls to 1/e of
		// itself in 1/drag seconds. 0 means no drag at all -- the body coasts
		// forever, and only maxSpeed bounds it.
		float drag = 0.f;
	};

	inline Options instant(float speed)
	{
		Options o;
		o.mode = Mode::Instant;
		o.maxSpeed = speed;
		return o;
	}

	inline Options momentum(float acceleration, float drag)
	{
		Options o;
		o.mode = Mode::Momentum;
		o.acceleration = acceleration;
		o.drag = drag;
		return o;
	}

	// The speed full intent settles at: maxSpeed for Instant, acceleration /
	// drag (capped by maxSpeed if set) for Momentum. 0 when unbounded.
	float topSpeed(const Options &options);

	// Advances one body by `deltaTime`. An intent longer than 1 is shortened
	// to 1, so diagonal keys are not faster than straight ones.
	//
	// Momentum is integrated exactly rather than stepped, so the same second
	// of input lands in the same place at 240 fps or at 10.
	void integrate(glm::vec2 &position, glm::vec2 &velocity, glm::vec2 intent,
		const Options &options, float deltaTime);
}
