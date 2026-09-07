#pragma once

// A layer effect: flush everything recorded so far through an offscreen
// target, then draw that target back under a transform.
//
// This is the mechanism half of what used to be hudShake.cpp. Nothing here
// knows what a HUD is, how hard a hit shakes it, or how fast that decays --
// the caller owns all of that and hands over a finished transform. Milestone
// 10 built the target machinery; this is the shape it wanted.
//
// Why the round trip is worth it at all: a composed layer can be moved and
// rotated as one image. Nudging each quad separately cannot rotate a group
// about a shared centre without shearing the layout apart.

#include <render/wgpu2d.h>

namespace render
{
	// How the composed layer is drawn back. Screen-space pixels, and degrees
	// about the layer's centre.
	//
	// Deliberately just these two. A tint, a scale and a per-effect shader all
	// have plausible futures, and adding a field when one arrives is a few
	// lines; a transform designed around three imagined callers is how the
	// wrong abstraction gets built. Same reasoning as the pipeline key in 12.
	struct LayerTransform
	{
		glm::vec2 offsetPixels = {};
		float rotationDegrees = 0.f;

		// Exact comparison on purpose: a caller that settles its animation to
		// exactly zero at rest gets the bypass below, and one that decays
		// asymptotically never does. That is a real property to design to, not
		// a float-equality bug -- see the settle in hudShake.
		bool isIdentity() const
		{
			return offsetPixels.x == 0.f && offsetPixels.y == 0.f && rotationDegrees == 0.f;
		}
	};

	struct LayerEffect
	{
		// Flushes everything the renderer has recorded since its last flush
		// through the target, then records one quad drawing that target back
		// under `transform`. The caller's next flush is what puts it on screen,
		// on top of whatever was flushed before it.
		//
		// `width` and `height` are the framebuffer size -- the same values the
		// game passes to updateWindowMetrics -- and the target follows them.
		//
		// An identity transform skips the target entirely and flushes straight
		// to the screen. That is not an optimisation the caller has to
		// remember: it keeps the untransformed case pixel-exact, since a round
		// trip through a same-size target is only byte-identical for opaque
		// pixels.
		void flush(wgpu2d::Renderer2D &renderer, int width, int height, const LayerTransform &transform);

		// Releases the target. Safe to call twice, and safe never to call.
		void cleanup();

		// The composed layer. Screen-sized, resized with the window so the
		// composite is 1:1.
		wgpu2d::FrameBuffer target;
	};
}
