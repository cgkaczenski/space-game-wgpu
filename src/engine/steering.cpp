#include <engine/steering.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace steering
{

namespace
{
	glm::vec2 clampLength(glm::vec2 v, float most)
	{
		const float length = glm::length(v);
		return length > most && length > 0.f ? v * (most / length) : v;
	}

	// The speed it can go and still stop in `distance`, braking at the share
	// of full thrust the params allow -- and no faster than it can go at all.
	float stoppingSpeed(const movement::Body &body, float distance, const Params &params)
	{
		const float top = movement::topSpeed(body.move);
		const float braking = body.move.mode == movement::Mode::Momentum
			? body.move.acceleration * params.brakeShare : 0.f;
		const float canStop = braking > 0.f ? std::sqrt(2.f * braking * std::max(distance, 0.f)) : top;
		return top > 0.f ? std::min(top, canStop) : canStop;
	}
}

glm::vec2 matchVelocity(const movement::Body &body, glm::vec2 desired, const Params &params)
{
	const movement::Options &move = body.move;
	if (move.mode == movement::Mode::Instant)
	{
		// No inertia to steer: the intent is the velocity, scaled.
		return move.maxSpeed > 0.f ? clampLength(desired / move.maxSpeed, 1.f) : glm::vec2(0.f);
	}
	if (move.acceleration <= 0.f) { return {}; }

	// The acceleration that closes the gap in `responseTime`, plus the drag
	// it would have to beat to hold `desired` once there: at velocity v, drag
	// takes drag * v a second.
	const glm::vec2 wanted = (desired - body.velocity) / std::max(params.responseTime, 0.01f)
		+ desired * move.drag;
	return clampLength(wanted / move.acceleration, 1.f);
}

glm::vec2 arrive(const movement::Body &body, glm::vec2 target, glm::vec2 targetVelocity, const Params &params)
{
	const glm::vec2 to = target - body.position;
	const float distance = glm::length(to);
	const glm::vec2 closing = distance > 1e-3f ? to / distance * stoppingSpeed(body, distance, params) : glm::vec2(0.f);
	const float top = movement::topSpeed(body.move);
	const glm::vec2 desired = targetVelocity + closing;
	return matchVelocity(body, top > 0.f ? clampLength(desired, top) : desired, params);
}

glm::vec2 predict(glm::vec2 from, float speed, glm::vec2 target, glm::vec2 targetVelocity, float maxLead)
{
	const float lead = std::min(glm::distance(from, target) / std::max(speed, 1.f), std::max(maxLead, 0.f));
	return target + targetVelocity * lead;
}

glm::vec2 pursue(const movement::Body &body, glm::vec2 target, glm::vec2 targetVelocity,
	float maxLead, const Params &params)
{
	const glm::vec2 ahead = predict(body.position, movement::topSpeed(body.move), target, targetVelocity, maxLead);
	return arrive(body, ahead, targetVelocity, params);
}

glm::vec2 orbit(const movement::Body &body, glm::vec2 centre, glm::vec2 centreVelocity,
	float radius, float speed, bool clockwise, float maxLead, const Params &params)
{
	const glm::vec2 from = body.position - centre;
	const float r = glm::length(from);

	// Far off, the circle is not the point yet: close in on where the centre
	// is going, and stop at the ring's distance from it.
	if (r > 2.f * radius || r < 1e-3f)
	{
		const glm::vec2 ahead = predict(body.position, movement::topSpeed(body.move), centre, centreVelocity, maxLead);
		const glm::vec2 back = r > 1e-3f ? from / r : glm::vec2(1.f, 0.f);
		return arrive(body, ahead + back * radius, centreVelocity, params);
	}

	// On the way round: along the circle at `speed`, and onto the ring as
	// fast as it can still stop there -- both on top of the centre's own
	// motion, so the circle travels with it.
	const glm::vec2 out = from / r;
	// The world is y-down, so (-y, x) turns clockwise on screen.
	const glm::vec2 around = clockwise ? glm::vec2(-out.y, out.x) : glm::vec2(out.y, -out.x);
	const float offRing = r - radius;
	const float onto = stoppingSpeed(body, std::abs(offRing), params) * (offRing > 0.f ? -1.f : 1.f);
	const float top = movement::topSpeed(body.move);
	glm::vec2 desired = centreVelocity + around * speed + out * onto;
	if (top > 0.f) { desired = clampLength(desired, top); }

	glm::vec2 thrust = matchVelocity(body, desired, params);
	if (body.move.mode == movement::Mode::Momentum && body.move.acceleration > 0.f)
	{
		// The pull a circle needs, up front.
		thrust += -out * (speed * speed / std::max(r, 1.f)) / body.move.acceleration;
	}
	return clampLength(thrust, 1.f);
}

}
