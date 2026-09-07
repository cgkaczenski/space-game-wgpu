#if defined(__APPLE__)

#include <render/wgpuMetalLayer.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace render
{
	bool pinMetalLayerColorSpaceToSRGB(GLFWwindow *window)
	{
		NSWindow *nsWindow = glfwGetCocoaWindow(window);
		CALayer *layer = nsWindow.contentView.layer;
		if (![layer isKindOfClass:[CAMetalLayer class]])
		{
			return false;
		}
		CAMetalLayer *metalLayer = (CAMetalLayer *)layer;
		CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		metalLayer.colorspace = srgb; // the property retains it
		CGColorSpaceRelease(srgb);
		return true;
	}
}

#endif
