#pragma once

#include <glm/vec2.hpp>
#include <variant>

namespace collision
{

struct Circle
{
	glm::vec2 center{};
	float radius{0.f};
};

struct Rect
{
	glm::vec2 pos{};
	glm::vec2 size{};
};

// Ships and bullets currently use circles. Rect is here so ship-ship (or a
// later box collider) can switch without rewriting overlap dispatch.
using Hitbox = std::variant<Circle, Rect>;

class ICollisionSystem
{
public:
	virtual ~ICollisionSystem() = default;

	virtual bool overlaps(const Circle &a, const Circle &b) const = 0;
	virtual bool overlaps(const Rect &a, const Rect &b) const = 0;
	virtual bool overlaps(const Circle &circle, const Rect &rect) const = 0;
	virtual bool overlaps(const Rect &rect, const Circle &circle) const = 0;
	virtual bool overlaps(const Hitbox &a, const Hitbox &b) const = 0;

	// Push to separate two overlapping circles. Zero if they do not overlap.
	// Intended for ship-ship resolution (player vs enemy, enemy vs enemy).
	virtual glm::vec2 separation(const Circle &a, const Circle &b) const = 0;
};

class BasicCollisionSystem final : public ICollisionSystem
{
public:
	bool overlaps(const Circle &a, const Circle &b) const override;
	bool overlaps(const Rect &a, const Rect &b) const override;
	bool overlaps(const Circle &circle, const Rect &rect) const override;
	bool overlaps(const Rect &rect, const Circle &circle) const override;
	bool overlaps(const Hitbox &a, const Hitbox &b) const override;

	glm::vec2 separation(const Circle &a, const Circle &b) const override;
};

// Sprite is visualSize x visualSize; radius is half of that so the circle
// fits the ship square. Player, enemies, and ship-ship all use this.
inline float shipHitboxRadius(float visualSize)
{
	return visualSize * 0.5f;
}

inline Circle shipHitbox(glm::vec2 center, float visualSize)
{
	return {center, shipHitboxRadius(visualSize)};
}

} // namespace collision
