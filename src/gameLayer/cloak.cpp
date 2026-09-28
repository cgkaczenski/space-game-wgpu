#include <cloak.h>

#include <platformTools.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cloak
{

namespace
{
	wgpu2d::Effect effect;

	// The world, when the cloak needs something to sample. Only created the
	// first time it is engaged, so a game that never cloaks never allocates a
	// screen-sized target.
	wgpu2d::FrameBuffer worldTarget;

	bool active = false;
	float level = 0.f; // eased `active`

	// The shimmer's clock: game time, so it freezes when the game does.
	// Wrapped well before float precision would coarsen the ripple, on a
	// whole number of the shader's periods (it runs at 5 rad/s) so the wrap
	// does not jump.
	float shimmerClock = 0.f;

	// The gate's swirl, for the next flush only.
	glm::vec2 swirlAt = {};
	float swirlRadius = 0.f;
	float swirlStrength = 0.f;
	const float swirlMaxRadians = 1.4f; // the turn at the centre, at full strength
	const float shimmerWrap = 1000.f * 6.2831853f / 5.f;

	const float engagePerSecond = 3.5f;
	const float disengagePerSecond = 4.5f;

	// The field, as a multiple of the ship's on-screen radius. Big enough that
	// the bending is not confined to the silhouette, small enough that it
	// reads as attached to the ship rather than as a screen-wide wobble.
	const float radiusPerShipRadius = 2.4f;
	const float maxStrength = 1.0f;

	// What is left of the hull at full cloak. Not zero: the distortion needs
	// something to carry, and an absent ship reads as a bug.
	const float hiddenAlpha = 0.12f;

	bool readFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}
}

bool init()
{
	std::string source;
	const char *path = RESOURCES_PATH "shaders/cloak.wgsl";
	if (!readFile(path, source))
	{
		std::cerr << "cloak: cannot read " << path << "\n";
		return false;
	}

	effect = wgpu2d::createEffect(source.c_str(), "cloak");
	if (effect.id == 0)
	{
		std::cerr << "cloak: effect did not compile\n";
		return false;
	}
	return true;
}

void cleanup()
{
	worldTarget.cleanup();
}

void setActive(bool a) { active = a; }

void setSwirl(glm::vec2 worldPos, float worldRadius, float strength)
{
	swirlAt = worldPos;
	swirlRadius = worldRadius;
	swirlStrength = strength;
}
bool isActive() { return active; }

float shipAlpha()
{
	return 1.f - (1.f - hiddenAlpha) * level;
}

void flushWorld(wgpu2d::Renderer2D &renderer, glm::vec2 shipWorldPos,
	float shipWorldSize, int width, int height, float dt)
{
	const float target = active ? 1.f : 0.f;
	const float rate = active ? engagePerSecond : disengagePerSecond;
	level += (target - level) * std::min(1.f, rate * std::max(0.f, dt));
	shimmerClock = std::fmod(shimmerClock + std::max(0.f, dt), shimmerWrap);
	if (level < 0.004f) { level = 0.f; } // settle exactly, so down is free

	// The swirl is taken for this flush and cleared, so a gate that stops
	// asking stops swirling. Only if its field reaches the view: off screen it
	// would bend nothing and still cost the copy.
	const glm::vec4 view = renderer.getViewRect();
	const float swirl = swirlStrength;
	swirlStrength = 0.f;
	const bool swirlShows = swirl > 0.f && swirlRadius > 0.f
		&& swirlAt.x + swirlRadius > view.x && swirlAt.x - swirlRadius < view.x + view.z
		&& swirlAt.y + swirlRadius > view.y && swirlAt.y - swirlRadius < view.y + view.w;

	// Down, or nothing usable: this is an ordinary flush and costs nothing.
	// The round trip is skipped entirely rather than run with strength 0,
	// because a full-screen copy is not free and an idle feature should not
	// charge for itself.
	if ((level <= 0.f && !swirlShows) || effect.id == 0 || width <= 0 || height <= 0)
	{
		renderer.flush();
		return;
	}

	if (worldTarget.fbo == 0)
	{
		worldTarget.create((unsigned)width, (unsigned)height);
		if (worldTarget.fbo == 0) { renderer.flush(); return; } // no target; draw normally
	}
	worldTarget.resize((unsigned)width, (unsigned)height);

	// The world is opaque and covers the target, but clearing is still right:
	// last frame's contents are not this frame's, and the first frame after a
	// resize would otherwise sample undefined memory.
	worldTarget.clear();
	renderer.flushFBO(worldTarget);

	// The shader works in screen pixels; the caller thinks in world units.
	// getViewRect is the visible world rectangle, so this is the same mapping
	// the projection does, done once on the CPU for one point.
	glm::vec2 screenPos = {(float)width * 0.5f, (float)height * 0.5f};
	float screenPerWorld = 1.f;
	if (view.z != 0.f && view.w != 0.f)
	{
		screenPos.x = (shipWorldPos.x - view.x) / view.z * (float)width;
		screenPos.y = (shipWorldPos.y - view.y) / view.w * (float)height;
		screenPerWorld = (float)width / view.z;
	}
	const float shipScreenRadius = shipWorldSize * 0.5f * screenPerWorld;

	wgpu2d::EffectParams params;
	params.a = {screenPos.x, screenPos.y,
		shipScreenRadius * radiusPerShipRadius, level * maxStrength};
	params.b = {shimmerClock, 0.f, 0.f, 0.f};
	if (swirlShows && view.z != 0.f && view.w != 0.f)
	{
		params.c = {(swirlAt.x - view.x) / view.z * (float)width,
			(swirlAt.y - view.y) / view.w * (float)height,
			swirlRadius * screenPerWorld, swirl * swirlMaxRadians};
	}

	renderer.drawFullscreenEffect(worldTarget.texture, effect, params);
}

}
