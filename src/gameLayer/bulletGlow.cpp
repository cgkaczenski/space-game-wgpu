#include <bulletGlow.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace bulletGlow
{

namespace
{
	wgpu2d::Texture capsule;

	// The texture's aspect, and the quad's. They have to match, or the capsule
	// stretches: a round cap in a square texture drawn on a 3:1 quad comes out
	// an ellipse.
	const int textureHeight = 96;
	const int textureWidth = 256;
	const float aspect = (float)textureWidth / (float)textureHeight;

	// Sized against the sprite it sits under. Bullet::render lays five 100px
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
		std::cerr << "bulletGlow: could not create the capsule texture\n";
		return false;
	}
	return true;
}

void cleanup()
{
	capsule.cleanup();
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec2 position, glm::vec2 direction, bool isEnemy)
{
	if (capsule.id == 0) { return; }

	// The capsule's long axis is +x in the texture. pushQuad rotates in gl2d's
	// flipped (y-up) space and flips back, so a rotation of t maps +x to
	// (cos t, -sin t) in world coordinates -- hence the negated y. This is not
	// the angle Bullet::render uses; that one is tuned to where its sprite art
	// points, which is a different question.
	const float rotation = glm::degrees(std::atan2(-direction.y, direction.x));

	const glm::vec2 centre = position + direction * centreAhead;
	const glm::vec4 color = (isEnemy ? enemyColor : playerColor) * intensity;

	renderer.renderRectangle(
		{centre - glm::vec2(glowLength * 0.5f, glowWidth * 0.5f), glowLength, glowWidth},
		capsule, glm::vec4{color.r, color.g, color.b, 1.f}, {}, rotation);
}

}
