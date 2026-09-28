#pragma once

// What a run is for (gameplay roadmap L3): deposits placed by the level, mined
// with the laser, carried, and banked by extracting.
//
//   Mining      the beam, and only the beam. Holding it on a deposit drains
//               that deposit into the hold. The beam costs charge and then a
//               cooldown, so a large deposit takes more than one pull -- and
//               firing uncloaks, so there is no mining unseen.
//   Interrupted a hit stops the drain for a moment. The beam keeps burning;
//               what was already taken stays taken.
//   Spent       an emptied deposit is destroyed the way a ship is (C4a),
//               through that same function: one blast, and the rock's own
//               sprite cut into pieces that fly apart, spin, slow and stay.
//               A worked route is marked by its rubble.
//   Orbs        ore does not appear in the hold. The beam knocks it off the
//               rock as orbs, thrown back toward the ship, which hang where
//               they stop. An orb only comes when the ship is near enough,
//               and then chases it like a missile -- turning harder and
//               running faster the longer the chase. Ore left too far away
//               stays where it is, so a deposit worked from cover has to be
//               collected afterwards.
//   Fragments   a dead enemy leaves the same orbs, worth very little.
//   Death       the hold spills at the wreck as one pile, to be mined again.
//   Extraction  the hold becomes points, which survive the next round.

#include <level.h>
#include <render/wgpu2d.h>
#include <vector>

namespace resources
{
	bool init();
	void cleanup();

	// A new round: the level's deposits, full, and an empty hold. Points
	// banked by extracting are not a round's, and stay.
	void reset(const std::vector<level::Resource> &placed);

	// Game time. Moves the fragments and their pull toward the player, and
	// runs out the interruption.
	void update(float gameDeltaTime, glm::vec2 playerPos, bool playerPresent);

	// The beam stops at a deposit as it does at an enemy: how far along the
	// ray the nearest one is, or a negative number for none. `reach` is how
	// far the beam gets otherwise.
	float rayToDeposit(glm::vec2 origin, glm::vec2 direction, float reach);

	// The beam is resting on the deposit the ray above found. Drains it, and
	// returns what went into the hold this frame (0 while interrupted).
	float mine(glm::vec2 origin, glm::vec2 direction, float gameDeltaTime);

	// A hit landed on the player: mining stops for a moment.
	void interrupt();

	// A dead enemy's fragments, and a dead player's hold.
	void enemyDropped(glm::vec2 position);
	void playerDropped(glm::vec2 position);

	// The hold becomes points.
	void extracted();

	float held();    // this round, not yet banked
	float banked();  // points, across rounds

	// Deposits and husks, under alpha with the ships.
	void draw(wgpu2d::Renderer2D &renderer);
	// Their glow and the orbs. **Caller must have set BlendMode::Additive.**
	void drawGlow(wgpu2d::Renderer2D &renderer, float time);

	// What the editor and the loader default a deposit to.
	float defaultAmount();

	void debugUi();
}
