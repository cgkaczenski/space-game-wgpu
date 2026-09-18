#include <effects.h>

#include <shipSprite.h>
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace effects
{

namespace
{
	wgpu2d::Texture glow;

	// A sight cone: a 45-degree-each-way sector with its apex at the middle of
	// the left edge, reaching the right edge, fading with distance. A narrower
	// cone is the same texture drawn squashed across: scaling across keeps the
	// sides straight lines through the apex, so the angle is exact and only the
	// far edge bends from an arc to an ellipse, which the fade hides.
	wgpu2d::Texture sector;
	const int sectorLength = 128;
	const int sectorBreadth = 256;

	struct Piece
	{
		glm::vec2 position;
		glm::vec2 velocity;
		glm::vec2 size;
		glm::vec4 uv;      // this piece's part of the enemy's cell
		float angle;       // degrees, in the renderer's convention
		float spin;        // degrees per second
		float age;
		float fadeStart;   // age it began fading out, < 0 while it is kept
	};

	struct Blast
	{
		glm::vec2 position;
		float size;        // the ship's size
		float age;
	};

	std::vector<Piece> pieces;
	std::vector<Blast> blasts;

	// The ram's trail. Ghosts are copies of the ship left where it was; streaks
	// are thin lines of light laid in space along the ram, standing still, so
	// the ship rushing past them is what makes them read as speed.
	struct Ghost
	{
		glm::vec2 position;
		glm::vec2 facing;
		glm::vec4 uv;
		float size;
		float age;
	};
	struct Streak
	{
		glm::vec2 position;
		glm::vec2 direction;
		float length;
		float age;
	};
	std::vector<Ghost> ghosts;
	std::vector<Streak> streaks;
	float ghostEvery = 0.035f;      // seconds between afterimages
	float ghostLife = 0.25f;
	float ghostTimer = 0.f;
	float streaksPerSecond = 70.f;
	float streakLife = 0.22f;
	float streakTimer = 0.f;
	const float streakWidth = 14.f;
	const glm::vec4 streakColor = {0.60f, 0.85f, 1.0f, 1.f};

	// The cell is cut into a grid: 3 across, 2 down, so the pieces are the
	// size of chunks of hull rather than slivers.
	const int piecesAcross = 3;
	const int piecesDown = 2;

	// Debris stays: it flies apart, slows to a stop and stays where it fell,
	// still turning a little, so a fight leaves a wreck field behind it. Each
	// piece is one quad, and a frame is already dozens of quads, so hundreds
	// cost little. Past `debrisKept` the oldest fade out rather than vanish.
	int debrisKept = 400;           // about 66 kills
	float debrisFadeOut = 1.f;      // seconds, for the oldest past the limit
	float debrisDrag = 1.6f;        // per second, so the burst slows as it spreads
	float debrisSpeedMin = 200.f;
	float debrisSpeedMax = 900.f;
	float debrisSpinMax = 540.f;    // degrees per second either way, at the burst
	float debrisSpinRest = 30.f;    // the most a settled piece still turns
	float debrisSettle = 2.f;       // seconds for a piece to darken into the scene

	float blastLife = 1.1f;         // seconds
	float blastGrowth = 2.2f;       // final size, in ship sizes

	// The world shake. The HUD's shake numbers, in world units instead of
	// pixels: two frequencies so it does not read as one diagonal slide.
	float shakeIntensity = 0.f;     // 1 is one impact
	float shakePhase = 0.f;
	float shakeAmplitude = 45.f;    // world units at intensity 1
	float shakeDecay = 8.f;         // per second
	const float shakeFrequencyX = 19.f;
	const float shakeFrequencyY = 24.f;

	// Hot core to orange rim. Above 1 on purpose, as the bullet glows are and
	// the plume is not: an explosion should burn out to white at its centre
	// while its edge keeps its orange, and clipping is what does that -- the
	// channels clip at different points along the falloff. At under 1 the
	// first version read as a dim brown smudge once the fade had begun.
	const glm::vec4 coreColor = glm::vec4(1.0f, 0.85f, 0.55f, 1.f) * 2.4f;
	const glm::vec4 fireColor = glm::vec4(1.0f, 0.42f, 0.12f, 1.f) * 1.8f;

	float randomBetween(float a, float b)
	{
		return a + (b - a) * (std::rand() / (float)RAND_MAX);
	}

	// A soft radial falloff: the same shape the plume uses, generated.
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
				const float r = std::min(1.f, std::sqrt(dx * dx + dy * dy));
				const float a = (1.f - r) * (1.f - r);
				unsigned char *p = pixels.data() + ((size_t)y * size + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(a * 255.f);
			}
		}
		glow.createFromBuffer((const char *)pixels.data(), size, size, false, true);
		return glow.id != 0;
	}
}

