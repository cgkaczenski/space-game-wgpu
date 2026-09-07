#pragma once
#include <render/wgpu2d.h>

struct TiledRenderer
{

	float backgroundSize = 10000;
	wgpu2d::Texture texture;

	float paralaxStrength = 1;

	void render(wgpu2d::Renderer2D &renderer);
};

void renderSpaceShip(
	wgpu2d::Renderer2D &renderer,
	glm::vec2 position, float size,
	wgpu2d::Texture texture,
	glm::vec4 uvs, glm::vec2 viewDirection);