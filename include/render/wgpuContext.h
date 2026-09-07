#pragma once

// WebGPU render context: bring-up, the frame bracket, and teardown. This is
// the library's second public header, and the platform layer is its consumer.
// The game includes wgpu2d.h and never this.
//
// It names WebGPU's C types. It used to name none, so that glfwMain.cpp could
// stay free of WebGPU -- but the application now creates the surface, which
// means it already holds a WGPUSurface, so the rule bought nothing. The C
// header is enough: nothing here needs the C++ wrapper, so the application
// does not have to compile it.
//
// Why bring-up is two calls: creating a surface needs an instance, and only
// the application knows what a window is. So the library makes the instance,
// the application makes the surface from it and lends it for the frame
// bracket, then releases it after shutdown.
//
//     WGPUInstance instance = render::wgpuInitInstance();
//     WGPUSurface surface = glfwCreateWindowWGPUSurface(instance, window);
//     render::wgpuInit(surface, framebufferWidth, framebufferHeight);
//     ...
//     render::wgpuShutdown();
//     wgpuSurfaceRelease(surface);

#include <webgpu/webgpu.h>

namespace render
{
	// Creates the WebGPU instance and installs wgpu-native's log callback
	// (WGPU_LOG_LEVEL raises the level: off/error/warn/info/debug/trace).
	// No GPU and no window are involved yet. Idempotent. Null on failure.
	WGPUInstance wgpuInitInstance();

	// Requests an adapter that can drive `surface` and a device with its
	// queue, configures the surface at the given framebuffer size, and builds
	// the sprite pipeline and its layouts. Prints the adapter and the chosen
	// surface format to stdout. Returns false on failure.
	//
	// `surface` is borrowed. The application created it and must
	// wgpuSurfaceRelease it after wgpuShutdown. A failed init drops the
	// pointer without releasing, so the application still owns the handle.
	bool wgpuInit(WGPUSurface surface, int width, int height);

	// Reconfigures the surface when the framebuffer size changes. Cheap and
	// idempotent when it has not, so calling it every frame is fine -- and on
	// Metal that is what you want: wgpu-native never reports the surface
	// Outdated, it just keeps presenting a drawable of the configured size and
	// lets the layer stretch it. Miss this and sprites stretch while the
	// visible world does not change.
	void wgpuResize(int width, int height);

	// Frame bracket, called by the platform loop around the game logic.
	// wgpuBeginFrame acquires the surface texture and opens a command encoder.
	// Draws recorded by wgpu2d::Renderer2D::flush during the frame go into
	// one render pass that begins lazily (cleared to the recorded color).
	// wgpuEndFrame ends the pass, submits, and presents.
	void wgpuBeginFrame();
	void wgpuEndFrame();

	// Releases everything the two init calls created, in reverse order, except
	// the surface -- that is the application's, released after this returns.
	void wgpuShutdown();
}
