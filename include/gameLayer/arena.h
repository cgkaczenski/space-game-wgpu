#pragma once

// The level's circle (gameplay roadmap L2): past its edge, ships are pushed
// back in, and a faint ring shows where the edge is. L4's closing circle is
// this radius shrinking.
//
// A soft edge rather than a wall: the push grows with how far out a ship is,
// so the player can overshoot a little at speed and is eased back, not
// bounced. The player is pushed through its velocity, so the push fights the
// thrust and reads as a current; enemies move instantly (their velocity is
// set fresh each frame), so they are moved back directly.

#include <render/wgpu2d.h>

namespace arena
{
	// 0: no arena -- the endless mode with no level loaded.
	void setRadius(float radius);
	float radius();

	// Game time.
	void pushPlayer(glm::vec2 position, glm::vec2 &velocity, float gameDeltaTime);
	void pushEnemy(glm::vec2 &position, float gameDeltaTime);

	// The ring. **The caller must have set `BlendMode::Additive`.** `zoom` keeps
	// the line the same width on screen however far out the view is.
	void draw(wgpu2d::Renderer2D &renderer, float zoom);

	void debugUi();
}
