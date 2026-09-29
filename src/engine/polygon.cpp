#include <engine/polygon.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace polygon
{

namespace
{
	const float tau = 6.2831853f;

	// A 32-bit integer hash (a common "lowbias32" mix): each input bit flips
	// about half the output bits, so neighbouring seeds give unrelated shapes.
	uint32_t hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU;
		x ^= x >> 15; x *= 0x846ca68bU;
		x ^= x >> 16;
		return x;
	}

	// A stream of numbers from a seed: hash the seed and a counter together.
	struct Random
	{
		uint32_t seed;
		uint32_t counter = 0;
		float next() { return (hash(seed ^ hash(counter++)) >> 8) * (1.f / 16777216.f); } // [0, 1)
		float between(float a, float b) { return a + (b - a) * next(); }
	};

	// z of the 3D cross product: positive if b is counter-clockwise from a
	// (in y-up terms), negative if clockwise, 0 if they line up. The whole of
	// 2D geometry's "which side" questions come down to this sign.
	float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

	// Inside or on a triangle: the point is on the same side of all three
	// edges. Either winding works, because it compares the signs rather than
	// expecting one.
	bool inTriangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c)
	{
		const float d1 = cross(b - a, p - a);
		const float d2 = cross(c - b, p - b);
		const float d3 = cross(a - c, p - c);
		const bool anyNegative = d1 < 0.f || d2 < 0.f || d3 < 0.f;
		const bool anyPositive = d1 > 0.f || d2 > 0.f || d3 > 0.f;
		return !(anyNegative && anyPositive);
	}

	float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b)
	{
		const glm::vec2 ab = b - a;
		const float lengthSquared = glm::dot(ab, ab);
		const float t = lengthSquared > 0.f ? std::clamp(glm::dot(p - a, ab) / lengthSquared, 0.f, 1.f) : 0.f;
		return glm::length(p - (a + ab * t));
	}

	// Where the ray origin + t * direction crosses segment ab, as t; -1 if it
	// does not. Two unknowns (t along the ray, s along the edge) from one
	// vector equation, solved with cross products.
	float rayToSegment(glm::vec2 origin, glm::vec2 direction, glm::vec2 a, glm::vec2 b)
	{
		const glm::vec2 edge = b - a;
		const float denominator = cross(direction, edge);
		if (std::abs(denominator) < 1e-8f) { return -1.f; } // parallel
		const glm::vec2 toA = a - origin;
		const float t = cross(toA, edge) / denominator;
		const float s = cross(toA, direction) / denominator;
		if (t < 0.f || s < 0.f || s > 1.f) { return -1.f; }
		return t;
	}
}

std::vector<glm::vec2> starShaped(uint32_t seed, float radius, const StarParams &params)
{
	Random random{hash(seed)};
	const int n = std::max(params.vertexCount, 3);

	// The smooth part of the wander: a few sine waves round the loop. Each has
	// a *whole number* of periods per turn, so after 2 pi it is back where it
	// started and the outline has no seam where the angle wraps. Higher waves
	// are smaller, so the shape is lumpy rather than spiky.
	constexpr int waves = 5;
	float amplitude[waves];
	float phase[waves];
	for (int k = 0; k < waves; k++)
	{
		amplitude[k] = random.between(0.4f, 1.f) / (float)(k + 2);
		phase[k] = random.between(0.f, tau);
	}
	float amplitudeSum = 0.f;
	for (float a : amplitude) { amplitudeSum += a; }

	std::vector<glm::vec2> outline;
	outline.reserve((size_t)n);
	const float jitter = std::clamp(params.angleJitter, 0.f, 0.95f);
	for (int i = 0; i < n; i++)
	{
		// Uneven spacing, but each corner stays inside its own slice of the
		// turn, so the order never changes -- and in angle order, a positive
		// radius at every corner is exactly what keeps it star-shaped.
		const float angle = tau * ((float)i + random.between(-0.5f, 0.5f) * jitter) / (float)n;

		float wander = 0.f;
		for (int k = 0; k < waves; k++)
		{
			wander += amplitude[k] * std::sin((float)(k + 2) * angle + phase[k]);
		}
		wander /= amplitudeSum; // -1 .. 1
		const float grit = random.between(-1.f, 1.f) * 0.25f;
		const float r = radius * std::clamp(1.f + params.roughness * (wander + grit), 0.35f, 1.6f);

		outline.push_back({std::cos(angle) * r, std::sin(angle) * r});
	}
	return outline;
}

float boundingRadius(const std::vector<glm::vec2> &outline)
{
	float r = 0.f;
	for (const glm::vec2 &p : outline) { r = std::max(r, glm::length(p)); }
	return r;
}

glm::vec2 directionToLocal(const Placement &placement, glm::vec2 v)
{
	// The inverse of a rotation is the rotation by minus the angle.
	const float c = std::cos(placement.angle), s = std::sin(placement.angle);
	return {v.x * c + v.y * s, -v.x * s + v.y * c};
}

glm::vec2 toLocal(const Placement &placement, glm::vec2 worldPoint)
{
	return directionToLocal(placement, worldPoint - placement.position);
}

glm::vec2 toWorld(const Placement &placement, glm::vec2 p)
{
	const float c = std::cos(placement.angle), s = std::sin(placement.angle);
	return placement.position + glm::vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

bool contains(const std::vector<glm::vec2> &outline, glm::vec2 point)
{
	const size_t n = outline.size();
	for (size_t i = 0; i < n; i++)
	{
		if (inTriangle(point, {0.f, 0.f}, outline[i], outline[(i + 1) % n])) { return true; }
	}
	return false;
}

bool overlapsCircle(const std::vector<glm::vec2> &outline, glm::vec2 centre, float radius)
{
	if (contains(outline, centre)) { return true; }
	const size_t n = outline.size();
	for (size_t i = 0; i < n; i++)
	{
		if (distanceToSegment(centre, outline[i], outline[(i + 1) % n]) <= radius) { return true; }
	}
	return false;
}

float raycast(const std::vector<glm::vec2> &outline, glm::vec2 origin, glm::vec2 direction,
	float maxDistance)
{
	if (contains(outline, origin)) { return 0.f; }
	float nearest = -1.f;
	const size_t n = outline.size();
	for (size_t i = 0; i < n; i++)
	{
		const float t = rayToSegment(origin, direction, outline[i], outline[(i + 1) % n]);
		if (t >= 0.f && t <= maxDistance && (nearest < 0.f || t < nearest)) { nearest = t; }
	}
	return nearest;
}

bool touchesSegment(const std::vector<glm::vec2> &outline, glm::vec2 a, glm::vec2 b)
{
	const float length = glm::length(b - a);
	if (length <= 0.f) { return contains(outline, a); }
	return raycast(outline, a, (b - a) / length, length) >= 0.f;
}

}
