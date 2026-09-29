#include <background.h>

#include <tiledRenderer.h>

#include <iostream>

namespace background
{

namespace
{
	constexpr int layerCount = 3;

	// Back to front. Every layer moves slower than the world, the furthest
	// slowest -- at 0.2, 0.4 and 0.6 of its speed -- so the whole starfield is
	// behind the play, and the play (ships, rocks, shots) is the fastest thing
	// on screen, which is what reads as nearest. background4.png, the planets,
	// was the front layer; levels place those planets one at a time now, as
	// scenery (gameplay roadmap L2), between these and the play.
	const char *files[layerCount] = {
		RESOURCES_PATH "background1.png",
		RESOURCES_PATH "background2.png",
		RESOURCES_PATH "background3.png",
	};
	constexpr float parallax[layerCount] = {0.8f, 0.6f, 0.4f};

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
