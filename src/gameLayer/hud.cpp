#include <hud.h>

#include <glui/glui.h>      // layout only (Frame, Box)
#include <platformTools.h>

#include <chrono>
#include <cmath>
#include <iostream>

namespace hud
{

namespace
{
	wgpu2d::Texture healthBarTexture;
	wgpu2d::Texture healthTexture;

	// Where the bar sits, as fractions of the window so it lands the same
	// place at any size.
	const float barLeftPerc = 0.65f;
	const float barTopPerc = 0.1f;
	const float barWidthPerc = 0.3f;
	const float barAspect = 1.f / 8.f;

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

	if (healthBarTexture.id == 0 || healthTexture.id == 0)
	{
		std::cerr << "HUD: failed to load the health bar textures\n";
		return false;
	}
	return true;
}

void cleanup()
{
	shake.cleanup();
	healthBarTexture.cleanup();
	healthTexture.cleanup();
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

void draw(wgpu2d::Renderer2D &renderer, float health, int width, int height)
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

		renderer.renderRectangle(bar, healthBarTexture);

		// The fill is the same box clipped to the health fraction, with its
		// texture coordinates clipped to match so the art does not stretch.
		glm::vec4 fillRect = bar();
		fillRect.z *= health;

		glm::vec4 fillCoords = {0, 1, 1, 0};
		fillCoords.z *= health;

		renderer.renderRectangle(fillRect, healthTexture, Colors_White, {}, {}, fillCoords);
	}
	renderer.popCamera();

	// Through the shake when one is running; straight to the screen when it is
	// not, which LayerEffect decides from the transform.
	shake.flush(renderer, width, height, advanceShake(height));
}

}
