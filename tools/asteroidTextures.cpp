// Makes the asteroid's small textures from the 4K rock material in
// resources/textures/ (gameplay roadmap A1-A4). Run through
// tools/asteroidTextures.sh, which builds and runs this.
//
// Three outputs, all `size` x `size`:
//
//   rock_colour.png   the colour map, averaged down
//   rock_height.png   the height (displacement) map, averaged down, 8-bit grey
//   rock_normal.png   normals worked out from that height
//
// Why the normals are derived rather than converted. The material ships a
// normal map, but as an OpenEXR file that neither stb_image nor macOS's sips
// could read. A normal map is the height map's slope in disguise, so it can be
// rebuilt: at each texel, how fast the height rises to the right (dh/dx) and
// upward (dh/dy) gives a surface that tilts away from the rise, and the normal
// is (-dh/dx, -dh/dy, 1) scaled to length 1. `strength` stretches the slopes
// -- the height map says nothing about how tall its 0..1 range is in texels.
//
// The convention is OpenGL's, like the file it replaces (`nor_gl`): +x right,
// +y **up the image**, +z out of the surface, stored as n * 0.5 + 0.5. The
// shader that reads it has to know that the world here is y-down.
//
// The material tiles, so slopes at an edge wrap round to the other side
// instead of treating the edge as a cliff.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image/stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// Averages each `factor` x `factor` block into one texel -- a box filter.
	// Picking every fourth texel instead would alias: fine detail in the
	// source would come out as noise rather than as its average.
	std::vector<float> shrink(const std::vector<float> &src, int srcSize, int channels, int factor)
	{
		const int size = srcSize / factor;
		std::vector<float> out((size_t)size * size * channels, 0.f);
		const float weight = 1.f / (float)(factor * factor);
		for (int y = 0; y < srcSize; y++)
		{
			for (int x = 0; x < srcSize; x++)
			{
				const size_t from = ((size_t)y * srcSize + x) * channels;
				const size_t to = ((size_t)(y / factor) * size + (x / factor)) * channels;
				for (int c = 0; c < channels; c++) { out[to + c] += src[from + c] * weight; }
			}
		}
		return out;
	}

	unsigned char byte(float v)
	{
		const float c = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
		return (unsigned char)std::lround(c * 255.f);
	}
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: %s <texturesDir> <outDir> <size> [normalStrength]\n", argv[0]);
		return 1;
	}
	const std::string in = std::string(argv[1]) + "/";
	const std::string out = std::string(argv[2]) + "/";
	const int size = std::atoi(argv[3]);
	const float strength = argc > 4 ? (float)std::atof(argv[4]) : 6.f;

	// ---- Colour: 8-bit sRGB in, averaged, 8-bit out. -------------------------
	// Averaging sRGB values directly is slightly wrong -- light adds linearly,
	// the stored numbers do not -- and slightly darkens fine bright detail.
	// For rock at a quarter size the difference does not show.
	int w = 0, h = 0, n = 0;
	unsigned char *colour = stbi_load((in + "rock_04_diff_4k.jpg").c_str(), &w, &h, &n, 3);
	if (!colour || w != h || w % size != 0)
	{
		std::fprintf(stderr, "colour: cannot load, or %dx%d does not shrink to %d\n", w, h, size);
		return 1;
	}
	std::vector<float> colourF((size_t)w * h * 3);
	for (size_t i = 0; i < colourF.size(); i++) { colourF[i] = colour[i] / 255.f; }
	stbi_image_free(colour);
	const std::vector<float> colourSmall = shrink(colourF, w, 3, w / size);
	std::vector<unsigned char> colourOut(colourSmall.size());
	for (size_t i = 0; i < colourOut.size(); i++) { colourOut[i] = byte(colourSmall[i]); }
	stbi_write_png((out + "rock_colour.png").c_str(), size, size, 3, colourOut.data(), size * 3);

	// ---- Height: 16-bit grey in, kept as floats until the end. ----------------
	unsigned short *height = stbi_load_16((in + "rock_04_disp_4k.png").c_str(), &w, &h, &n, 1);
	if (!height || w != h || w % size != 0)
	{
		std::fprintf(stderr, "height: cannot load, or %dx%d does not shrink to %d\n", w, h, size);
		return 1;
	}
	std::vector<float> heightF((size_t)w * h);
	for (size_t i = 0; i < heightF.size(); i++) { heightF[i] = height[i] / 65535.f; }
	stbi_image_free(height);
	const std::vector<float> hs = shrink(heightF, w, 1, w / size);
	std::vector<unsigned char> heightOut(hs.size());
	for (size_t i = 0; i < hs.size(); i++) { heightOut[i] = byte(hs[i]); }
	stbi_write_png((out + "rock_height.png").c_str(), size, size, 1, heightOut.data(), size);

	// ---- Normals from the height's slope. -------------------------------------
	// Central differences: the texel to the right minus the one to the left,
	// over the two texels between them. Image rows run down, and +y is up, so
	// "upward" is the row above minus the row below.
	auto at = [&](int x, int y) { return hs[(size_t)((y + size) % size) * size + ((x + size) % size)]; };
	std::vector<unsigned char> normalOut((size_t)size * size * 3);
	double sumZ = 0.0;
	for (int y = 0; y < size; y++)
	{
		for (int x = 0; x < size; x++)
		{
			const float dx = (at(x + 1, y) - at(x - 1, y)) * 0.5f * strength * (size / 64.f);
			const float dy = (at(x, y - 1) - at(x, y + 1)) * 0.5f * strength * (size / 64.f);
			float nx = -dx, ny = -dy, nz = 1.f;
			const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
			nx /= len; ny /= len; nz /= len;
			sumZ += nz;
			unsigned char *p = &normalOut[((size_t)y * size + x) * 3];
			p[0] = byte(nx * 0.5f + 0.5f);
			p[1] = byte(ny * 0.5f + 0.5f);
			p[2] = byte(nz * 0.5f + 0.5f);
		}
	}
	stbi_write_png((out + "rock_normal.png").c_str(), size, size, 3, normalOut.data(), size * 3);

	// How tilted the surface is on average: 1 is flat. A bumpy rock sits
	// somewhere around 0.8 - 0.9; near 1 means `strength` is too low to see.
	std::printf("wrote %dx%d colour, height, normal; mean normal z %.3f (strength %.1f)\n",
		size, size, sumZ / ((double)size * size), strength);
	return 0;
}
