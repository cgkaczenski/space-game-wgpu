#pragma once

// Damage numbers (gameplay roadmap U1): what a hit took off an enemy, shown
// where it landed, rising and fading.
//
// Hits on the same enemy close together add up into one number rather than
// stacking a column of them -- which is what makes a beam, a hit every frame,
// read as one climbing figure. Life is a fraction of a hull, so the number is
// it times 100: a 0.1 shot reads 10, and a standard enemy is 100.
//
// Drawn in screen space, after the world and under the HUD: the font is a
// pixel font and only stays crisp on the screen's own pixel grid, which the
// world camera's zoom would take it off. It does not shake with the HUD.

#include <render/wgpu2d.h>
#include <functional>

namespace damageNumbers
{
	// A new round: none left over.
	void reset();

	// The enemy `id` lost `amount` of its life at `at` (world).
	void hit(unsigned int id, glm::vec2 at, float amount);

	// Game time, so a pause freezes them mid-rise.
	void update(float gameDeltaTime);

	// Whether a number at a world point may be drawn: one over an enemy the
	// player cannot see would give it away. Empty: all of them.
	using Shown = std::function<bool(glm::vec2)>;

	// `view` is the world camera's visible rectangle (getViewRect), which
	// places each number on the `width` x `height` screen. Call with the
	// world camera still current: this pushes its own.
	void draw(wgpu2d::Renderer2D &renderer, glm::vec4 view, int width, int height,
		const Shown &shown = nullptr);

	void debugUi();
}
