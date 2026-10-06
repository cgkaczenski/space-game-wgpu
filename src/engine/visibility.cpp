#include <engine/visibility.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace visibility
{

namespace
{
	constexpr float twoPi = 6.28318530718f;

	float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

	int wrap(int i, int n) { return ((i % n) + n) % n; }

	float sliceAngle(const PolarMap &map, int slice)
	{
		return ((float)slice + 0.5f) * twoPi / (float)map.distance.size();
	}
}

void begin(PolarMap &map, glm::vec2 origin, float range, int slices)
{
	map.origin = origin;
	map.range = std::max(range, 0.f);
	map.distance.assign((size_t)std::max(slices, 3), map.range);
}

void addSegment(PolarMap &map, glm::vec2 a, glm::vec2 b)
{
	const int n = (int)map.distance.size();
	if (n == 0) { return; }
	const glm::vec2 pa = a - map.origin;
	const glm::vec2 pb = b - map.origin;
	const glm::vec2 edge = pb - pa;

	// The angle the segment covers, seen from the viewer: from the end it
	// starts at, turning the short way to the other. A segment whose line
	// passes through the viewer is seen edge-on and covers none.
	const float turn = std::atan2(cross(pa, pb), glm::dot(pa, pb));
	if (std::fabs(turn) < 1e-7f) { return; }
	const glm::vec2 from = turn >= 0.f ? pa : pb;
	const float start = std::atan2(from.y, from.x);
	const float end = start + std::fabs(turn);

	// Every slice whose centre line lies in that angle: where along that line
	// the segment is -- t * u = pa + s * edge, crossed with edge on both sides.
	const float step = twoPi / (float)n;
	const float along = cross(pa, edge);
	for (int k = (int)std::ceil(start / step - 0.5f); ((float)k + 0.5f) * step <= end; k++)
	{
		const float angle = ((float)k + 0.5f) * step;
		const glm::vec2 u = {std::cos(angle), std::sin(angle)};
		const float facing = cross(u, edge);
		if (std::fabs(facing) < 1e-9f) { continue; }
		const float t = along / facing;
		if (t <= 0.f) { continue; }
		float &slot = map.distance[(size_t)wrap(k, n)];
		slot = std::min(slot, t);
	}
}

void blockAll(PolarMap &map)
{
	std::fill(map.distance.begin(), map.distance.end(), 0.f);
}

glm::vec2 direction(const PolarMap &map, int slice)
{
	const float angle = sliceAngle(map, slice);
	return {std::cos(angle), std::sin(angle)};
}

glm::vec2 corner(const PolarMap &map, int slice)
{
	const int n = (int)map.distance.size();
	const int i = wrap(slice, n);
	return map.origin + direction(map, i) * map.distance[(size_t)i];
}

bool sees(const PolarMap &map, glm::vec2 point)
{
	const int n = (int)map.distance.size();
	if (n == 0) { return false; }
	const glm::vec2 p = point - map.origin;
	if (glm::dot(p, p) <= 0.f) { return true; }

	// The two slices whose centre lines the point lies between, and the
	// triangle the fan draws there: viewer, one corner, the next.
	const float step = twoPi / (float)n;
	const int i0 = (int)std::floor(std::atan2(p.y, p.x) / step - 0.5f);
	const glm::vec2 q0 = corner(map, i0) - map.origin;
	const glm::vec2 q1 = corner(map, i0 + 1) - map.origin;
	const glm::vec2 chord = q1 - q0;
	const float viewerSide = cross(chord, -q0);
	if (viewerSide == 0.f) { return false; } // a slice that sees nothing: no triangle
	return cross(chord, p - q0) * viewerSide >= 0.f;
}

}
