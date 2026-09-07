#include <render/hudShake.h>
#include <render/layerEffect.h>

#include <chrono>
#include <cmath>

namespace render
{

namespace
{
	// The mechanism. Everything else in this file is this game's numbers.
	LayerEffect effect;

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
	const float rotationDegrees = 1.4f;

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
}

void hudShakeTrigger(float strength)
{
	if (strength <= 0.f) { return; }

	// Hits stack up to a full-strength shake, and each one restarts the
	// oscillation so a second hit reads as a second impact.
	intensity += strength;
	if (intensity > 1.f) { intensity = 1.f; }
	phase = 0.f;
}

void hudShakeFlush(wgpu2d::Renderer2D &renderer, int width, int height)
{
	const float dt = advanceClock();
	if (intensity > 0.f)
	{
		phase += dt;
		intensity *= std::exp(-decayPerSecond * dt);
		// Settle to exactly zero, which is what earns the identity bypass in
		// LayerEffect::flush and keeps the HUD pixel-exact at rest.
		if (intensity < 0.002f) { intensity = 0.f; }
	}

	LayerTransform transform;
	if (intensity > 0.f)
	{
		// The displacement, scaled with the window so the shake is the same
		// fraction of the screen at any size.
		const float scale = (float)height / 500.f;
		const float amplitude = amplitudePixels * scale * intensity;
		transform.offsetPixels.x = amplitude * std::sin(6.2831853f * frequencyX * phase);
		transform.offsetPixels.y = amplitude * std::sin(6.2831853f * frequencyY * phase + 1.1f);
		transform.rotationDegrees = rotationDegrees * intensity * std::sin(6.2831853f * frequencyX * phase);
	}

	effect.flush(renderer, width, height, transform);
}

}
