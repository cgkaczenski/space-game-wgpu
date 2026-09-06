#pragma once
#include <render/renderer.h>
#include <collisionSystem.h>

constexpr float bulletHitboxRadius = 20.f;
// Trail sprites sit ahead of `position`; keep radius and shift the circle to the nose.
constexpr float bulletHitboxForwardOffset = 100.f;

struct Bullet
{
	glm::vec2 position = {};
	glm::vec2 fireDirection = {};

	void render(r2d::Renderer2D &renderer,
		r2d::Texture bulletsTexture, r2d::TextureAtlasPadding bulletsAtlas
		);

	void update(float deltaTime, float speedMultiplier = 1.f);

	collision::Circle getHitbox() const;

	bool isEnemy = 0;
	float speed = 3000;
};