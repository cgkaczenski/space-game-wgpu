#include <render/hudShake.h>

#include <chrono>
#include <cmath>

namespace render
{

namespace
{
	// The target the HUD is composed into. Screen-sized and recreated when the
	// window changes, so the composite is 1:1 and, at rest, an exact copy.
	wgpu2d::FrameBuffer hudTarget;

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
	if (width <= 0 || height <= 0)
	{
		renderer.flush();
		return;
	}

	const float dt = advanceClock();
	if (intensity > 0.f)
	{
		phase += dt;
		intensity *= std::exp(-decayPerSecond * dt);
		if (intensity < 0.002f) { intensity = 0.f; } // settle exactly, so rest is 1:1
	}

	// No shake in progress: nothing to gain from the round trip, and drawing
	// straight to the screen keeps the HUD pixel-exact.
	if (intensity <= 0.f)
	{
		renderer.flush();
		return;
	}

	if (hudTarget.fbo == 0)
	{
		hudTarget.create((unsigned)width, (unsigned)height);
		if (hudTarget.fbo == 0) { renderer.flush(); return; } // creation failed; draw normally
	}
	hudTarget.resize((unsigned)width, (unsigned)height);

	// Last frame's HUD must not linger: the target is transparent everywhere
	// the HUD does not cover, and that is what lets the world show through.
	hudTarget.clear();
	renderer.flushFBO(hudTarget);

	// The displacement, scaled with the window so the shake is the same
	// fraction of the screen at any size, and rounded to nothing finer than
	// the sub-pixel the linear filter can show.
	const float scale = (float)height / 500.f;
	const float amplitude = amplitudePixels * scale * intensity;
	const float dx = amplitude * std::sin(6.2831853f * frequencyX * phase);
	const float dy = amplitude * std::sin(6.2831853f * frequencyY * phase + 1.1f);
	const float tilt = rotationDegrees * intensity * std::sin(6.2831853f * frequencyX * phase);

	// One quad, the size of the screen, sampling the composed HUD. The
	// rotation is about the quad's centre, which is the screen's centre.
	//
	// Under the default camera, not the caller's: by here the game has popped
	// back to the world camera (which is offset and zoomed), and the target
	// has to land on the screen 1:1 the way the HUD itself was laid out.
	renderer.pushCamera();
	const wgpu2d::Rect target = {dx, dy, (float)width, (float)height};
	renderer.renderRectangle(target, hudTarget.texture, Colors_White, {}, tilt);
	renderer.popCamera();
}

}
