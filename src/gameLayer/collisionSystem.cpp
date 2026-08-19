#include <collisionSystem.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace collision
{

bool BasicCollisionSystem::overlaps(const Circle &a, const Circle &b) const
{
	const float radiusSum = a.radius + b.radius;
	return glm::distance(a.center, b.center) <= radiusSum;
}

bool BasicCollisionSystem::overlaps(const Rect &a, const Rect &b) const
{
	return (a.pos.x < b.pos.x + b.size.x &&
		a.pos.x + a.size.x > b.pos.x &&
		a.pos.y < b.pos.y + b.size.y &&
		a.pos.y + a.size.y > b.pos.y);
}

bool BasicCollisionSystem::overlaps(const Circle &circle, const Rect &rect) const
{
	const glm::vec2 rectMin = rect.pos;
	const glm::vec2 rectMax = rect.pos + rect.size;

	glm::vec2 closestPoint;
	closestPoint.x = std::clamp(circle.center.x, rectMin.x, rectMax.x);
	closestPoint.y = std::clamp(circle.center.y, rectMin.y, rectMax.y);

	return glm::distance(circle.center, closestPoint) <= circle.radius;
}

bool BasicCollisionSystem::overlaps(const Rect &rect, const Circle &circle) const
{
	return overlaps(circle, rect);
}

bool BasicCollisionSystem::overlaps(const Hitbox &a, const Hitbox &b) const
{
	return std::visit([this](const auto &lhs, const auto &rhs)
	{
		return overlaps(lhs, rhs);
	}, a, b);
}

glm::vec2 BasicCollisionSystem::separation(const Circle &a, const Circle &b) const
{
	glm::vec2 delta = a.center - b.center;
	float dist = glm::length(delta);
	float minDist = a.radius + b.radius;

	if (dist >= minDist)
	{
		return {};
	}

	if (dist <= 0.0001f)
	{
		return {minDist, 0.f};
	}

	return glm::normalize(delta) * (minDist - dist);
}

} // namespace collision
