#pragma once

// Pins the window's CAMetalLayer to sRGB so windowed and fullscreen
// presentation agree. glfw3webgpu attaches the layer with no color space set,
// and macOS then interprets our pixels in the display's own space when it
// presents fullscreen (direct to display) and in sRGB when composited in a
// window: colors shift on entering fullscreen.
//
// Needs the GLFW window, so this lives in platform/, not in the drawing
// library. On anything other than Apple it is a no-op: there is nothing to
// pin. glfwMain always calls it, with no ifdef.

struct GLFWwindow;

namespace render
{
#ifdef __APPLE__
	// Returns false if the window's layer is not a CAMetalLayer.
	bool pinMetalLayerColorSpaceToSRGB(GLFWwindow *window);
#else
	inline bool pinMetalLayerColorSpaceToSRGB(GLFWwindow *window)
	{
		(void)window;
		return true;
	}
#endif

	// ---- GPU frame capture (roadmap N2c) --------------------------------
	//
	// This adapter has no TimestampQuery, so the renderer cannot time itself
	// (roadmap N2). Xcode's Metal trace can, per pass, and R1 already paid for
	// the half that makes a trace readable: 28 object labels and the debug
	// groups, so a capture shows "batch -> surface", "composite scaled target"
	// and "imgui" rather than anonymous draws. What was missing was a way to
	// start one without launching the binary from Xcode.
	//
	// Two things to know before this works:
	//
	//  - macOS refuses programmatic capture unless it was enabled before Metal
	//    started. Run with MTL_CAPTURE_ENABLED=1 in the environment.
	//  - wgpu-native exposes no handle to its MTLDevice -- there is no HAL
	//    escape hatch in wgpu.h -- so this captures MTLCreateSystemDefaultDevice().
	//    Metal device objects are per-GPU singletons, so that is the same
	//    object wgpu picked *provided* it chose the system default. The adapter
	//    report says which GPU it took; if that is ever not the default one,
	//    the trace will come back empty and this is why.
	//
	// Off Apple both are no-ops that report failure, so callers need no ifdef.
#if defined(__APPLE__)
	bool metalCaptureBegin(const char *gputracePath);
	void metalCaptureEnd();
#else
	inline bool metalCaptureBegin(const char *) { return false; }
	inline void metalCaptureEnd() {}
#endif
}
