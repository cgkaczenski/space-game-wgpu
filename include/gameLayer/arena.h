#pragma once

// The level's circle (gameplay roadmap L2) and the closing circle inside it
// (L4).
//
// The **safe zone** starts as the level's circle and, if the level has rings,
// closes in stages to each of them, largest first, and finally to nothing:
// hold, close, hold, close, each close faster than the one before. A level
// with no rings keeps the zone on its circle for good.
//
// Nothing pushes anyone back in. Outside the zone, ships burn -- damage in
// ticks, past the shield -- and the world goes grey. The player can still mine
// out there; it costs health. Enemies burn too: one that is fighting keeps
// fighting wherever the fight goes, and one that is not flies back in. When
// the last ring has closed to nothing, the round is over for anyone still in
// it.
//
// The schedule is engine/closingZone, which knows nothing of this game. What
// is here is the policy: the timings, what outside costs, and how it looks.

#include <render/wgpu2d.h>
#include <engine/closingZone.h>
#include <level.h>
#include <vector>

namespace arena
{
	// A round's arena, from the level: the outer edge at `radius` round 0,0,
	// and the rings the circle closes to. Radius 0: no arena -- the endless
	// mode with no level loaded. No rings: the circle never closes.
	void start(float radius, const std::vector<level::Ring> &rings);
	float radius();

	// Game time: the circle closes, the ring's pulse runs, the burn flash
	// fades. Paused, nothing moves.
	void update(float gameDeltaTime);

	// Where the safe zone is now: the level's circle when nothing is closing.
	zone::Circle safeZone();
	bool closes(); // this round has a closing circle at all
	bool outside(glm::vec2 position);

	// The zone has reached the level's last ring -- holding on it, or closing
	// it to nothing. The gate opens here (gameplay roadmap L5).
	bool onFinalRing();

	// The last ring has closed to nothing: the round is up.
	bool collapsed();

	// What the player burns this frame for being at `position`: a tick's
	// damage on the frames a tick lands, 0 otherwise. Game time.
	float burn(glm::vec2 position, float gameDeltaTime);
	// 1 on a tick, fading: for flashing the hull.
	float burnFlash();

	// The same for an enemy, on its own timer and flash, which fade here too.
	float burnEnemy(Enemy &enemy, float gameDeltaTime);

	// The hull's red flash on a burn tick: additive, so the caller must have
	// set `BlendMode::Additive`. Drawn **after** the world is graded -- inside
	// the world it would be greyed with everything else outside the zone, and
	// a red flash that comes out grey says nothing. The caller passes the
	// hull's sprite; `flash` 0 draws nothing.
	void drawBurnFlash(wgpu2d::Renderer2D &renderer, float flash, glm::vec2 position,
		float size, wgpu2d::Texture sheet, glm::vec4 cell, glm::vec2 facing,
		float alpha = 1.f, float stretch = 1.f);

	// Where an enemy at `position` should fly to get back into the zone.
	// False when it is inside.
	bool wayBackIn(glm::vec2 position, glm::vec2 &to);

	// The rings. **The caller must have set `BlendMode::Additive`.** `zoom`
	// keeps the lines the same width on screen however far out the view is.
	// The safe zone pulses, and before each close flashes faster and faster
	// as it turns from blue through amber to red; red while it closes. A faint
	// white ring shows where it will close to next.
	void draw(wgpu2d::Renderer2D &renderer, float zoom);

	void debugUi();
}
