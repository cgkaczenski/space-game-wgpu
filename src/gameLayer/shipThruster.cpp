#include <shipThruster.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace thruster
{

namespace
{
	wgpu2d::Texture glow;

	// The eased throttle, and a clock for the flicker.
	float level = 0.f;
	float phase = 0.f;

	// How the plume behaves. Rise is faster than fall so the engine catches
	// immediately and trails off, which is what an exhaust does.
	const float risePerSecond = 14.f;
	const float fallPerSecond = 7.f;
	const float flickerHz = 17.f;
	const float flickerDepth = 0.16f;

	// Four puffs, each further back, smaller and dimmer than the last. The
	// overlap is the point: under additive they sum into a hot core near the
	// hull and taper to nothing, which no single quad would do.
	const int puffCount = 4;
	const float nearDistance = 0.34f;  // fractions of the ship's size
	const float farDistance = 0.64f;
	const float nearSize = 0.44f;
	const float farSize = 0.18f;

	// Deliberately under 1 per channel. The bullets' ramp reached 1.6 and,
	// once it stopped being clamped by alpha blending, five overlapping quads
	// summed to 4x and clipped every channel to white -- the sprite vanished
	// into a featureless blob. Here the innermost puff peaks near 1 on its own
	// and only the very centre saturates, which is what a hot core should do.
	const glm::vec4 coreColor = {0.42f, 0.68f, 1.0f, 1.f};

	// A soft radial falloff, opaque at the centre and zero at the rim. Squared
	// so the edge is gentle rather than a visible disc.
	bool buildGlowTexture()
	{
		const int size = 64;
		std::vector<unsigned char> pixels((size_t)size * size * 4);

		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				const float dx = (x + 0.5f) / size * 2.f - 1.f;
				const float dy = (y + 0.5f) / size * 2.f - 1.f;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float falloff = std::max(0.f, 1.f - r);

				unsigned char *p = pixels.data() + ((size_t)y * size + x) * 4;
				// White, with the shape entirely in alpha: the sprite shader
				// multiplies vertex colour by the sample, and additive then
				// contributes rgb * a, so alpha is the falloff and the vertex
				// colour is the intensity.
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(falloff * falloff * 255.f);
			}
		}

		// Smooth, not pixelated: this is a gradient, and nearest filtering
		// would show the 64px grid as the quad is scaled up.
		glow.createFromBuffer((const char *)pixels.data(), size, size, false, true);
		return glow.id != 0;
	}
}

bool init()
{
	if (!buildGlowTexture())
	{
		std::cerr << "thruster: could not create the glow texture\n";
		return false;
	}
	return true;
}

void cleanup()
{
	glow.cleanup();
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec2 shipPos, float shipSize,
	glm::vec2 facing, float throttle, float dt)
{
	if (glow.id == 0) { return; }

	// Ease towards the throttle. The clamp keeps a stalled frame from
	// snapping the plume to full or off in one step.
	const float rate = (throttle > level) ? risePerSecond : fallPerSecond;
	level += (throttle - level) * std::min(1.f, rate * std::max(0.f, dt));
	if (level < 0.004f)
	{
		level = 0.f; // settle exactly, so an idle ship records no quads at all
		return;
	}
	phase += dt;

	const float flicker = 1.f + flickerDepth * std::sin(6.2831853f * flickerHz * phase);
	const glm::vec2 back = -facing;

	// One setBlendMode around the whole plume. It costs no extra draw run in
	// practice: the glow is its own texture, so the batch was breaking here
	// anyway.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (int i = 0; i < puffCount; i++)
	{
		// i / puffCount, not i / (puffCount - 1): the latter puts t at exactly
		// 1 on the last puff, where the fade below is zero -- a quad drawn
		// every frame that contributes nothing. This way every puff earns its
		// place and the plume reaches further back.
		const float t = i / (float)puffCount;
		const float distance = shipSize * (nearDistance + (farDistance - nearDistance) * t);
		const float size = shipSize * (nearSize + (farSize - nearSize) * t);
		const float fade = (1.f - t) * (1.f - t);

		glm::vec4 color = coreColor * (level * flicker * fade);
		color.a = 1.f; // the falloff is the texture's; this is intensity only

		const glm::vec2 centre = shipPos + back * distance;
		renderer.renderRectangle({centre - glm::vec2(size * 0.5f), size, size}, glow, color);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
}

}
