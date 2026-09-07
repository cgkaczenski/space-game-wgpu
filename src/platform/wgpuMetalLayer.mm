#if defined(__APPLE__)

#include <platform/wgpuMetalLayer.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <iostream>

namespace render
{
	bool pinMetalLayerColorSpaceToSRGB(GLFWwindow *window)
	{
		NSWindow *nsWindow = glfwGetCocoaWindow(window);
		CALayer *layer = nsWindow.contentView.layer;
		if (![layer isKindOfClass:[CAMetalLayer class]])
		{
			std::cerr << "WebGPU: window layer is not a CAMetalLayer; color space not pinned\n";
			std::cerr.flush();
			return false;
		}
		CAMetalLayer *metalLayer = (CAMetalLayer *)layer;
		CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		metalLayer.colorspace = srgb; // the property retains it
		CGColorSpaceRelease(srgb);
		std::cout << "WebGPU Metal layer color space pinned to sRGB\n";
		std::cout.flush();
		return true;
	}
}

#endif
