#include <bulletLook.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace bulletLook
{

namespace
{
	wgpu2d::Texture capsule;

	// The pixel-art sprites, one cell each for player and enemy shots.
	wgpu2d::Texture sheet;
	wgpu2d::TextureAtlasPadding sheetAtlas;

	// The texture's aspect, and the quad's. They have to match, or the capsule
	// stretches: a round cap in a square texture drawn on a 3:1 quad comes out
	// an ellipse.
	const int textureHeight = 96;
	const int textureWidth = 256;
	const float aspect = (float)textureWidth / (float)textureHeight;

	// Sized against the sprite it sits under. drawSprite lays five 100px
	// quads along the heading at 25px steps, so the art spans roughly the
	// bullet's position to position + 100 * direction, and its visual centre
	// is half way along that.
	const float glowLength = 230.f;
	const float glowWidth = glowLength / aspect;
	const float centreAhead = 50.f;

	// Above 1 on purpose, which inverts the rule the plume and the shield
	// follow. They keep every channel under 1 because clipping there destroys
	// the shape. Here clipping is the effect: the channels clip at different
	// gradient values, so the core burns to white while the body keeps its hue
	// and the halo stays saturated. One colour, one gradient, a hot core for
	// free. Drop this to ~1.4 for a flat glow with no white centre.
	const float intensity = 2.2f;

	const glm::vec4 playerColor = {1.00f, 0.30f, 0.85f, 1.f}; // plasma
	const glm::vec4 enemyColor = {0.35f, 0.70f, 1.00f, 1.f};  // ice
	const glm::vec4 miningColor = {1.00f, 0.70f, 0.20f, 1.f}; // amber: the flight-mode beam (M1)

	struct Look
	{
		glm::ivec2 cell;  // in the 3x2 sheet
		glm::vec4 glow;
	};

	Look lookFor(BulletStyle style, bool isEnemy)
	{
		if (isEnemy) { return {{0, 0}, enemyColor}; }
		switch (style)
		{
		case BulletStyle::Standard: return {{1, 1}, playerColor};
		case BulletStyle::Heavy:    return {{1, 0}, {1.00f, 0.42f, 0.12f, 1.f}};  // ember
		case BulletStyle::Missile:  return {{0, 1}, {0.45f, 1.00f, 0.30f, 1.f}};  // acid
		case BulletStyle::Laser:    return {{2, 0}, {0.30f, 0.90f, 1.00f, 1.f}};  // cyan
		}
		return {{1, 1}, playerColor};
	}

	// Distance to a horizontal line segment, faded. Worked in half-height
	// units so the caps come out round: v spans [-1, 1] over the height and u
	// spans [-aspect, aspect] over the width, which makes a unit of u and a
	// unit of v the same number of texels.
	bool buildCapsuleTexture()
	{
		std::vector<unsigned char> pixels((size_t)textureWidth * textureHeight * 4);
		const float halfLength = aspect - 1.f; // leave room for a round cap at each end

		for (int y = 0; y < textureHeight; y++)
		{
			for (int x = 0; x < textureWidth; x++)
			{
				const float u = ((x + 0.5f) / textureWidth * 2.f - 1.f) * aspect;
				const float v = (y + 0.5f) / textureHeight * 2.f - 1.f;

				const float beyondEnd = std::max(0.f, std::fabs(u) - halfLength);
				const float distance = std::sqrt(beyondEnd * beyondEnd + v * v);
				const float a = std::clamp(1.f - distance, 0.f, 1.f);

				unsigned char *p = pixels.data() + ((size_t)y * textureWidth + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(a * a * 255.f); // squared: a gentle edge
			}
		}

		capsule.createFromBuffer((const char *)pixels.data(), textureWidth, textureHeight, false, true);
		return capsule.id != 0;
	}
}

bool init()
{
	if (!buildCapsuleTexture())
	{
		std::cerr << "bulletLook: could not create the capsule texture\n";
		return false;
	}

	sheet.loadFromFileWithPixelPadding
	(RESOURCES_PATH "spaceShip/stitchedFiles/projectiles.png", 500, true);
	if (sheet.id == 0)
	{
		std::cerr << "bulletLook: could not load the bullet sprite sheet\n";
		return false;
	}
	sheetAtlas = wgpu2d::TextureAtlasPadding(3, 2, sheet.GetSize().x, sheet.GetSize().y);
	return true;
}

void cleanup()
{
	capsule.cleanup();
	sheet.cleanup();
}

void drawGlow(wgpu2d::Renderer2D &renderer, glm::vec2 position, glm::vec2 direction, bool isEnemy,
	BulletStyle style, float size, Mark mark)
{
	if (capsule.id == 0) { return; }

	// The capsule's long axis is +x in the texture. pushQuad rotates in gl2d's
	// flipped (y-up) space and flips back, so a rotation of t maps +x to
	// (cos t, -sin t) in world coordinates -- hence the negated y. This is not
	// the angle drawSprite uses; that one is tuned to where its sprite art
	// points, which is a different question.
	const float rotation = glm::degrees(std::atan2(-direction.y, direction.x));

	const float length = glowLength * size;
	const float width = glowWidth * size;
	const glm::vec2 centre = position + direction * (centreAhead * size);
	glm::vec4 color = lookFor(style, isEnemy).glow * intensity;
	if (mark == Mark::Stun) { color = glm::vec4(1.0f, 0.92f, 0.25f, 1.f) * intensity; }
	if (mark == Mark::Lockdown) { color = glm::vec4(0.72f, 0.35f, 1.0f, 1.f) * intensity; }

	renderer.renderRectangle(
		{centre - glm::vec2(length * 0.5f, width * 0.5f), length, width},
		capsule, glm::vec4{color.r, color.g, color.b, 1.f}, {}, rotation);
}

void drawSprite(wgpu2d::Renderer2D &renderer, glm::vec2 position, glm::vec2 direction,
	bool isEnemy, BulletStyle style, float size)
{
	if (sheet.id == 0) { return; }

	float angle = atan2(direction.y, -direction.x);
	angle = glm::degrees(angle) + 90.f;

	const glm::ivec2 cell = lookFor(style, isEnemy).cell;
	const glm::vec4 textureCoords = sheetAtlas.get(cell.x, cell.y);

	const float quad = 100.f * size;
	const float step = 25.f * size;

	for (int i = 0; i < 5; i++)
	{
		glm::vec4 color(1* (i + 4) / 5.f, 1* (i + 4) / 5.f, 1* (i + 4) / 5.f, (i+1) / 5.f);

		renderer.renderRectangle({position - glm::vec2(quad * 0.5f) + (float)i * step * direction, quad, quad},
			sheet, color, {}, angle, textureCoords);
	}
}

namespace
{
	// The beam's proportions. The core is the sprite art; the glow is wider
	// and softer, and the CRT's own glow blooms it further.
	const float beamCoreWidth = 44.f;
	const float beamGlowWidth = 120.f;
	const float beamScrollSpeed = 900.f; // world units per second, along the beam
	const float impactSize = 260.f;
}

void drawBeamGlow(wgpu2d::Renderer2D &renderer, glm::vec2 start, glm::vec2 end,
	BeamImpact impact, float time, glm::vec2 surfaceNormal, bool isEnemy, bool mining)
{
	if (capsule.id == 0) { return; }
	const glm::vec2 along = end - start;
	const float length = glm::length(along);
	if (length < 1.f) { return; }
	const glm::vec2 direction = along / length;

	// As drawGlow: the capsule's long axis is +x, rotated in flipped space.
	const float rotation = glm::degrees(std::atan2(-direction.y, direction.x));
	const glm::vec4 color = (mining && !isEnemy ? miningColor : lookFor(BulletStyle::Laser, isEnemy).glow)
		* intensity;
	const glm::vec2 centre = (start + end) * 0.5f;

	// A little longer than the beam, so its rounded ends do not stop short.
	const float glowLength = length + beamGlowWidth;
	renderer.renderRectangle(
		{centre - glm::vec2(glowLength * 0.5f, beamGlowWidth * 0.5f), glowLength, beamGlowWidth},
		capsule, glm::vec4{color.r, color.g, color.b, 1.f}, {}, rotation);

	if (impact == BeamImpact::Burn)
	{
		// Where it burns: a round flare, flickering. The capsule's middle
		// drawn short is round enough.
		const float flicker = 0.8f + 0.2f * std::sin(time * 55.f);
		const float size = impactSize * flicker;
		const glm::vec4 burst = color * 1.2f;
		renderer.renderRectangle({end - glm::vec2(size * 0.5f, size * 0.5f / aspect * 2.f),
			size, size / aspect * 2.f}, capsule, glm::vec4{burst.r, burst.g, burst.b, 1.f}, {}, rotation);
	}
	else if (impact == BeamImpact::Deflect)
	{
		// It does nothing here, and must look it: no flare, no colour -- the
		// beam's light splashing flat off the surface instead. A hard bright
		// bar lying along the surface where it strikes, and sparks skating off
		// along the surface both ways, lifting a little away from it, each
		// flickering on its own. Along the surface rather than reflected: hit
		// square on, a reflection points straight back up the beam and is
		// lost in its glow.
		glm::vec2 n = surfaceNormal;
		const float nl = glm::length(n);
		n = nl > 1e-4f ? n / nl : -direction;
		const glm::vec2 along = {-n.y, n.x};
		const float flatRotation = glm::degrees(std::atan2(-along.y, along.x));
		const float flicker = 0.85f + 0.15f * std::sin(time * 61.f);
		const glm::vec4 white = {0.85f, 0.9f, 1.f, 1.f};
		const float bar = impactSize * 0.9f * flicker;
		const float thick = impactSize * 0.1f;
		renderer.renderRectangle({end - glm::vec2(bar * 0.5f, thick * 0.5f), bar, thick},
			capsule, white, {}, flatRotation);

		for (int k = 0; k < 4; k++)
		{
			const float side = (k % 2 == 0) ? 1.f : -1.f;
			const float jitter = std::sin(time * (37.f + 11.f * k) + k * 2.1f);
			const float lift = 0.2f + 0.2f * (k / 2) + 0.1f * jitter; // radians off the surface
			const glm::vec2 d = glm::normalize(along * side * std::cos(lift) + n * std::sin(lift));
			const float sparkLength = impactSize * (0.7f + 0.3f * jitter);
			const float sparkRotation = glm::degrees(std::atan2(-d.y, d.x));
			const glm::vec2 mid = end + d * (sparkLength * 0.5f + 20.f);
			const float bright = 0.6f + 0.4f * (0.5f + 0.5f * jitter);
			renderer.renderRectangle({mid - glm::vec2(sparkLength * 0.5f, 8.f), sparkLength, 16.f},
				capsule, glm::vec4(white.r, white.g, white.b, 1.f) * bright, {}, sparkRotation);
		}
	}
}

void drawBeamCore(wgpu2d::Renderer2D &renderer, glm::vec2 start, glm::vec2 end, float time, bool isEnemy,
	bool mining)
{
	if (sheet.id == 0) { return; }
	const glm::vec2 along = end - start;
	const float length = glm::length(along);
	if (length < 1.f) { return; }
	const glm::vec2 direction = along / length;

	// As drawSprite: the art points up in its cell.
	const float angle = glm::degrees(std::atan2(direction.y, -direction.x)) + 90.f;
	const glm::ivec2 cell = lookFor(BulletStyle::Laser, false).cell;
	const glm::vec4 textureCoords = sheetAtlas.get(cell.x, cell.y);

	// Square tiles along the beam, the first shifted back by the scroll so the
	// pattern travels outward. The part behind `start` sits under the hull.
	const float tile = beamCoreWidth;
	const float offset = std::fmod(time * beamScrollSpeed, tile);
	for (float d = -offset; d < length; d += tile)
	{
		const glm::vec2 centre = start + direction * (d + tile * 0.5f);
		// The laser's own art either way; an enemy's is tinted to its
		// colour, and a mining beam to amber.
		const glm::vec4 tint = isEnemy ? enemyColor : mining ? miningColor : glm::vec4(Colors_White);
		renderer.renderRectangle({centre - glm::vec2(tile * 0.5f), tile, tile},
			sheet, tint, {}, angle, textureCoords);
	}
}

void drawIcon(wgpu2d::Renderer2D &renderer, glm::vec2 centre, float height, BulletStyle style)
{
	// The trail spans from half a quad behind `position` to 150 units ahead of
	// it at size 1, so its middle is 50 ahead: start 50 behind the centre.
	const glm::vec2 up = {0.f, -1.f};
	const float size = height / 200.f;
	const glm::vec2 position = centre - up * (50.f * size);

	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	drawGlow(renderer, position, up, false, style, size * 0.8f);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	drawSprite(renderer, position, up, false, style, size);
}

}
