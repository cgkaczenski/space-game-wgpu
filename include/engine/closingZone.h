#pragma once

// A safe zone that closes in stages: hold still, then close to the next
// circle, then hold again (gameplay roadmap L4).
//
// **Only the schedule.** It says where the zone is and what it is doing; it
// does not know what being outside costs, how the edge is drawn, or who is
// inside. Those are a game's policy -- damage, colours, what enemies do about
// it -- and live with the game. Plain numbers, like movement and cameraFollow:
// no wgpu2d types, no clock. The caller passes a delta on whichever clock it
// means.
//
// A stage closes from the circle the zone is on to its `target`, centre and
// radius together, in a straight line. The edge does not move at one speed all
// the way round when the centre moves too: the side the centre moves away from
// travels by the shift plus the shrink. `closeSpeed` is that fastest point's
// speed, so no part of the edge ever moves faster than it -- which is what a
// player running from it cares about.

#include <glm/vec2.hpp>
#include <vector>

namespace zone
{
	struct Circle
	{
		glm::vec2 centre = {};
		float radius = 0.f;
	};

	struct Stage
	{
		Circle target;             // where this stage closes to
		float holdSeconds = 0.f;   // still, before the close starts
		float closeSpeed = 1.f;    // world units per second, the edge's fastest point
	};

	enum class Phase { Holding, Closing, Done };

	struct ClosingZone
	{
		std::vector<Stage> stages;
		int stage = 0;          // the one holding or closing; stages.size() when done
		Phase phase = Phase::Done;
		float elapsed = 0.f;    // seconds into the phase
		Circle from;            // where the current stage started
	};

	// A zone on `start`, holding for the first stage's hold. No stages: it is
	// Done at once and stays on `start`.
	ClosingZone begin(Circle start, std::vector<Stage> stages);

	// Moves it on by `deltaTime`. A delta longer than a phase carries into the
	// next, so a hitch does not stretch the schedule.
	void update(ClosingZone &zone, float deltaTime);

	// Ends the current phase now: a hold starts closing, a close lands.
	void skipPhase(ClosingZone &zone);

	// Where the zone is.
	Circle current(const ClosingZone &zone);

	// Where it goes next: the current stage's target, or where it is when done.
	Circle next(const ClosingZone &zone);

	// The current phase's length and how far into it the zone is, 0 .. 1.
	float phaseSeconds(const ClosingZone &zone);
	float phaseProgress(const ClosingZone &zone);

	// How long closing `from` to `to` takes at `speed` (see above).
	float closeSeconds(Circle from, Circle to, float speed);

	// How far past the edge `point` is; 0 inside.
	float outside(Circle circle, glm::vec2 point);
}
