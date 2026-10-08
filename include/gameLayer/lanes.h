#pragma once

// High-speed lanes (sight roadmap W4): paths with a width, open space cut
// through the fields (level::fieldsAsPlayed), whose current carries every
// ship -- the player and each enemy, in either mode -- along them, either
// way, faster than it flies on its own.
//
// This is the policy; the mechanism is engine/movement's Medium. In a lane
// a ship's medium has:
//   - a top speed of the lane's `speed` times its own, along the lane: the
//     more a ship's motion lines up with the lane, the more of that it gets,
//     so flying across a lane is no faster than flying anywhere else;
//   - a push along the lane, whichever way along it the ship is going;
//   - a hold: its speed across the lane is damped, so a ship that enters at
//     an angle is turned into the current. Thrust beats it, to leave.
// Leaving, a ship is thrown out at speed: what it has above its own top
// speed bleeds off over a moment instead of snapping away. Out of a lane,
// the fields' own slowness (W2, interior) applies as ever.
//
// "Rail" is the other choice: the full lane speed whatever the angle, a far
// stronger push and hold -- carried along the line, steering only to leave.
//
// In flight mode two more things happen in a lane, which the game applies
// (it knows the mode): entering one gives a kick of speed along it, with the
// ram's afterimages for a moment; and Shift, instead of braking, slides --
// the speed is kept, and the velocity swings round toward where the nose
// points, fast, so a bend can be carved without leaving the lane.

#include <render/wgpu2d.h>
#include <engine/movement.h>
#include <glm/vec2.hpp>
#include <vector>

namespace level { struct Lane; }

namespace lanes
{
	// Per ship: which lane it is in, and what that has meant lately.
	struct Rider
	{
		bool coasting = false;      // still carrying a lane's speed out of it
		int lane = -1;              // the lane it is in, or -1
		bool entered = false;       // it came into a lane this frame, after a while out of any
		float outFor = 1e9f;        // seconds since it was last in a lane
		glm::vec2 direction = {};   // which way along its lane it is being carried
	};

	// This round's lanes. Call when a round starts.
	void start(const std::vector<level::Lane> &lanes);
	const std::vector<level::Lane> &all();

	// What `body` is flying through this frame: a lane's current, or the
	// fields' and open space's (interior::at), with a lane's speed still
	// bleeding off if it has just left one. Call before the body steps; it
	// also brings `rider` up to date. Game time.
	movement::Medium mediumFor(Rider &rider, const movement::Body &body, float gameDeltaTime);

	// Flight mode's kick on entering a lane: speed added along it, and how
	// long the ram's afterimages run after.
	float entryBoost();
	float entryTrailSeconds();

	// The slide (Shift, in a lane, in flight mode): turns `body`'s velocity
	// toward where it faces, keeping its speed. After the body has stepped.
	void slide(movement::Body &body, float gameDeltaTime);

	// Which lane `point` is in, or -1.
	int laneAt(glm::vec2 point);

	// The lanes, in the world's camera: their edges and a dashed middle, a
	// fixed width on screen. `riding` brightens the one the player is in.
	void draw(wgpu2d::Renderer2D &renderer, int riding = -1);

	// The same for any lanes -- the editor's, as they are being drawn.
	// `selected` brightens one.
	void drawLanes(wgpu2d::Renderer2D &renderer, const std::vector<level::Lane> &lanes, int selected);

	void debugUi();
}
