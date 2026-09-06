#pragma once
#include <render/renderer.h>

struct TiledRenderer
{

	float backgroundSize = 10000;
	r2d::Texture texture;

	float paralaxStrength = 1;

	void render(r2d::Renderer2D &renderer);
};

void renderSpaceShip(
	r2d::Renderer2D &renderer,
	glm::vec2 position, float size,
	r2d::Texture texture,
	glm::vec4 uvs, glm::vec2 viewDirection);