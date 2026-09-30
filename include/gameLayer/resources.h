#pragma once

// What a run is for (gameplay roadmap L3, A4): ore mined from asteroids,
// carried, and banked by extracting.
//
//   Mining      the beam, and only the beam, on any asteroid but a field's
//               core. The rock sheds its ore as it is worn down (asteroids.h);
//               this module is what the ore becomes. The beam costs charge and
//               then a cooldown, so a big rock takes more than one pull -- and
//               firing uncloaks, so there is no mining unseen.
//   Interrupted a hit stops the shedding for a moment. The beam keeps burning;
//               what was already taken stays taken.
//   Orbs        ore does not appear in the hold. The beam knocks it off the
//               rock as orbs, thrown back toward the ship, which hang where
//               they stop. An orb only comes when the ship is near enough,
//               and then chases it like a missile -- turning harder and
//               running faster the longer the chase. Ore left too far away
//               stays where it is, so a rock worked from cover has to be
//               collected afterwards.
//   Fragments   a dead enemy leaves the same orbs, worth very little.
//   Death       the hold scatters from the wreck as orbs.
//   Extraction  the hold becomes points, which survive the next round.
//
// Until A4 the level placed deposits -- glowing rocks that were only ore.
// Asteroids replaced them: every rock is ore now, and cover, and in the way.

#include <render/wgpu2d.h>

namespace resources
{
	bool init();
	void cleanup();

	// A new round: an empty hold and no orbs. Points banked by extracting are
	// not a round's, and stay.
	void reset();

	// Game time. Moves the orbs and their pull toward the player, and runs out
	// the interruption.
	void update(float gameDeltaTime, glm::vec2 playerPos, bool playerPresent);

	// A hit landed on the player: mining stops for a moment.
	void interrupt();
	bool interrupted();

	// An orb of ore worth `value`, thrown from `at` along `direction` (unit).
	void emitOrb(glm::vec2 at, glm::vec2 direction, float value);

	// A dead enemy's fragments, and a dead player's hold.
	void enemyDropped(glm::vec2 position);
	void playerDropped(glm::vec2 position);

	// The hold becomes points.
	void extracted();

	float held();    // this round, not yet banked
	float banked();  // points, across rounds

	// The orbs. **Caller must have set BlendMode::Additive.**
	void drawGlow(wgpu2d::Renderer2D &renderer, float time);

	void debugUi();
}
