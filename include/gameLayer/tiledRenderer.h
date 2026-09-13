#pragma once
#include <render/wgpu2d.h>

struct TiledRenderer
{

	float backgroundSize = 10000;
	wgpu2d::Texture texture;

	float paralaxStrength = 1;

	void render(wgpu2d::Renderer2D &renderer);
};
