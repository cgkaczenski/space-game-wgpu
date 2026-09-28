#include <hud.h>

#include <bulletLook.h>
#include <shipShield.h>
#include <glui/glui.h>      // layout only (Frame, Box)
#include <platformTools.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace hud
{

namespace
{
	wgpu2d::Texture healthBarTexture;
	wgpu2d::Texture healthTexture;

	// The health art hue-shifted to blue, frame untouched. Its own files rather
	// than a tint: the renderer's tint multiplies, and a red fill multiplied by
	// blue keeps only the fill's small blue channel -- it comes out near black.
	wgpu2d::Texture energyBarTexture;
	wgpu2d::Texture energyTexture;

	// Where the bar sits, as fractions of the window so it lands the same
	// place at any size.
	const float barLeftPerc = 0.65f;
	const float barTopPerc = 0.1f;
	const float barWidthPerc = 0.3f;
	const float barAspect = 1.f / 8.f;

	// The energy bar sits under the health bar, this many bar-heights down.
	const float energyBarStep = 1.3f;

	// A background, and the fill clipped to `fraction` with its texture
	// coordinates clipped to match so the art does not stretch.
	void drawBar(wgpu2d::Renderer2D &renderer, glm::vec4 rect,
		wgpu2d::Texture frame, wgpu2d::Texture fill, float fraction)
	{
		renderer.renderRectangle(rect, frame);

		glm::vec4 fillRect = rect;
		fillRect.z *= fraction;

		glm::vec4 fillCoords = {0, 1, 1, 0};
		fillCoords.z *= fraction;

		renderer.renderRectangle(fillRect, fill, Colors_White, {}, {}, fillCoords);
	}

	// ---- The gate's arrow -----------------------------------------------

	bool pointerShown = false;
	glm::vec2 pointerTarget = {};
	float pointerPulse = 0.f;
	glm::vec3 pointerColour = {1.f, 1.f, 1.f};

	const float pointerSizePerc = 0.035f;  // of the window height
	const float pointerInsetPerc = 0.07f;  // from the edge, of the height

	void drawPointer(wgpu2d::Renderer2D &renderer, int width, int height)
	{
		if (!pointerShown) { return; }
		const glm::vec2 size = {(float)width, (float)height};
		const float inset = height * pointerInsetPerc;

		// On screen, the gate speaks for itself.
		if (pointerTarget.x >= 0.f && pointerTarget.y >= 0.f
			&& pointerTarget.x <= size.x && pointerTarget.y <= size.y) { return; }

		// Along the ray from the centre, stopped at the inset rectangle: the
		// ray's length to each pair of edges, and whichever it meets first.
		const glm::vec2 centre = size * 0.5f;
		const glm::vec2 toward = pointerTarget - centre;
		const float length = glm::length(toward);
		if (length <= 0.f) { return; }
		const glm::vec2 dir = toward / length;
		const glm::vec2 half = centre - glm::vec2(inset);
		float reach = 1e9f;
		if (dir.x != 0.f) { reach = std::min(reach, half.x / std::abs(dir.x)); }
		if (dir.y != 0.f) { reach = std::min(reach, half.y / std::abs(dir.y)); }
		const glm::vec2 tip = centre + dir * reach;

		// A chevron: two strokes back from the tip, 40 degrees either side.
		const float arm = height * pointerSizePerc;
		const float c = std::cos(2.44f), s = std::sin(2.44f); // 140 degrees
		const glm::vec2 left = {dir.x * c - dir.y * s, dir.x * s + dir.y * c};
		const glm::vec2 right = {dir.x * c + dir.y * s, -dir.x * s + dir.y * c};
		const float bright = 0.45f + 0.55f * pointerPulse;
		const glm::vec4 colour = {pointerColour * bright, 1.f};
		const float stroke = std::max(2.f, height * 0.006f);
		renderer.renderLine(tip, tip + left * arm, colour, stroke);
		renderer.renderLine(tip, tip + right * arm, colour, stroke);
	}

	// ---- Weapon slots ---------------------------------------------------
	//
	// Plain rectangles and the bullets' own art: no new textures. Sized by the
	// window's height, so the row is the same fraction of the screen at any
	// size, and centred along the bottom.
	const float slotSizePerc = 0.085f;    // of the window height
	const float slotGapPerc = 0.25f;      // of a slot
	const float slotBottomPerc = 0.05f;   // clearance under the row, of the height

	const glm::vec4 slotFrame = {0.35f, 0.35f, 0.48f, 0.9f};
	const glm::vec4 slotFrameSelected = {1.0f, 0.86f, 0.55f, 1.f};
	const glm::vec4 slotBackground = {0.05f, 0.04f, 0.11f, 0.85f};
	const glm::vec4 cooldownShade = {0.f, 0.f, 0.f, 0.6f};
	const glm::vec4 unusableShade = {0.f, 0.f, 0.f, 0.65f};
	const glm::vec4 pipFull = {0.55f, 1.0f, 0.40f, 1.f};
	const glm::vec4 pipEmpty = {0.18f, 0.20f, 0.22f, 0.9f};

	void drawSlots(wgpu2d::Renderer2D &renderer, const WeaponSlot *slots, int count,
		float ramReady, int width, int height)
	{
		if (!slots || count <= 0) { return; }

		const float size = height * slotSizePerc;
		const float gap = size * slotGapPerc;
		const float total = count * size + (count - 1) * gap;
		const float left = (width - total) * 0.5f;
		const float top = height - size - height * slotBottomPerc;

		// The ram, set apart to the left: not a weapon you select, a move you
		// make, so it sits outside the row.
		{
			const float x = left - size - gap * 3.f;
			const float border = size * 0.035f;
			renderer.renderRectangle(glm::vec4{x - border, top - border,
				size + 2.f * border, size + 2.f * border}, slotFrame);
			renderer.renderRectangle(glm::vec4{x, top, size, size}, slotBackground);
			shield::drawIcon(renderer, {x + size * 0.5f, top + size * 0.5f}, size * 0.8f);
			if (ramReady < 1.f)
			{
				renderer.renderRectangle(glm::vec4{x, top, size, size * (1.f - ramReady)}, cooldownShade);
			}
		}

		for (int i = 0; i < count; i++)
		{
			const WeaponSlot &s = slots[i];
			const float x = left + i * (size + gap);
			const float border = size * (s.selected ? 0.08f : 0.035f);

			renderer.renderRectangle(glm::vec4{x - border, top - border,
				size + 2.f * border, size + 2.f * border},
				s.selected ? slotFrameSelected : slotFrame);
			renderer.renderRectangle(glm::vec4{x, top, size, size}, slotBackground);

			bulletLook::drawIcon(renderer, {x + size * 0.5f, top + size * 0.5f}, size * 0.8f, s.style);

			// Cooling down, the icon is shaded from the top and uncovered as it
			// recharges; one that cannot fire at all is shaded throughout.
			if (!s.usable)
			{
				renderer.renderRectangle(glm::vec4{x, top, size, size}, unusableShade);
			}
			else if (s.ready < 1.f)
			{
				renderer.renderRectangle(glm::vec4{x, top, size, size * (1.f - s.ready)}, cooldownShade);
			}

			if (s.maxAmmo > 0)
			{
				const float pip = size * 0.12f;
				const float pipGap = pip * 0.5f;
				const float row = s.maxAmmo * pip + (s.maxAmmo - 1) * pipGap;
				const float pipLeft = x + (size - row) * 0.5f;
				const float pipTop = top + size + border + pip * 0.6f;
				for (int a = 0; a < s.maxAmmo; a++)
				{
					renderer.renderRectangle(glm::vec4{pipLeft + a * (pip + pipGap), pipTop, pip, pip},
						a < s.ammo ? pipFull : pipEmpty);
				}
			}
		}
	}

	// ---- The damage shake -----------------------------------------------
	//
	// The mechanism is wgpu2d::LayerEffect. Everything below is how this
	// game's HUD reacts to being hit.

	wgpu2d::LayerEffect shake;

	// 1 right after a hit, decaying towards 0. Time is wall-clock, so the
	// shake is unaffected by the game-speed debug slider.
	float intensity = 0.f;
	float phase = 0.f;
	std::chrono::steady_clock::time_point lastTime;
	bool timing = false;

	// How the shake feels: how fast it dies away, how far it moves, how fast
	// it oscillates, how far it tips. Two slightly different frequencies per
	// axis keep it from looking like a single diagonal slide.
	const float decayPerSecond = 10.f;   // e ^ -(10 t): gone in about a third of a second
	const float amplitudePixels = 9.f;   // at 1x, before the window scale below
	const float frequencyX = 19.f;
	const float frequencyY = 24.f;
	const float tiltDegrees = 1.4f;

	float advanceClock()
	{
		const auto now = std::chrono::steady_clock::now();
		if (!timing)
		{
			timing = true;
			lastTime = now;
			return 0.f;
		}
		const float dt = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		// A breakpoint, a resize or a stalled frame should not teleport the
		// shake through its whole life in one step.
		return dt > 0.1f ? 0.1f : dt;
	}

	wgpu2d::LayerTransform advanceShake(int height)
	{
		const float dt = advanceClock();
		if (intensity > 0.f)
		{
			phase += dt;
			intensity *= std::exp(-decayPerSecond * dt);
			// Settle to exactly zero, which is what earns LayerEffect's
			// identity bypass and keeps the HUD pixel-exact at rest.
			if (intensity < 0.002f) { intensity = 0.f; }
		}

		wgpu2d::LayerTransform transform;
		if (intensity > 0.f)
		{
			// Scaled with the window so the shake is the same fraction of the
			// screen at any size.
			const float scale = (float)height / 500.f;
			const float amplitude = amplitudePixels * scale * intensity;
			transform.offsetPixels.x = amplitude * std::sin(6.2831853f * frequencyX * phase);
			transform.offsetPixels.y = amplitude * std::sin(6.2831853f * frequencyY * phase + 1.1f);
			transform.rotationDegrees = tiltDegrees * intensity * std::sin(6.2831853f * frequencyX * phase);
		}
		return transform;
	}
}

bool init()
{
	healthBarTexture.loadFromFile(RESOURCES_PATH "healthBar.png", true);
	healthTexture.loadFromFile(RESOURCES_PATH "health.png", true);
	energyBarTexture.loadFromFile(RESOURCES_PATH "energyBar.png", true);
	energyTexture.loadFromFile(RESOURCES_PATH "energy.png", true);

	if (healthBarTexture.id == 0 || healthTexture.id == 0)
	{
		std::cerr << "HUD: failed to load the health bar textures\n";
		return false;
	}
	if (energyBarTexture.id == 0 || energyTexture.id == 0)
	{
		std::cerr << "HUD: failed to load the energy bar textures\n";
		return false;
	}
	return true;
}

void cleanup()
{
	shake.cleanup();
	healthBarTexture.cleanup();
	healthTexture.cleanup();
	energyBarTexture.cleanup();
	energyTexture.cleanup();
}

void reset()
{
	intensity = 0.f;
	phase = 0.f;
	// Restart the clock too, so the first frame of the round does not see the
	// time since the last hit as one long step.
	timing = false;
}

void onDamage(float strength)
{
	if (strength <= 0.f) { return; }

	// Hits stack up to a full-strength shake, and each one restarts the
	// oscillation so a second hit reads as a second impact.
	intensity += strength;
	if (intensity > 1.f) { intensity = 1.f; }
	phase = 0.f;
}

void pointTo(bool shown, glm::vec2 target, float pulse, glm::vec3 colour)
{
	pointerShown = shown;
	pointerTarget = target;
	pointerPulse = pulse;
	pointerColour = colour;
}

void draw(wgpu2d::Renderer2D &renderer, float health, float energy,
	const WeaponSlot *slots, int slotCount, float ramReady, int width, int height)
{
	// The layer below this one -- the world -- goes to the screen first, so
	// the shake's target holds the HUD alone.
	renderer.flush();

	// Screen space, not the world camera: the bar is laid out against the
	// window, and the caller is still in whatever camera it drew the world in.
	renderer.pushCamera();
	{
		glui::Frame frame({0, 0, (float)width, (float)height});

		glui::Box bar = glui::Box().xLeftPerc(barLeftPerc).yTopPerc(barTopPerc)
			.xDimensionPercentage(barWidthPerc).yAspectRatio(barAspect);

		const glm::vec4 healthRect = bar();
		drawBar(renderer, healthRect, healthBarTexture, healthTexture, health);

		glm::vec4 energyRect = healthRect;
		energyRect.y += healthRect.w * energyBarStep;
		drawBar(renderer, energyRect, energyBarTexture, energyTexture, energy);

		drawSlots(renderer, slots, slotCount, ramReady, width, height);
		drawPointer(renderer, width, height);
		pointerShown = false; // for one draw; the game says so every frame
	}
	renderer.popCamera();

	// Through the shake when one is running; straight to the screen when it is
	// not, which LayerEffect decides from the transform.
	shake.flush(renderer, width, height, advanceShake(height));
}

}
