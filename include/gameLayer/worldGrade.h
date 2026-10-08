#pragma once

// A colour grade on the world alone: grey and dim, the HUD untouched. The
// paused look (gameplay roadmap L1), and outside the closing circle (L4).
//
// The outside look is the same grade with a circle: the shader measures each
// pixel's distance from the circle's centre and greys only what is past its
// edge, fading in over a few pixels. The circle is in world units here and
// converted to screen pixels on the CPU, once, the way the cloak places its
// field -- the shader has no camera. So the edge of the safe zone shows as the
// line where colour stops, even when the ring itself is off screen.
//
// Why it goes in before the cloak rather than after. The cloak draws the
// world back through its own shader, straight to the screen, and a second
// full-screen pass would have to read that result -- a second target. But a
// grade is per pixel and the cloak only moves pixels, so the order does not
// change the picture: grading first and bending the graded world is the same
// image as bending first and grading. So the world is flushed into a target
// here, and one screen-covering quad of it goes back into the batch through
// the grade effect (F6's per-quad effects); the cloak then takes that batch
// exactly as it would have taken the world.
//
// **The fog** (sight roadmap S3) is a third grade, "unseen", for what the
// player cannot see. The whole view comes back through it, and then the
// player's sight -- S2's polar map, a fan round the ship -- is drawn on top
// from the same target without it. So inside the sight the world is in
// colour and outside it is grey, with no mask texture: the fan's shape is
// the mask. Each corner's texture coordinate is its pixel over the view's
// size, and the target is the view's size, so inside the fan every pixel
// samples the texel under it -- the same picture as no fog at all. A soft
// edge is a ring of triangles past the fan, fading out. The world is already
// in the target for the grade, so the fog costs the fan and nothing more --
// but with it on, the world goes through the target every frame.
//
// **Never visited** (after W6) is a level below unseen: black. W6's explored
// map -- a target over the arena, opaque where the player has looked and
// transparent where it never has -- is laid over the unseen grade with the
// Mask blend, which keeps what is drawn only as far as the map covers it.
// Then the sight goes on top, so three levels: in sight, in colour; seen
// before, the fog's grey; never seen, black.
//
// The shader lives in resources/shaders/worldGrade.wgsl.

#include <render/wgpu2d.h>
#include <engine/closingZone.h>
#include <engine/visibility.h>
#include <vector>

namespace worldGrade
{
	bool init();
	void cleanup();

	// Call after the world is drawn and before it is flushed (before
	// cloak::flushWorld). `pauseAmount` 0 .. 1 is how far into the paused look.
	// `safe`, when given, greys everything outside it; a view wholly inside it
	// is left alone. `sight`, when given and the fog is on, greys everything
	// outside it (S3). With none of them, it does nothing and costs nothing --
	// the world is not sent through a target at all.
	// `reveals`, with the fog on, are circles of sight drawn in colour too
	// (centre and radius, world units): what the player's beam lights.
	// `explored`, with the fog on and Never visited Black: what the player
	// has ever seen, a texture whose coverage is 1 where it has and 0 where
	// it has not, laid over the world rectangle `worldRect` (x, y, width,
	// height); past it, the edge texel.
	struct Reveal { glm::vec2 centre; float radius; };
	struct Explored { wgpu2d::Texture texture; glm::vec4 worldRect; };
	void apply(wgpu2d::Renderer2D &renderer, float pauseAmount, const zone::Circle *safe,
		int width, int height, const visibility::PolarMap *sight = nullptr,
		const std::vector<Reveal> *reveals = nullptr, const Explored *explored = nullptr);

	// Whether the fog is on at all (S3): with it off, nothing is hidden for
	// being out of sight either -- unseen would look the same as seen.
	bool fogOn();

	void debugUi();
}
