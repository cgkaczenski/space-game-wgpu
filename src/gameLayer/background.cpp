#include <background.h>

#include <tiledRenderer.h>

#include <iostream>

namespace background
{

namespace
{
	constexpr int layerCount = 3;

	// Back to front. The furthest layer does not move with the camera at all.
	// background4.png, the planets, was the front layer; levels place those
	// planets one at a time now, as scenery (gameplay roadmap L2).
	const char *files[layerCount] = {
		RESOURCES_PATH "background1.png",
		RESOURCES_PATH "background2.png",
		RESOURCES_PATH "background3.png",
	};
	constexpr float parallax[layerCount] = {0.f, 0.2f, 0.4f};

	wgpu2d::Texture textures[layerCount];
	TiledRenderer layers[layerCount];
}

bool init()
{
	for (int i = 0; i < layerCount; i++)
	{
		textures[i].loadFromFile(files[i], true);
		if (textures[i].id == 0)
		{
			std::cerr << "background: failed to load " << files[i] << "\n";
			return false;
		}
		layers[i].texture = textures[i];
		layers[i].paralaxStrength = parallax[i];
	}
	return true;
}

void cleanup()
{
	for (int i = 0; i < layerCount; i++)
	{
		textures[i].cleanup();
		// The layer held a copy of the handle; it is a borrower, not an owner,
		// and must not keep pointing at a released id.
		layers[i].texture = {};
	}
}

void draw(wgpu2d::Renderer2D &renderer)
{
	for (int i = 0; i < layerCount; i++)
	{
		layers[i].render(renderer);
	}
}

}
