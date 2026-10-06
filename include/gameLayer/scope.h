#pragma once

// The long-range scope (sight roadmap S4b): hold V to stop and look far.
//
// While it is held the ship brakes to a stop and cannot fire or ram, the view
// zooms well out and leans toward the pointer -- the nearer the screen's edge,
// the further -- and a narrow cone of sight opens a long way along the aim,
// measured from the ship. The player's all-round sight shrinks to a small
// circle meanwhile: looking far is a risk. Release, and it eases back.
//
// What it is for is S4: an enemy the scope sees is seen, and when the scope
// lets go it leaves a ghost where it was, with the HUD's arrow pointing to
// it. Nothing here makes ghosts; the scope only makes sight reach far.
//
// The cone follows sight's rules, only longer: rocks and field edges block,
// looking out of a field and into one included.

#include <sight.h>
#include <glm/vec2.hpp>

namespace scope
{
	// Once a frame, real time, before movement. `held`: V is down and the
	// player is in control. `pointerFraction`: the pointer from the screen's
	// centre as a share of half the screen. How far out it is -- eased, so
	// the scope lags it like a periscope -- zooms the view out further and
	// stretches the cone longer and narrower.
	void update(bool held, glm::vec2 pointerFraction, float realDeltaTime);

	// The scope's cone swings toward `aim` (unit), slowly, as a periscope
	// turns. Once a frame, after the aim is known.
	void turnToward(glm::vec2 aim, float realDeltaTime);

	bool held();       // braking, no firing, aim from the ship
	float amount();    // 0 .. 1, eased: zoom, lean, the cone and the shrinking

	// The ship's speed is kept by this much over `gameDeltaTime` while held.
	float brake(float gameDeltaTime);

	// The scope's cone: along where it has turned to, as long and as narrow as
	// how far out the pointer is; its look-out and look-in reach run to its
	// range.
	sight::Look cone();

	// For sight: the cone, how far in the scope is, and the radius the
	// player's all-round sight shrinks to.
	sight::Scope view();

	// The zoom: the normal one, zoomed out toward the scope's by `amount`.
	float zoom(float normalZoom);

	// The view's lean toward the (eased) pointer, world units, and how
	// quickly the view follows while scoping or settling back: slowly, so it
	// trails the pointer.
	glm::vec2 lean(glm::vec2 viewWorldSize);
	float cameraRate();

	void debugUi();
}
