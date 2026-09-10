#if defined(__APPLE__)

#include <platform/wgpuMetalLayer.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
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

// ---------------------------------------------------------------------------
// GPU frame capture. See the notes in wgpuMetalLayer.h for the two conditions
// this depends on: MTL_CAPTURE_ENABLED=1 in the environment, and wgpu having
// chosen the system default GPU.
// ---------------------------------------------------------------------------

namespace render
{

bool metalCaptureBegin(const char *gputracePath)
{
	MTLCaptureManager *manager = [MTLCaptureManager sharedCaptureManager];
	if ([manager isCapturing])
	{
		std::cerr << "metal capture: already capturing\n";
		return false;
	}

	if (![manager supportsDestination:MTLCaptureDestinationGPUTraceDocument])
	{
		std::cerr << "metal capture: GPU trace documents are not supported here."
			" Run with MTL_CAPTURE_ENABLED=1.\n";
		return false;
	}

	// wgpu-native hands out no MTLDevice, so this is the system default. Metal
	// device objects are per-GPU singletons, so it is the same object as long
	// as wgpu took the default GPU.
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	if (device == nil)
	{
		std::cerr << "metal capture: no default Metal device\n";
		return false;
	}

	MTLCaptureDescriptor *descriptor = [[MTLCaptureDescriptor alloc] init];
	descriptor.captureObject = device;
	descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
	descriptor.outputURL = [NSURL fileURLWithPath:
		[NSString stringWithUTF8String:gputracePath]];

	NSError *error = nil;
	if (![manager startCaptureWithDescriptor:descriptor error:&error])
	{
		std::cerr << "metal capture: could not start: "
			<< (error ? [[error localizedDescription] UTF8String] : "unknown") << "\n";
		return false;
	}

	std::cout << "metal capture: recording to " << gputracePath << "\n";
	std::cout.flush();
	return true;
}

void metalCaptureEnd()
{
	MTLCaptureManager *manager = [MTLCaptureManager sharedCaptureManager];
	if (![manager isCapturing]) { return; }
	[manager stopCapture];
	std::cout << "metal capture: written. Open the .gputrace in Xcode.\n";
	std::cout.flush();
}

}
