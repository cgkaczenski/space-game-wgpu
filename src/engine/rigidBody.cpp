#include <engine/rigidBody.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace rigid
{

namespace
{
	float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

	// Sums triangles into mass properties. `next` hands back one triangle's
	// corners at a time, false when there are no more.
	template <typename Next>
	MassProperties sumTriangles(Next next, float density)
	{
		MassProperties m;
		float area = 0.f;
		glm::vec2 weighted = {};
		float momentAboutOrigin = 0.f; // area-weighted second moment, before density
		glm::vec2 a, b, c;
		while (next(a, b, c))
		{
			// Signed: a shape that winds the other way comes out negative on
			// every triangle, and the ratios below do not care.
			const float triangle = 0.5f * cross(b - a, c - a);
			area += triangle;
			weighted += triangle * (a + b + c) / 3.f;
			// A triangle's polar second moment about the origin, per unit
			// density: area / 6 * (a.a + b.b + c.c + a.b + b.c + c.a).
			momentAboutOrigin += triangle / 6.f
				* (glm::dot(a, a) + glm::dot(b, b) + glm::dot(c, c)
					+ glm::dot(a, b) + glm::dot(b, c) + glm::dot(c, a));
		}
		if (std::abs(area) < 1e-6f) { return m; }
		m.area = std::abs(area);
		m.centroid = weighted / area;
		m.mass = density * m.area;
		// Parallel axis theorem, backwards: the moment about the origin is the
		// moment about the centroid plus mass × (distance between them)².
		m.inertia = density * std::abs(momentAboutOrigin) - m.mass * glm::dot(m.centroid, m.centroid);
		return m;
	}
}

MassProperties fromTriangles(const std::vector<glm::vec2> &points, const std::vector<int> &triangles,
	float density)
{
	size_t at = 0;
	return sumTriangles([&](glm::vec2 &a, glm::vec2 &b, glm::vec2 &c)
	{
		if (at + 3 > triangles.size()) { return false; }
		a = points[triangles[at]]; b = points[triangles[at + 1]]; c = points[triangles[at + 2]];
		at += 3;
		return true;
	}, density);
}

MassProperties fromFan(const std::vector<glm::vec2> &outline, glm::vec2 apex, float density)
{
	size_t i = 0;
	const size_t n = outline.size();
	return sumTriangles([&](glm::vec2 &a, glm::vec2 &b, glm::vec2 &c)
	{
		if (i >= n) { return false; }
		a = apex; b = outline[i]; c = outline[(i + 1) % n];
		i++;
		return true;
	}, density);
}

Body makeBody(const MassProperties &p, glm::vec2 position, float angle)
{
	Body b;
	b.position = position;
	b.angle = angle;
	b.inverseMass = p.mass > 0.f ? 1.f / p.mass : 0.f;
	b.inverseInertia = p.inertia > 0.f ? 1.f / p.inertia : 0.f;
	return b;
}

void applyImpulse(Body &body, glm::vec2 worldPoint, glm::vec2 impulse)
{
	body.velocity += impulse * body.inverseMass;
	body.spin += cross(worldPoint - body.position, impulse) * body.inverseInertia;
}

void applyForce(Body &body, glm::vec2 worldPoint, glm::vec2 force, float dt)
{
	applyImpulse(body, worldPoint, force * dt);
}

void integrate(Body &body, float dt, float linearDamping, float angularDamping)
{
	if (dt <= 0.f) { return; }
	body.velocity *= std::exp(-std::max(linearDamping, 0.f) * dt);
	body.spin *= std::exp(-std::max(angularDamping, 0.f) * dt);
	body.position += body.velocity * dt;
	body.angle = std::remainder(body.angle + body.spin * dt, 6.2831853f);
}

bool collideCircles(Body &a, float ra, Body &b, float rb, float restitution, float friction,
	float restingSpeed)
{
	const glm::vec2 between = b.position - a.position;
	const float distance = glm::length(between);
	const float reach = ra + rb;
	if (distance >= reach) { return false; }
	const float totalInverse = a.inverseMass + b.inverseMass;
	if (totalInverse <= 0.f) { return false; }

	const glm::vec2 normal = distance > 1e-4f ? between / distance : glm::vec2(1.f, 0.f);

	// Apart first, shared by lightness: the heavier moves less. Without this,
	// two overlapping bodies with no closing speed would stay stuck together.
	const float overlap = reach - distance;
	a.position -= normal * (overlap * a.inverseMass / totalInverse);
	b.position += normal * (overlap * b.inverseMass / totalInverse);

	// Then the bounce, only if they are still moving together. The impulse
	// size is the one that makes the closing speed along the normal come out
	// as -restitution times what it was.
	const float closing = glm::dot(b.velocity - a.velocity, normal);
	if (closing < 0.f)
	{
		const float bounce = -closing > restingSpeed ? std::clamp(restitution, 0.f, 1.f) : 0.f;
		const float j = -(1.f + bounce) * closing / totalInverse;
		a.velocity -= normal * (j * a.inverseMass);
		b.velocity += normal * (j * b.inverseMass);

		if (friction > 0.f)
		{
			// Where they touch, and how fast the two surfaces slide past each
			// other there: each body's velocity plus its spin at that point
			// (ω × r, which in 2D is ω times r turned a quarter).
			const glm::vec2 contact = a.position + normal * ra;
			const glm::vec2 fromA = contact - a.position, fromB = contact - b.position;
			auto pointVelocity = [](const Body &body, glm::vec2 r)
			{
				return body.velocity + glm::vec2(-body.spin * r.y, body.spin * r.x);
			};
			const glm::vec2 tangent = {-normal.y, normal.x};
			const float sliding = glm::dot(pointVelocity(b, fromB) - pointVelocity(a, fromA), tangent);

			// The impulse that would stop the sliding outright. Along the
			// tangent, r × t is the whole radius, so turning resists it too:
			// the r² / I terms. Capped by friction × the bounce (Coulomb).
			const float resist = totalInverse + ra * ra * a.inverseInertia + rb * rb * b.inverseInertia;
			const float stop = -sliding / resist;
			const float jt = std::clamp(stop, -friction * j, friction * j);
			applyImpulse(a, contact, -tangent * jt);
			applyImpulse(b, contact, tangent * jt);
		}
	}
	return true;
}

}
