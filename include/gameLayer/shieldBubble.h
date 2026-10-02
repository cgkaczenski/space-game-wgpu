#pragma once

// One shield bubble's state, apart from the drawing (gameplay roadmap B1):
// what a ship owns, so energy -- and the Enemy that holds one -- can carry it
// without taking the renderer. shipShield.h draws it.

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace shield
{
	// A bubble's colours (B1): its rim, the flash of a hit, the glass, the
	// burning edge as it breaks, and the ripple. Under 1 per channel, for the
	// reason recorded in outline 14.
	struct Palette
	{
		glm::vec4 rim, flare, glass, edge, ripple;
	};
	Palette playerPalette();  // blue
	Palette enemyPalette();   // a hostile red-orange

	constexpr int maxImpacts = 4;

	// One ship's bubble as it looks right now (gameplay roadmap B1): every
	// ship that raises a shield has one, owned by its energy. The module keeps
	// only what all bubbles share -- the textures, the effects, the look's
	// tuning. A new bubble is up, easing in.
	struct Bubble
	{
		Palette palette = playerPalette();
		bool active = true;
		float level = 0.f;   // eased `active`, 0..1
		float flare = 0.f;   // spikes on a hit, decays
		float phase = 0.f;
		struct Impact
		{
			glm::vec2 direction = {0.f, -1.f}; // unit, from the ship's centre
			float elapsed = 0.f;
			float intensity = 0.f;
		};
		Impact impacts[maxImpacts];
		int nextImpact = 0;
		bool dissolving = false;
		float dissolveProgress = 0.f;
		// The ram's prow, drawn in the bubble's place (C4b).
		float ramLevel = 0.f;
		glm::vec2 ramDirection = {1.f, 0.f};
		float ramFlash = 0.f;
	};
}
