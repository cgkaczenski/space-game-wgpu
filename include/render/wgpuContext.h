#pragma once

// WebGPU render context. This header deliberately exposes no WebGPU types
// so the platform layer can call init / begin / end / shutdown without
// including webgpu.hpp.

struct GLFWwindow;

namespace render
{
	// Creates the WebGPU instance, wraps the GLFW window into a surface,
	// requests an adapter and a device with its queue, configures the
	// surface at the window's framebuffer size, and builds the sprite
	// pipeline and its layouts. Prints the adapter and the chosen surface
	// format to stdout. The window must have been created with
	// GLFW_CLIENT_API = GLFW_NO_API. Returns false on failure.
	bool wgpuInit(GLFWwindow *window);

	// Frame bracket, called by the platform loop around the game logic.
	// wgpuBeginFrame reconfigures the surface if the framebuffer size
	// changed, acquires the surface texture, and opens a command encoder.
	// Draws recorded by wgpu2d::Renderer2D::flush during the frame go into
	// one render pass that begins lazily (cleared to the recorded color).
	// wgpuEndFrame ends the pass, submits, and presents.
	void wgpuBeginFrame();
	void wgpuEndFrame();

	// Releases everything wgpuInit created, in reverse order.
	void wgpuShutdown();
}
