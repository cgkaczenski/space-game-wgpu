#pragma once
#include <render/wgpu2d.h>
#include <engine/collisionSystem.h>

constexpr float enemyShipSize = 250.f;

struct Enemy
{
	glm::uvec2 type = {}; //used to index into the texture atlas
	glm::vec2 position = {};

	glm::vec2 viewDirection = {1,0};

  float speed = 1500.f;
  float turnSpeed = 3.f;
  float firedTime = 1.f;
  float fireTimeReset = 0.2;
  float fireRange = 1.5;
  float bulletSpeed = 2000;

  float life = 1.f;

	void render(wgpu2d::Renderer2D &renderer, wgpu2d::Texture &sprites,
		wgpu2d::TextureAtlasPadding &atlas);

  bool update(float deltaTime, glm::vec2 playerPos, float speedMultiplier = 1.f);

	collision::Circle getHitbox() const;
};