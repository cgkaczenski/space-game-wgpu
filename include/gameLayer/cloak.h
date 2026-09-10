#pragma once

// The player ship's refraction cloak: the scene behind it bends.
//
// This is the effect that could not be done with a blend mode, and saying why
// is the point of it. Blending combines a fragment with the destination; it
// cannot read the destination and move it, and core WebGPU has no framebuffer
// fetch. Displacing what is behind the ship means *sampling* a texture that
// already holds the scene — so the world is rendered into a target and this
// draws that target back through a fragment shader.
//
// Note what is *not* here. A fade-out cloak — the ship going translucent —
// needs none of this: it is one float of vertex alpha on the existing sprite
// pipeline. The shimmer is a different feature that happens to share a name.
//
// The shader lives in resources/shaders/cloak.wgsl. The library compiles its
// own shaders in and has no resource layout (outline 13); an application's
// effect is the application's, so this one is a file it can edit and reload.

#include <render/wgpu2d.h>

namespace cloak
{
	// Loads and compiles the effect. Call once from initGame, after the
	// renderer exists. False if the file is missing or does not compile.
	bool init();
	void cleanup();

	// Whether the cloak is engaged. The distortion eases in and out, so this
	// can be flipped freely.
	void setActive(bool active);
	bool isActive();

	// How visible the hull should be, 1 down to `hiddenAlpha` when fully
	// engaged. The caller tints the ship with it.
	//
	// The hull fades rather than vanishing: something has to remain for the
	// distortion to carry, and a completely absent ship reads as a bug rather
	// than as a cloak. What sells it is the *pair* -- a faint outline plus the
	// background bending around it.
	float shipAlpha();

	// Flushes the world.
	//
	// Call this where the world's `renderer.flush()` would go. When the cloak
	// is down it *is* that flush, and costs exactly nothing extra. When it is
	// up, the world goes into a target and comes back through the shader.
	//
	// `shipWorldPos` is where to bend around; it is converted to screen pixels
	// here, because the shader works in pixels and the caller thinks in world
	// units. `dt` is game time, like the plume's.
	// `shipWorldSize` sizes the distortion field, so it tracks the ship's
	// apparent size and stays right when the camera zooms -- a field fixed to
	// a fraction of the screen would not.
	void flushWorld(wgpu2d::Renderer2D &renderer, glm::vec2 shipWorldPos,
		float shipWorldSize, int width, int height, float dt);

	// This feature's own debug controls (roadmap R11).
	void debugUi();
}
