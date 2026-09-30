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
	// A ray from the point toward +x: count the edges it crosses. Each edge
	// counts if it straddles the point's height (one end above, one not --
	// half-open, so a corner exactly at that height is counted once) and
	// crosses to the right of the point.
	bool inside = false;
	const size_t n = outline.size();
	for (size_t i = 0, j = n - 1; i < n; j = i++)
	{
		const glm::vec2 a = outline[i], b = outline[j];
		if ((a.y > point.y) != (b.y > point.y))
		{
			const float x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (point.x < x) { inside = !inside; }
		}
	}
	return inside;
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

float signedArea(const std::vector<glm::vec2> &outline)
{
	// The shoelace formula: half the sum of each edge's cross product with
	// the origin -- the fan from the origin's triangles, signed, which is why
	// it works for any simple polygon wherever the origin is.
	float twice = 0.f;
	const size_t n = outline.size();
	for (size_t i = 0; i < n; i++) { twice += cross(outline[i], outline[(i + 1) % n]); }
	return 0.5f * twice;
}

glm::vec2 centroid(const std::vector<glm::vec2> &outline)
{
	float twice = 0.f;
	glm::vec2 sum = {};
	const size_t n = outline.size();
	for (size_t i = 0; i < n; i++)
	{
		const glm::vec2 a = outline[i], b = outline[(i + 1) % n];
		const float c = cross(a, b);
		twice += c;
		sum += (a + b) * c;
	}
	if (std::abs(twice) < 1e-6f)
	{
		glm::vec2 mean = {};
		for (const glm::vec2 &p : outline) { mean += p; }
		return n ? mean / (float)n : mean;
	}
	return sum / (3.f * twice);
}

bool fanWorksFrom(const std::vector<glm::vec2> &outline, glm::vec2 apex)
{
	const size_t n = outline.size();
	if (n < 3) { return false; }
	const float winding = signedArea(outline) >= 0.f ? 1.f : -1.f;
	for (size_t i = 0; i < n; i++)
	{
		// Each fan triangle must turn the polygon's own way, with some area.
		if (winding * cross(outline[i] - apex, outline[(i + 1) % n] - apex) <= 1e-4f) { return false; }
	}
	return true;
}

std::vector<int> earClip(const std::vector<glm::vec2> &outline)
{
	std::vector<int> triangles;
	const int n = (int)outline.size();
	if (n < 3) { return triangles; }
	const float winding = signedArea(outline) >= 0.f ? 1.f : -1.f;

	std::vector<int> left(n);
	for (int i = 0; i < n; i++) { left[i] = i; }

	// Walk round what is left; clip the first ear found. If a whole lap finds
	// none, the polygon crosses itself (or is degenerate) -- stop there.
	int misses = 0;
	int at = 0;
	while (left.size() > 3 && misses < (int)left.size())
	{
		const int m = (int)left.size();
		const int ip = left[(at + m - 1) % m], ic = left[at % m], in = left[(at + 1) % m];
		const glm::vec2 a = outline[ip], b = outline[ic], c = outline[in];

		// Convex here: the corner turns the polygon's way.
		bool ear = winding * cross(b - a, c - b) > 1e-6f;
		// And no other remaining corner inside the triangle it would cut off.
		for (int k = 0; ear && k < m; k++)
		{
			const int ik = left[k];
			if (ik == ip || ik == ic || ik == in) { continue; }
			if (inTriangle(outline[ik], a, b, c)) { ear = false; }
		}

		if (ear)
		{
			triangles.push_back(ip);
			triangles.push_back(ic);
			triangles.push_back(in);
			left.erase(left.begin() + (at % m));
			misses = 0;
			if (at >= (int)left.size()) { at = 0; }
		}
		else
		{
			at = (at + 1) % m;
			misses++;
		}
	}
	if (left.size() == 3)
	{
		triangles.push_back(left[0]);
		triangles.push_back(left[1]);
		triangles.push_back(left[2]);
	}
	return triangles;
}

std::vector<Piece> voronoiFracture(const std::vector<glm::vec2> &outline,
	const std::vector<glm::vec2> &sites)
{
	std::vector<Piece> pieces;
	for (size_t i = 0; i < sites.size(); i++)
	{
		Piece piece;
		piece.points = outline;
		piece.neighbour.assign(outline.size(), -1);

		for (size_t j = 0; j < sites.size() && piece.points.size() >= 3; j++)
		{
			if (j == i) { continue; }
			// Nearer site i than site j: the side of their perpendicular
			// bisector where dot(p, normal) <= limit.
			const glm::vec2 normal = sites[j] - sites[i];
			const float limit = 0.5f * (glm::dot(sites[j], sites[j]) - glm::dot(sites[i], sites[i]));
			auto keep = [&](glm::vec2 p) { return glm::dot(p, normal) <= limit; };

			// Sutherland-Hodgman against one half-plane: walk the edges, keep
			// what is inside, and where an edge crosses the line, add the
			// crossing. An edge that runs *along* the line -- from one
			// crossing to the next -- is a new border, with piece j.
			Piece out;
			const size_t n = piece.points.size();
			for (size_t k = 0; k < n; k++)
			{
				const glm::vec2 p = piece.points[k], q = piece.points[(k + 1) % n];
				const bool pIn = keep(p), qIn = keep(q);
				if (pIn)
				{
					out.points.push_back(p);
					// The edge from p goes on to q if q is kept; if not, it now
					// ends at the crossing, on the bisector -- and the edge after
					// that crossing runs along it.
					out.neighbour.push_back(piece.neighbour[k]);
				}
				if (pIn != qIn)
				{
					const float dp = glm::dot(p, normal) - limit, dq = glm::dot(q, normal) - limit;
					const glm::vec2 crossing = p + (q - p) * (dp / (dp - dq));
					out.points.push_back(crossing);
					// Leaving: the next kept edge starts here and runs along the
					// bisector to where the outline comes back in. Entering: the
					// rest of the old edge, from here to q.
					out.neighbour.push_back(pIn ? (int)j : piece.neighbour[k]);
				}
			}
			piece = std::move(out);
		}
		if (piece.points.size() >= 3) { pieces.push_back(std::move(piece)); }
	}
	return pieces;
}

}
