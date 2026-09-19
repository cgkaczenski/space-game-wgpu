#include <scenery.h>

#include <algorithm>
#include <iostream>

namespace scenery
{

namespace
{
	// A file, loaded once. Several pieces of art can be cut from one.
	struct Source
	{
		const char *file;
		wgpu2d::Texture texture;
	};

	Source sources[] = {
		// The starfield's old front layer, whose planets are now placed by
		// levels instead of tiled (gameplay roadmap L2).
		{RESOURCES_PATH "background4.png", {}},
		{RESOURCES_PATH "space/Black Hole1.png", {}},
		{RESOURCES_PATH "space/Black Hole2.png", {}},
		{RESOURCES_PATH "space/Black Hole3.png", {}},
		{RESOURCES_PATH "space/ShatteredMoon.png", {}},
		{RESOURCES_PATH "space/ShatteredPlanet5.png", {}},
		{RESOURCES_PATH "space/ShatteredPlanet7.png", {}},
		{RESOURCES_PATH "space/ShatteredPlanet8.png", {}},
		{RESOURCES_PATH "space/ShatteredPlanet9.png", {}},
	};

	struct Art
	{
		const char *name;   // as a level file writes it: no spaces
		int source;
		// The piece's pixels in the source, left top right bottom (exclusive);
		// all zero for the whole image.
		int left, top, right, bottom;
	};

	// The planets are background4.png's four, found by their opaque pixels
	// with two transparent pixels of margin, largest first. The starfield drew
	// that sheet at 20 world units a pixel, so their natural sizes are about
	// 2700, 2000, 1200 and 1000.
	//
	// The black holes and shattered planets are 64-128 px art that looks
	// coarse at any size near a ship's. Use them small and far back only.
	const Art art[] = {
		{"Planet1",           0,  43, 225, 183, 365},
		{"Planet2",           0,  17,  27, 122, 132},
		{"Planet3",           0, 266, 295, 330, 359},
		{"Planet4",           0, 294, 113, 348, 167},
		{"BlackHole1",        1, 0, 0, 0, 0},
		{"BlackHole2",        2, 0, 0, 0, 0},
		{"BlackHole3",        3, 0, 0, 0, 0},
		{"ShatteredMoon",     4, 0, 0, 0, 0},
		{"ShatteredPlanet5",  5, 0, 0, 0, 0},
		{"ShatteredPlanet7",  6, 0, 0, 0, 0},
		{"ShatteredPlanet8",  7, 0, 0, 0, 0},
		{"ShatteredPlanet9",  8, 0, 0, 0, 0},
	};
	constexpr int count = sizeof(art) / sizeof(art[0]);

	const Art *find(const std::string &name)
	{
		for (const Art &a : art)
		{
			if (name == a.name) { return &a; }
		}
		return nullptr;
	}
}

bool init()
{
	for (Source &s : sources)
	{
		// Pixelated: small art drawn thousands of units across.
		s.texture.loadFromFile(s.file, true);
		if (s.texture.id == 0)
		{
			std::cerr << "scenery: failed to load " << s.file << "\n";
			return false;
		}
	}
	return true;
}

void cleanup()
{
	for (Source &s : sources) { s.texture.cleanup(); }
}

void draw(wgpu2d::Renderer2D &renderer, const std::vector<level::Scenery> &pieces)
{
	const glm::vec4 view = renderer.getViewRect();
	const glm::vec2 camera = glm::vec2(view.x, view.y) + glm::vec2(view.z, view.w) * 0.5f;

	for (const level::Scenery &piece : pieces)
	{
		const Art *a = find(piece.art);
		if (!a) { continue; }
		// A copy of the handle: GetSize is not const in wgpu2d.
		wgpu2d::Texture texture = sources[a->source].texture;

		// Carried along with the camera by `depth` of its movement, measured
		// from the piece's own spot, so it sits at its x y when you are there.
		const glm::vec2 at = piece.position + (camera - piece.position) * piece.depth;

		// The piece's rectangle in the source, and the texture coordinates for
		// it in the renderer's convention: left, top, right, bottom, with v
		// running up from the bottom of the image (see the atlas in wgpu2d).
		const glm::vec2 image = glm::vec2(texture.GetSize());
		glm::vec4 pixels = {(float)a->left, (float)a->top, (float)a->right, (float)a->bottom};
		if (a->right == 0) { pixels = {0.f, 0.f, image.x, image.y}; }
		const glm::vec4 uv = {pixels.x / image.x, 1.f - pixels.y / image.y,
			pixels.z / image.x, 1.f - pixels.w / image.y};

		// Keeps the art's own shape: `size` is across the wider side.
		const glm::vec2 texels = {pixels.z - pixels.x, pixels.w - pixels.y};
		const float longer = std::max(texels.x, texels.y);
		const glm::vec2 size = longer > 0.f ? texels / longer * piece.size : glm::vec2(piece.size);

		renderer.renderRectangle({at - size * 0.5f, size}, texture, Colors_White, {}, 0.f, uv);
	}
}

int artCount() { return count; }
const char *artName(int index) { return art[index].name; }

}
