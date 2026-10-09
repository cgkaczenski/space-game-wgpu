#include <hud.h>
#include <vector>

#include <bulletLook.h>
#include <shipShield.h>
#include <glui/glui.h>      // layout only (Frame, Box)
#include <platformTools.h>
#include <textLook.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

	// ---- The mode slot (M1) ---------------------------------------------

	bool modeShown = false;
	bool modeFlight = false;
	const glm::vec4 modeFlightShade = {0.f, 0.f, 0.f, 0.6f};
	const glm::vec4 modeFlightChevron = {0.55f, 0.95f, 1.f, 1.f};

	// ---- Text (U1) ------------------------------------------------------

	bool haulShown = false;
	int haulHeld = 0;
	int haulBanked = 0;
	const glm::vec4 haulColour = {0.85f, 0.95f, 1.f, 0.9f};
	const float haulGapPerc = 0.25f; // under the energy bar, of a bar's height

	void drawText(wgpu2d::Renderer2D &renderer, glm::vec4 energyRect, int height)
	{
		const float scale = textLook::screenScale(height);
		if (haulShown)
		{
			char line[64];
			std::snprintf(line, sizeof(line), "HOLD %d   BANKED %d", haulHeld, haulBanked);
			textLook::draw(renderer, {energyRect.x, energyRect.y + energyRect.w * (1.f + haulGapPerc)},
				line, haulColour, scale);
		}
	}

	// ---- The gate's arrow -----------------------------------------------

	bool pointerShown = false;
	glm::vec2 pointerTarget = {};
	float pointerPulse = 0.f;
	glm::vec3 pointerColour = {1.f, 1.f, 1.f};

	const float pointerSizePerc = 0.035f;  // of the window height
	const float pointerInsetPerc = 0.07f;  // from the edge, of the height

	// Off-screen markers for this draw (sight roadmap S4): the ghosts of
	// enemies last seen off screen, in their colour, a little smaller than
	// the gate's so the two do not read as the same thing.
	struct Marker
	{
		glm::vec2 target;
		glm::vec4 colour;
		float scale;
	};
	std::vector<Marker> markers;

	// A chevron just inside the screen's edge, on the line from the centre to
	// `target` (screen pixels), pointing out toward it -- unless `target` is
	// on screen, where the thing speaks for itself.
	void drawChevron(wgpu2d::Renderer2D &renderer, glm::vec2 target, glm::vec4 colour, float scale,
		int width, int height)
	{
		const glm::vec2 size = {(float)width, (float)height};
		const float inset = height * pointerInsetPerc;
		if (target.x >= 0.f && target.y >= 0.f && target.x <= size.x && target.y <= size.y) { return; }

		// Along the ray from the centre, stopped at the inset rectangle: the
		// ray's length to each pair of edges, and whichever it meets first.
		const glm::vec2 centre = size * 0.5f;
		const glm::vec2 toward = target - centre;
		const float length = glm::length(toward);
		if (length <= 0.f) { return; }
		const glm::vec2 dir = toward / length;
		const glm::vec2 half = centre - glm::vec2(inset);
		float reach = 1e9f;
		if (dir.x != 0.f) { reach = std::min(reach, half.x / std::abs(dir.x)); }
		if (dir.y != 0.f) { reach = std::min(reach, half.y / std::abs(dir.y)); }
		const glm::vec2 tip = centre + dir * reach;

		// A chevron: two strokes back from the tip, 40 degrees either side.
		const float arm = height * pointerSizePerc * scale;
		const float c = std::cos(2.44f), s = std::sin(2.44f); // 140 degrees
		const glm::vec2 left = {dir.x * c - dir.y * s, dir.x * s + dir.y * c};
		const glm::vec2 right = {dir.x * c + dir.y * s, -dir.x * s + dir.y * c};
		const float stroke = std::max(2.f, height * 0.006f * scale);
		renderer.renderLine(tip, tip + left * arm, colour, stroke);
		renderer.renderLine(tip, tip + right * arm, colour, stroke);
	}

	void drawPointer(wgpu2d::Renderer2D &renderer, int width, int height)
	{
		for (const Marker &m : markers) { drawChevron(renderer, m.target, m.colour, m.scale, width, height); }
		if (!pointerShown) { return; }
		const float bright = 0.45f + 0.55f * pointerPulse;
		drawChevron(renderer, pointerTarget, {pointerColour * bright, 1.f}, 1.f, width, height);
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

	// The row's geometry, shared by drawing and by elementRect.
	struct SlotRow
	{
		float size, gap, left, top, total;
	};

	SlotRow slotRow(int count, int width, int height)
	{
		SlotRow r;
		r.size = height * slotSizePerc;
		r.gap = r.size * slotGapPerc;
		r.total = count * r.size + (count - 1) * r.gap;
		r.left = (width - r.total) * 0.5f;
		r.top = height - r.size - height * slotBottomPerc;
		return r;
	}

	// ---- Highlights (hints roadmap H1) ----------------------------------
	//
	// A highlighted element pulses larger about its centre and gets a frame
	// in the hint colour. Real time, so a pause does not freeze a hint's
	// pulse. The text is left its size -- a pixel font only scales by whole
	// numbers -- and gets the frame alone.

	bool highlighted[(int)Element::Count] = {};
	int lastSlotCount = 4;
	const float growBy = 0.14f;        // at the top of the pulse
	const float pulseHz = 1.6f;

	float pulse()
	{
		static const auto start = std::chrono::steady_clock::now();
		const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
		return 0.5f - 0.5f * std::cos(6.2831853f * pulseHz * t);
	}

	float grow(Element e)
	{
		if (!highlighted[(int)e] || e == Element::Haul) { return 1.f; }
		return 1.f + growBy * pulse();
	}

	glm::vec4 grown(glm::vec4 r, float g)
	{
		const glm::vec2 extra = glm::vec2(r.z, r.w) * (g - 1.f);
		return {r.x - extra.x * 0.5f, r.y - extra.y * 0.5f, r.z + extra.x, r.w + extra.y};
	}

	void drawSlots(wgpu2d::Renderer2D &renderer, const WeaponSlot *slots, int count,
		float ramReady, int width, int height)
	{
		if (!slots || count <= 0) { return; }
		lastSlotCount = count;

		const SlotRow row = slotRow(count, width, height);

		// The ram, set apart to the left: not a weapon you select, a move you
		// make, so it sits outside the row.
		{
			const glm::vec4 r = grown({row.left - row.size - row.gap * 3.f, row.top, row.size, row.size}, grow(Element::Ram));
			const float border = r.z * 0.035f;
			renderer.renderRectangle(glm::vec4{r.x - border, r.y - border,
				r.z + 2.f * border, r.w + 2.f * border}, slotFrame);
			renderer.renderRectangle(r, slotBackground);
			shield::drawIcon(renderer, {r.x + r.z * 0.5f, r.y + r.w * 0.5f}, r.z * 0.8f);
			if (ramReady < 1.f)
			{
				renderer.renderRectangle(glm::vec4{r.x, r.y, r.z, r.w * (1.f - ramReady)}, cooldownShade);
			}
		}

		// The mode, set apart to the right, mirroring the ram: not a weapon
		// either, a stance the ship is in.
		if (modeShown)
		{
			const glm::vec4 r = grown({row.left + row.total + row.gap * 3.f, row.top, row.size, row.size}, grow(Element::Mode));
			const float border = r.z * 0.035f;
			renderer.renderRectangle(glm::vec4{r.x - border, r.y - border,
				r.z + 2.f * border, r.w + 2.f * border}, slotFrame);
			renderer.renderRectangle(r, slotBackground);
			shield::drawIcon(renderer, {r.x + r.z * 0.5f, r.y + r.w * 0.5f}, r.z * 0.8f);
			if (modeFlight)
			{
				// The shield put away, and the ship going somewhere: two
				// chevrons pointing up the screen, over the dimmed icon.
				renderer.renderRectangle(r, modeFlightShade);
				const float arm = r.z * 0.28f;
				const float stroke = std::max(2.f, r.z * 0.07f);
				for (int k = 0; k < 2; k++)
				{
					const glm::vec2 tip = {r.x + r.z * 0.5f, r.y + r.w * (0.3f + 0.24f * k)};
					renderer.renderLine(tip, tip + glm::vec2{-arm, arm * 0.8f}, modeFlightChevron, stroke);
					renderer.renderLine(tip, tip + glm::vec2{arm, arm * 0.8f}, modeFlightChevron, stroke);
				}
			}
		}

		for (int i = 0; i < count; i++)
		{
			const WeaponSlot &s = slots[i];
			const float g = i < 4 ? grow((Element)((int)Element::Weapon1 + i)) : 1.f;
			const glm::vec4 r = grown({row.left + i * (row.size + row.gap), row.top, row.size, row.size}, g);
			const float x = r.x, top = r.y, size = r.z;
			const float border = size * (s.selected ? 0.08f : 0.035f);

			renderer.renderRectangle(glm::vec4{x - border, top - border,
				size + 2.f * border, size + 2.f * border},
				s.selected ? slotFrameSelected : slotFrame);
			renderer.renderRectangle(glm::vec4{x, top, size, size}, slotBackground);

			if (s.empty) { continue; } // the frame and the dark, nothing in it
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
				const float rowW = s.maxAmmo * pip + (s.maxAmmo - 1) * pipGap;
				const float pipLeft = x + (size - rowW) * 0.5f;
				const float pipTop = top + size + border + pip * 0.6f;
				for (int a = 0; a < s.maxAmmo; a++)
				{
					renderer.renderRectangle(glm::vec4{pipLeft + a * (pip + pipGap), pipTop, pip, pip},
						a < s.ammo ? pipFull : pipEmpty);
				}
			}
		}
	}

	// A frame round each highlighted element, over everything else in the
	// HUD, brightening with the pulse.
	void drawHighlights(wgpu2d::Renderer2D &renderer, int width, int height)
	{
		const float p = pulse();
		const float thick = std::max(2.f, std::round(height * 0.003f));
		const float gapOut = std::max(3.f, std::round(height * 0.006f));
		const glm::vec4 colour = {textLook::hintColour, 0.55f + 0.45f * p};
		for (int i = 0; i < (int)Element::Count; i++)
		{
			if (!highlighted[i]) { continue; }
			const glm::vec4 r = grown(elementRect((Element)i, width, height), grow((Element)i));
			const glm::vec4 o = {r.x - gapOut - thick, r.y - gapOut - thick, r.z + 2.f * (gapOut + thick), r.w + 2.f * (gapOut + thick)};
			renderer.renderRectangle({o.x, o.y, o.z, thick}, colour);
			renderer.renderRectangle({o.x, o.y + o.w - thick, o.z, thick}, colour);
			renderer.renderRectangle({o.x, o.y + thick, thick, o.w - 2.f * thick}, colour);
			renderer.renderRectangle({o.x + o.z - thick, o.y + thick, thick, o.w - 2.f * thick}, colour);
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

void showMode(bool flight)
{
	modeShown = true;
	modeFlight = flight;
}

void showHaul(int held, int banked)
{
	haulShown = true;
	haulHeld = held;
	haulBanked = banked;
}

void markOffScreen(glm::vec2 target, glm::vec4 colour, float scale)
{
	markers.push_back({target, colour, scale});
}

void highlight(Element element)
{
	if (element != Element::Count) { highlighted[(int)element] = true; }
}

glm::vec4 elementRect(Element element, int width, int height)
{
	glm::vec4 health;
	{
		glui::Frame frame({0, 0, (float)width, (float)height});
		health = glui::Box().xLeftPerc(barLeftPerc).yTopPerc(barTopPerc)
			.xDimensionPercentage(barWidthPerc).yAspectRatio(barAspect)();
	}
	glm::vec4 energy = health;
	energy.y += health.w * energyBarStep;

	const SlotRow row = slotRow(lastSlotCount, width, height);
	switch (element)
	{
	case Element::Weapon1:
	case Element::Weapon2:
	case Element::Weapon3:
	case Element::Weapon4:
	{
		const int i = (int)element - (int)Element::Weapon1;
		return {row.left + i * (row.size + row.gap), row.top, row.size, row.size};
	}
	case Element::Ram: return {row.left - row.size - row.gap * 3.f, row.top, row.size, row.size};
	case Element::Mode: return {row.left + row.total + row.gap * 3.f, row.top, row.size, row.size};
	case Element::Health: return health;
	case Element::Energy: return energy;
	case Element::Haul:
	{
		// The line's own box: drawText's position and the text it prints.
		const float scale = textLook::screenScale(height);
		char line[64];
		std::snprintf(line, sizeof(line), "HOLD %d   BANKED %d", haulHeld, haulBanked);
		const glm::vec2 size = wgpu2d::measureText(textLook::font(), line, scale);
		return {energy.x, energy.y + energy.w * (1.f + haulGapPerc), size.x, size.y};
	}
	case Element::Count: break;
	}
	return {};
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
		const glm::vec4 healthRect = elementRect(Element::Health, width, height);
		drawBar(renderer, grown(healthRect, grow(Element::Health)), healthBarTexture, healthTexture, health);

		const glm::vec4 energyRect = elementRect(Element::Energy, width, height);
		drawBar(renderer, grown(energyRect, grow(Element::Energy)), energyBarTexture, energyTexture, energy);

		drawSlots(renderer, slots, slotCount, ramReady, width, height);
		drawPointer(renderer, width, height);
		drawText(renderer, energyRect, height);
		drawHighlights(renderer, width, height);
		pointerShown = false; // for one draw; the game says so every frame
		modeShown = false;
		haulShown = false;
		markers.clear();
		for (bool &h : highlighted) { h = false; }
	}
	renderer.popCamera();

	// Through the shake when one is running; straight to the screen when it is
	// not, which LayerEffect decides from the transform.
	shake.flush(renderer, width, height, advanceShake(height));
}

}
