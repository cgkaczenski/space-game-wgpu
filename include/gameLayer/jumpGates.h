#pragma once

// Jump gates (sight roadmap W5): pairs of gates, where flying into one brings
// a ship out of the other, moving the way it went in. A road across a big
// level, not an exit: no charging, any time, uncloaked -- then that pair
// rests a few seconds, so a ship cannot bounce straight back.
//
//   Short transit (the default): the hull stretches and the screen goes white
//     over a moment; at the whitest the ship is moved to the far gate, and the
//     white clears there. The far end's rocks are made during the white
//     (asteroids::streamAllNext), so nothing pops in.
//   Instant: moved the same frame, behind a brief white flash.
//
// Coming out, the player has a grace: shots, beams and rams pass through, as
// when cloaked, and it cannot fire -- time to see where it is before anyone
// can hit it. Through the transit too.
//
// Enemies chasing the player -- engaged or searching -- that reach the gate it
// took within a few seconds come through after it. Others ignore the gates.
//
// The look is the extraction gate's black hole (gate::drawHole) in violet,
// with a ring: dim while the pair rests.

#include <render/wgpu2d.h>
#include <engine/movement.h>
#include <glm/vec2.hpp>
#include <vector>

namespace level { struct JumpPair; }

namespace jumpGates
{
	// This round's pairs. Call when a round starts.
	void start(const std::vector<level::JumpPair> &pairs);

	// The player, once a frame after it has moved. `canUse`: present, in
	// control and uncloaked. Starts a jump on entering a gate, runs the
	// transit, and moves `ship` to the far gate when it is time. True on the
	// frame it is moved: snap whatever follows it.
	bool updatePlayer(movement::Body &ship, bool canUse, float gameDeltaTime);

	// An enemy, once a frame after it has moved. `chasing`: engaged with the
	// player or searching for it. If the player has just gone through a gate
	// and this enemy reaches it in time, it comes out of the other. True if
	// it was moved.
	bool updateEnemy(movement::Body &body, bool chasing);

	bool inTransit();
	// The player can be neither hit nor fire: in transit, or in the grace
	// after coming out.
	bool shielded();

	// For the CRT and the hull: how white the screen is, 0 .. 1; how blurred
	// outward, 0 .. 1; how long the hull is drawn, 1 normal.
	float whiteOut();
	float warpBlur();
	float stretch();

	// The gates, one at a time: for the HUD's markers and the swirl.
	int count();
	glm::vec2 position(int gate);
	bool resting(int gate);
	float radius();
	// The nearest gate to `from` that is not resting, if any within `within`.
	bool nearest(glm::vec2 from, float within, glm::vec2 &at);
	// How hard the world swirls round one, and how far.
	float swirlStrength();
	float swirlRadius();

	// The black holes, in alpha, under the ships; then the rings, which want
	// `BlendMode::Additive` set.
	void drawBodies(wgpu2d::Renderer2D &renderer);
	void drawRings(wgpu2d::Renderer2D &renderer, float zoom);

	void debugUi();
}
