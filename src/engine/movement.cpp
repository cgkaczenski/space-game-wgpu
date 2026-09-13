#include <engine/movement.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace movement
{

float topSpeed(const Options &options)
{
	if (options.mode == Mode::Instant) { return options.maxSpeed; }
	if (options.drag <= 0.f) { return options.maxSpeed; }

	const float balance = options.acceleration / options.drag;
	return options.maxSpeed > 0.f ? std::min(balance, options.maxSpeed) : balance;
}

void integrate(glm::vec2 &position, glm::vec2 &velocity, glm::vec2 intent,
	const Options &options, float deltaTime)
{
	const float intentLength = glm::length(intent);
	if (intentLength > 1.f) { intent /= intentLength; }

	if (options.mode == Mode::Instant)
	{
		velocity = intent * options.maxSpeed;
		position += velocity * deltaTime;
		return;
	}

	const glm::vec2 acceleration = intent * options.acceleration;
	const glm::vec2 startPosition = position;
	const glm::vec2 startVelocity = velocity;

	if (options.drag > 0.f)
	{
		// dv/dt = a - drag * v has an exact solution over a step with constant
		// input: velocity relaxes exponentially toward a / drag. Stepping it
		// instead (v += a dt; v *= decay) settles about 15% slow at 10 fps
		// against 2.5% at 60, which is a ship that handles differently when
		// the frame rate drops. Position is the integral of the same curve.
		const glm::vec2 terminal = acceleration / options.drag;
		const float decay = std::exp(-options.drag * deltaTime);
		const glm::vec2 fromTerminal = velocity - terminal;

		position += terminal * deltaTime + fromTerminal * ((1.f - decay) / options.drag);
		velocity = terminal + fromTerminal * decay;
	}
	else
	{
		// No drag: constant acceleration, also exact.
		position += velocity * deltaTime + acceleration * (0.5f * deltaTime * deltaTime);
		velocity += acceleration * deltaTime;
	}

	if (options.maxSpeed > 0.f)
	{
		const float speed = glm::length(velocity);
		if (speed > options.maxSpeed)
		{
			velocity *= options.maxSpeed / speed;

			// The position above assumed the velocity kept rising past the cap
			// for the whole step. Sitting at the cap, that overshoot is about
			// half of (acceleration * dt^2) per frame, which is a ship 13% faster
			// at 10 fps than at 60. Averaging the start and capped end velocity
			// is exact at the cap and a small, bounded error on the one frame
			// that reaches it.
			position = startPosition + (startVelocity + velocity) * (0.5f * deltaTime);
		}
	}
}

}
