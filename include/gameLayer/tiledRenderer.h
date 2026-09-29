#pragma once
#include <render/wgpu2d.h>

struct TiledRenderer
{

	float backgroundSize = 10000;
	wgpu2d::Texture texture;

	// 0 moves with the world; toward 1, slower -- further away. 1 hangs still.
	float paralaxStrength = 1;

	void render(wgpu2d::Renderer2D &renderer);
};
