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
}
