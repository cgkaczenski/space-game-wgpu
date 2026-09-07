#pragma once

// macOS only. The CAMetalLayer that glfw3webgpu attaches to the window has
// no color space set, so macOS interprets our pixels in the display's own
// space when it presents fullscreen (direct to display) and in sRGB when
// composited in a window: colors shift on entering fullscreen. Pinning the
// layer to sRGB makes both paths agree.

struct GLFWwindow;

namespace render
{
	// Returns false if the window's layer is not a CAMetalLayer.
	bool pinMetalLayerColorSpaceToSRGB(GLFWwindow *window);
}