namespace
{
	bool buildSectorTexture()
	{
		std::vector<unsigned char> pixels((size_t)sectorLength * sectorBreadth * 4);
		const float halfAngle = 0.785398f; // 45 degrees
		const float edgeSoftness = 0.08f;  // radians of fade at the sides
		for (int y = 0; y < sectorBreadth; y++)
		{
			for (int x = 0; x < sectorLength; x++)
			{
				const float fx = (x + 0.5f) / sectorLength;               // 0 .. 1 along
				const float fy = (y + 0.5f) / sectorBreadth * 2.f - 1.f;  // -1 .. 1 across
				const float r = std::sqrt(fx * fx + fy * fy);
				const float angle = std::fabs(std::atan2(fy, fx));
				const float side = std::clamp((halfAngle - angle) / edgeSoftness, 0.f, 1.f);
				const float reach = std::clamp(1.f - r, 0.f, 1.f);
				const float a = side * std::pow(reach, 0.6f);
				unsigned char *p = pixels.data() + ((size_t)y * sectorLength + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(std::clamp(a, 0.f, 1.f) * 255.f);
			}
		}
		sector.createFromBuffer((const char *)pixels.data(), sectorLength, sectorBreadth, false, true);
		return sector.id != 0;
	}
}

bool init()
{
	if (!buildGlowTexture() || !buildSectorTexture())
	{
		std::cerr << "effects: could not create the fireball or sight textures\n";
		return false;
	}
	return true;
}

void cleanup()
{
	glow.cleanup();
	sector.cleanup();
}

void drawSight(wgpu2d::Renderer2D &renderer, const Enemy &enemy)
{
	if (sector.id == 0 || enemy.stunned > 0.f) { return; }

	glm::vec4 color;
	switch (enemy.awareness)
	{
	case Enemy::Awareness::Unaware:   color = glm::vec4(0.55f, 0.60f, 0.75f, 1.f) * 0.10f; break;
	case Enemy::Awareness::Searching: color = glm::vec4(1.00f, 0.65f, 0.15f, 1.f) * 0.16f; break;
	case Enemy::Awareness::Engaged:   color = glm::vec4(1.00f, 0.22f, 0.15f, 1.f) * 0.20f; break;
	}
	color.a = 1.f;

	// The texture's 45 degrees fills its breadth; tan(angle) of that is the
	// squash for this enemy's own cone.
	const float range = enemy.sightRange;
	const float breadth = 2.f * range * std::tan(enemy.sightHalfAngle);
	const glm::vec2 dir = enemy.viewDirection;
	const glm::vec2 centre = enemy.position + dir * (range * 0.5f);
	const float rotation = glm::degrees(std::atan2(-dir.y, dir.x));
	renderer.renderRectangle({centre - glm::vec2(range * 0.5f, breadth * 0.5f), range, breadth},
		sector, color, {}, rotation);
}

void drawAwareness(wgpu2d::Renderer2D &renderer, const Enemy &enemy, float time)
{
	glm::vec4 color;
	switch (enemy.awareness)
	{
	case Enemy::Awareness::Unaware: return;
	case Enemy::Awareness::Engaged:
		color = {1.0f, 0.22f, 0.16f, 1.f};
		break;
	case Enemy::Awareness::Searching:
		color = {1.0f, 0.68f, 0.18f, 0.55f + 0.45f * std::sin(time * 8.f)};
		break;
	}

	// Above the ship on screen, whatever way the ship faces.
	const float size = enemyShipSize * 0.14f;
	const glm::vec2 at = enemy.position + glm::vec2(0.f, -enemyShipSize * 0.72f);
	renderer.renderRectangle({at - glm::vec2(size * 0.5f), size, size}, color, {}, 45.f);
}

void reset()
{
	pieces.clear();
	blasts.clear();
	ghosts.clear();
	streaks.clear();
	shakeIntensity = 0.f;
}

void ramTrail(glm::vec2 shipPos, glm::vec2 direction, float shipSize, glm::vec4 shipCell,
	float gameDeltaTime)
{
	ghostTimer -= gameDeltaTime;
	if (ghostTimer <= 0.f)
	{
		ghosts.push_back({shipPos, direction, shipCell, shipSize, 0.f});
		ghostTimer += ghostEvery;
	}

	// Laid around the ship, ahead of and beside it, so it is about to rush
	// past them rather than leaving them already behind.
	const glm::vec2 side = {-direction.y, direction.x};
	streakTimer -= gameDeltaTime;
	while (streakTimer <= 0.f)
	{
		Streak s;
		s.position = shipPos + side * randomBetween(-1.3f, 1.3f) * shipSize
			+ direction * randomBetween(-0.5f, 1.5f) * shipSize;
		s.direction = direction;
		s.length = randomBetween(250.f, 550.f);
		s.age = 0.f;
		streaks.push_back(s);
		streakTimer += 1.f / streaksPerSecond;
	}
}

void drawAfterimages(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet)
{
	for (const Ghost &g : ghosts)
	{
		const float fade = 1.f - g.age / ghostLife;
		// Pale and cool, and fading fast: a trace of where it was, not a ship.
		const glm::vec4 tint = {0.75f, 0.90f, 1.0f, 0.45f * fade};
		renderSpaceShip(renderer, g.position, g.size, shipSheet, g.uv, g.facing, tint);
	}
}

void shake(float strength)
{
	shakeIntensity = std::min(1.5f, shakeIntensity + strength);
	shakePhase = 0.f; // each impact restarts the oscillation, as the HUD's does
}

glm::vec2 shakeOffset(float realDeltaTime)
{
	if (shakeIntensity <= 0.f) { return {}; }
	const float dt = std::min(realDeltaTime, 0.1f);
	shakePhase += dt;
	shakeIntensity *= std::exp(-shakeDecay * dt);
	if (shakeIntensity < 0.002f) { shakeIntensity = 0.f; return {}; }

	const float amplitude = shakeAmplitude * shakeIntensity;
	return {amplitude * std::sin(6.2831853f * shakeFrequencyX * shakePhase),
		amplitude * std::sin(6.2831853f * shakeFrequencyY * shakePhase + 1.1f)};
}

void enemyKilled(const Enemy &enemy, glm::vec4 cell)
{
	blasts.push_back({enemy.position, enemyShipSize, 0.f});

	// The same angle renderSpaceShip draws the hull at, so the pieces start
	// where that part of the hull was. The renderer rotates in its flipped
	// (y-up) space: an angle t maps +x to (cos t, -sin t) on screen.
	const float angleDegrees = glm::degrees(std::atan2(enemy.viewDirection.y, -enemy.viewDirection.x)) + 90.f;
	const float t = glm::radians(angleDegrees);
	const float c = std::cos(t);
	const float s = std::sin(t);

	const glm::vec2 pieceSize = {enemyShipSize / piecesAcross, enemyShipSize / piecesDown};

	for (int j = 0; j < piecesDown; j++)
	{
		for (int i = 0; i < piecesAcross; i++)
		{
			// Where this piece sits in the unrotated sprite, from its centre.
			const glm::vec2 local = {
				((i + 0.5f) / piecesAcross - 0.5f) * enemyShipSize,
				((j + 0.5f) / piecesDown - 0.5f) * enemyShipSize};
			const glm::vec2 offset = {local.x * c + local.y * s, -local.x * s + local.y * c};

			Piece p;
			p.position = enemy.position + offset;
			p.size = pieceSize;

			// Its part of the cell. The atlas gives (left, top, right, bottom)
			// in the renderer's own convention, so interpolating each edge keeps
			// whatever way up that is.
			const float u0 = (float)i / piecesAcross, u1 = (float)(i + 1) / piecesAcross;
			const float v0 = (float)j / piecesDown, v1 = (float)(j + 1) / piecesDown;
			p.uv = {cell.x + (cell.z - cell.x) * u0, cell.y + (cell.w - cell.y) * v0,
				cell.x + (cell.z - cell.x) * u1, cell.y + (cell.w - cell.y) * v1};

			// Outward from the centre, scattered, carrying the ship's own drift.
			const float outLength = glm::length(offset);
			glm::vec2 out = outLength > 0.001f ? offset / outLength : glm::vec2(1.f, 0.f);
			const float scatter = randomBetween(-0.6f, 0.6f);
			out = {out.x * std::cos(scatter) - out.y * std::sin(scatter),
				out.x * std::sin(scatter) + out.y * std::cos(scatter)};
			p.velocity = out * randomBetween(debrisSpeedMin, debrisSpeedMax) + enemy.velocity * 0.5f;

			p.angle = angleDegrees;
			p.spin = randomBetween(-debrisSpinMax, debrisSpinMax);
			p.age = 0.f;
			p.fadeStart = -1.f;
			pieces.push_back(p);
		}
	}
}

void update(float gameDeltaTime)
{
	const float drag = std::exp(-debrisDrag * gameDeltaTime);
	const float settle = 1.f - std::exp(-gameDeltaTime); // spin eases toward rest
	for (Piece &p : pieces)
	{
		p.age += gameDeltaTime;
		p.position += p.velocity * gameDeltaTime;
		p.velocity *= drag;
		p.angle += p.spin * gameDeltaTime;

		const float rest = std::copysign(std::min(std::fabs(p.spin), debrisSpinRest), p.spin);
		p.spin += (rest - p.spin) * settle;
	}

	// Oldest first in the list, so the pieces past the limit are the first
	// ones. They start fading; the kept ones never do.
	const int excess = (int)pieces.size() - debrisKept;
	for (int i = 0; i < excess; i++)
	{
		if (pieces[i].fadeStart < 0.f) { pieces[i].fadeStart = pieces[i].age; }
	}
	pieces.erase(std::remove_if(pieces.begin(), pieces.end(),
		[](const Piece &p) { return p.fadeStart >= 0.f && p.age - p.fadeStart >= debrisFadeOut; }),
		pieces.end());

	for (Blast &b : blasts) { b.age += gameDeltaTime; }
	blasts.erase(std::remove_if(blasts.begin(), blasts.end(),
		[](const Blast &b) { return b.age >= blastLife; }), blasts.end());

	for (Ghost &g : ghosts) { g.age += gameDeltaTime; }
	ghosts.erase(std::remove_if(ghosts.begin(), ghosts.end(),
		[](const Ghost &g) { return g.age >= ghostLife; }), ghosts.end());

	for (Streak &s : streaks) { s.age += gameDeltaTime; }
	streaks.erase(std::remove_if(streaks.begin(), streaks.end(),
		[](const Streak &s) { return s.age >= streakLife; }), streaks.end());
}

void drawDebris(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet)
{
	for (const Piece &p : pieces)
	{
		// Burnt, and darkening as it settles, so a wreck field sits back in the
		// scene instead of competing with live ships. Opaque unless it is one
		// of the oldest being retired.
		const float settled = std::min(1.f, p.age / debrisSettle);
		const float shade = 0.8f - 0.25f * settled;
		const float alpha = p.fadeStart < 0.f ? 1.f
			: std::clamp(1.f - (p.age - p.fadeStart) / debrisFadeOut, 0.f, 1.f);
		const glm::vec4 tint = {shade, shade * 0.9f, shade * 0.82f, alpha};
		renderer.renderRectangle({p.position - p.size * 0.5f, p.size.x, p.size.y},
			shipSheet, tint, {}, p.angle, p.uv);
	}
}

void drawGlow(wgpu2d::Renderer2D &renderer)
{
	if (glow.id == 0) { return; }

	// Streaks: the soft glow stretched long and thin along the ram. The glow
	// texture is round, so the quad's shape is the streak's.
	for (const Streak &s : streaks)
	{
		const float fade = 1.f - s.age / streakLife;
		glm::vec4 color = streakColor * (1.2f * fade);
		color.a = 1.f;
		const float rotation = glm::degrees(std::atan2(-s.direction.y, s.direction.x));
		renderer.renderRectangle({s.position - glm::vec2(s.length * 0.5f, streakWidth * 0.5f),
			s.length, streakWidth}, glow, color, {}, rotation);
	}
	for (const Blast &b : blasts)
	{
		const float t = b.age / blastLife;              // 0 .. 1
		const float swell = 1.f - (1.f - t) * (1.f - t); // fast out, easing in
		const float fade = (1.f - t) * (1.f - t);

		// The fire: large, orange, fading as it spreads.
		const float fireSize = b.size * (0.5f + blastGrowth * swell);
		glm::vec4 fire = fireColor * fade;
		fire.a = 1.f;
		renderer.renderRectangle({b.position - glm::vec2(fireSize * 0.5f), fireSize, fireSize}, glow, fire);

		// The flash: small, white-hot, gone in the first third.
		const float flashFade = std::max(0.f, 1.f - t * 3.f);
		if (flashFade > 0.f)
		{
			const float flashSize = b.size * (0.6f + 0.6f * swell);
			glm::vec4 flash = coreColor * flashFade;
			flash.a = 1.f;
			renderer.renderRectangle({b.position - glm::vec2(flashSize * 0.5f), flashSize, flashSize}, glow, flash);
		}
	}
}

void drawTargetBox(wgpu2d::Renderer2D &renderer, glm::vec2 centre, float size, float time)
{
	// Dashes marching clockwise round the box: each side is a run of dashes
	// along it, and the whole pattern slides with `time`, continuous across the
	// corners because the four sides are walked as one loop.
	const float half = size * 0.5f;
	const glm::vec2 corners[5] = {
		centre + glm::vec2(-half, -half), centre + glm::vec2(half, -half),
		centre + glm::vec2(half, half), centre + glm::vec2(-half, half),
		centre + glm::vec2(-half, -half)};

	const float thickness = size * 0.035f;
	const float period = size * 0.2f;       // dash plus gap
	const float dash = period * 0.6f;
	const float march = std::fmod(time * size * 0.8f, period);
	const float pulse = 0.75f + 0.25f * std::sin(time * 10.f);
	const glm::vec4 color = {1.0f, 0.18f, 0.16f, pulse};

	float along = -march; // distance round the loop where the next dash starts
	for (int side = 0; side < 4; side++)
	{
		const glm::vec2 a = corners[side];
		const glm::vec2 b = corners[side + 1];
		const glm::vec2 dir = (b - a) / size;
		const float sideStart = side * size;

		for (float s = along; s < sideStart + size; s += period)
		{
			const float from = std::max(s, sideStart) - sideStart;
			const float to = std::min(s + dash, sideStart + size) - sideStart;
			if (to <= from) { continue; }

			const glm::vec2 p0 = a + dir * from;
			const glm::vec2 p1 = a + dir * to;
			const glm::vec2 lo = glm::min(p0, p1) - glm::vec2(thickness * 0.5f);
			const glm::vec2 hi = glm::max(p0, p1) + glm::vec2(thickness * 0.5f);
			renderer.renderRectangle(glm::vec4{lo, hi - lo}, color);
		}
		// Carry the pattern on: the first dash of the next side continues from
		// where this side's pattern would have gone next.
		while (along + period <= sideStart + size) { along += period; }
	}
}

void debugUi()
{
	ImGui::Text("%d pieces, %d fireballs", (int)pieces.size(), (int)blasts.size());
	ImGui::SliderInt("Debris kept", &debrisKept, 0, 3000);
	if (ImGui::SmallButton("Clear debris")) { pieces.clear(); }
	ImGui::SliderFloat("Debris speed", &debrisSpeedMax, 100.f, 3000.f, "%.0f");
	ImGui::SliderFloat("Debris spin", &debrisSpinMax, 0.f, 1440.f, "%.0f deg/s");
	ImGui::SliderFloat("Fireball life", &blastLife, 0.1f, 2.f, "%.2f s");
	ImGui::SliderFloat("Shake size", &shakeAmplitude, 0.f, 200.f, "%.0f");
	ImGui::SliderFloat("Shake decay", &shakeDecay, 1.f, 30.f, "%.1f /s");
	if (ImGui::SmallButton("Test shake")) { shake(1.f); }
	ImGui::SliderFloat("Fireball size", &blastGrowth, 0.2f, 5.f, "%.1f");
}

}
