#include <engine/movement.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace movement
{

glm::vec2 turnToward(glm::vec2 facing, glm::vec2 want, float maxRadians)
{
	// The signed angle from one to the other: atan2 of the cross and the dot.
	const float angle = std::atan2(facing.x * want.y - facing.y * want.x, glm::dot(facing, want));
	const float turn = std::clamp(angle, -maxRadians, maxRadians);
	const float c = std::cos(turn), s = std::sin(turn);
	// Renormalised: repeated small rotations drift off unit length.
	return glm::normalize(glm::vec2(facing.x * c - facing.y * s, facing.x * s + facing.y * c));
}

void step(Body &body, const Intent &intent, float deltaTime)
{
	const float wantLength = glm::length(intent.face);
	if (wantLength > 1e-6f)
	{
		const glm::vec2 want = intent.face / wantLength;
		body.facing = body.turnRate > 0.f ? turnToward(body.facing, want, body.turnRate * deltaTime) : want;
	}

	glm::vec2 thrust = intent.thrust + body.facing * intent.forward;
	const float length = glm::length(thrust);
	if (length > 1.f) { thrust /= length; }
	body.thrust = thrust;
	integrate(body.position, body.velocity, thrust,
		through(body.move, body.medium, glm::length(body.velocity), deltaTime), deltaTime);
}

Options through(const Options &options, const Medium &medium, float speed, float deltaTime)
{
	const float open = topSpeed(options);
	if (medium.topSpeed >= 1.f || open <= 0.f) { return options; }
	const float limit = open * std::max(medium.topSpeed, 0.f);
	float allowed = limit;
	if (medium.settleHalfLife > 0.f && speed > limit)
	{
		// What is left of the excess after this step.
		allowed = limit + (speed - limit) * std::exp2(-deltaTime / medium.settleHalfLife);
	}
	Options o = options;
	o.maxSpeed = o.maxSpeed > 0.f ? std::min(o.maxSpeed, allowed) : allowed;
	if (o.mode == Mode::Instant) { o.maxSpeed = allowed; } // its speed is the cap
	return o;
}

void push(Body &body, glm::vec2 impulse)
{
	body.velocity += impulse / std::max(body.mass, 1e-4f);
}

Contact collide(Body &a, float radiusA, Body &b, float radiusB, float restitution)
{
	Contact contact;
	if (!a.solid || !b.solid) { return contact; } // either passes through
	const glm::vec2 between = b.position - a.position;
	const float distance = glm::length(between);
	const float overlap = radiusA + radiusB - distance;
	if (overlap <= 0.f) { return contact; }
	contact.touched = true;

	// From a toward b; any way at all if they sit exactly on each other.
	const glm::vec2 normal = distance > 1e-4f ? between / distance : glm::vec2(1.f, 0.f);
	const float inverseA = 1.f / std::max(a.mass, 1e-4f);
	const float inverseB = 1.f / std::max(b.mass, 1e-4f);
	const float inverseSum = inverseA + inverseB;

	// Apart: each moves by its share of the overlap, by inverse mass.
	a.position -= normal * (overlap * inverseA / inverseSum);
	b.position += normal * (overlap * inverseB / inverseSum);

	// Closing along the normal? Then one impulse, equal and opposite, that
	// turns the closing speed into restitution times it, separating.
	const float closing = glm::dot(b.velocity - a.velocity, normal);
	if (closing < 0.f)
	{
		const float j = -(1.f + std::clamp(restitution, 0.f, 1.f)) * closing / inverseSum;
		a.velocity -= normal * (j * inverseA);
		b.velocity += normal * (j * inverseB);
		contact.impactSpeed = -closing;
	}
	return contact;
}

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
